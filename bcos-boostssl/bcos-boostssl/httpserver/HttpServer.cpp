/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  m_limitations under the License.
 *
 * @file HttpHttpServer.h
 * @author: octopus
 * @date 2021-07-08
 */

#include <bcos-boostssl/context/NodeInfoTools.h>
#include <bcos-boostssl/httpserver/HttpServer.h>
#include <bcos-utilities/ThreadPool.h>
#include <memory>
#include <utility>

using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::http;
using namespace bcos::boostssl::context;

// start http server
void HttpServer::start()
{
    if (m_acceptor && m_acceptor->is_open())
    {
        HTTP_SERVER(INFO) << LOG_BADGE("startListen") << LOG_DESC("http server is running");
        return;
    }

    HTTP_SERVER(INFO) << LOG_BADGE("startListen") << LOG_KV("listenIP", m_listenIP)
                      << LOG_KV("listenPort", m_listenPort);

    auto address = boost::asio::ip::make_address(m_listenIP);
    auto endpoint = boost::asio::ip::tcp::endpoint{address, m_listenPort};

    boost::beast::error_code ec;
    m_acceptor->open(endpoint.protocol(), ec);
    if (ec)
    {
        HTTP_SERVER(WARNING) << LOG_BADGE("open") << LOG_KV("failed", ec)
                             << LOG_KV("message", ec.message());
        BOOST_THROW_EXCEPTION(std::runtime_error("acceptor open failed"));
    }

    // allow address reuse
    m_acceptor->set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec)
    {
        HTTP_SERVER(WARNING) << LOG_BADGE("set_option") << LOG_KV("failed", ec)
                             << LOG_KV("message", ec.message());

        BOOST_THROW_EXCEPTION(std::runtime_error("acceptor set_option failed"));
    }

    m_acceptor->bind(endpoint, ec);
    if (ec)
    {
        HTTP_SERVER(WARNING) << LOG_BADGE("bind") << LOG_KV("failed", ec)
                             << LOG_KV("message", ec.message());
        BOOST_THROW_EXCEPTION(std::runtime_error("acceptor bind failed"));
    }

    m_acceptor->listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec)
    {
        HTTP_SERVER(WARNING) << LOG_BADGE("listen") << LOG_KV("failed", ec)
                             << LOG_KV("message", ec.message());
        BOOST_THROW_EXCEPTION(std::runtime_error("acceptor listen failed"));
    }

    // start accept
    accept();

    HTTP_SERVER(INFO) << LOG_BADGE("startListen") << LOG_KV("ip", endpoint.address().to_string())
                      << LOG_KV("port", endpoint.port());
}

void HttpServer::stop()
{
    if (m_acceptor && m_acceptor->is_open())
    {
        m_acceptor->close();
    }


    HTTP_SERVER(INFO) << LOG_BADGE("stop") << LOG_DESC("http server");
}

void HttpServer::accept()
{
    // The new connection gets its own strand
    m_acceptor->async_accept(*(m_ioservicePool->getIOService()),
        boost::beast::bind_front_handler(&HttpServer::onAccept, shared_from_this()));
}

void HttpServer::onAccept(boost::beast::error_code ec, boost::asio::ip::tcp::socket socket)
{
    if (ec)
    {
        HTTP_SERVER(WARNING) << LOG_BADGE("accept") << LOG_KV("failed", ec)
                             << LOG_KV("message", ec.message());
        accept();
        return;
    }

    boost::system::error_code sec;
    auto localEndpoint = socket.local_endpoint(sec);
    if (sec)
    {
        HTTP_SERVER(WARNING) << LOG_BADGE("accept") << LOG_KV("local_endpoint failed", sec)
                             << LOG_KV("message", sec.message());
        ws::WsTools::close(socket);
        accept();
        return;
    }
    auto remoteEndpoint = socket.remote_endpoint(sec);
    if (sec)
    {
        HTTP_SERVER(WARNING) << LOG_BADGE("accept") << LOG_KV("remote_endpoint failed", sec)
                             << LOG_KV("message", sec.message());
        ws::WsTools::close(socket);
        accept();
        return;
    }
    socket.set_option(boost::asio::ip::tcp::no_delay(true));

    HTTP_SERVER(INFO) << LOG_BADGE("accept") << LOG_KV("local_endpoint", localEndpoint)
                      << LOG_KV("remote_endpoint", remoteEndpoint);

    bool useSsl = !disableSsl();
    if (!useSsl)
    {  // non ssl , start http session
        auto httpStream = m_httpStreamFactory->buildHttpStream(
            std::make_shared<boost::beast::tcp_stream>(std::move(socket)));
        buildHttpSession(httpStream, nullptr)->run();

        accept();
        return;
    }

    // ssl should be used,  start ssl handshake
    auto self = std::weak_ptr<HttpServer>(shared_from_this());
    auto ss = std::make_shared<boost::beast::ssl_stream<boost::beast::tcp_stream>>(
        boost::beast::tcp_stream(std::move(socket)), *m_ctx);

    std::shared_ptr<std::string> nodeId = std::make_shared<std::string>();
    ss->set_verify_callback(NodeInfoTools::newVerifyCallback(nodeId));

    ss->async_handshake(boost::asio::ssl::stream_base::server,
        [ss, localEndpoint, remoteEndpoint, nodeId, self](boost::beast::error_code _ec) {
            if (_ec)
            {
                HTTP_SERVER(INFO) << LOG_BADGE("async_handshake")
                                  << LOG_DESC("ssl handshake failed")
                                  << LOG_KV("local", localEndpoint)
                                  << LOG_KV("remote", remoteEndpoint)
                                  << LOG_KV("message", _ec.message());
                ws::WsTools::close(ss->next_layer().socket());
                return;
            }

            if (auto server = self.lock())
            {
                auto httpStream = server->httpStreamFactory()->buildHttpStream(ss);
                server->buildHttpSession(httpStream, nodeId)->run();
            }
        });

    accept();
}


HttpSession::Ptr HttpServer::buildHttpSession(
    HttpStream::Ptr _httpStream, std::shared_ptr<std::string> _nodeId)
{
    auto session = std::make_shared<HttpSession>();

    Queue queue;
    queue.setSender([self = std::weak_ptr<HttpSession>(session)](HttpResponsePtr _httpResp) {
        auto session = self.lock();
        if (!session)
        {
            return;
        }

        auto* httpRespPtr = _httpResp.get();
        session->httpStream()->asyncWrite(
            *httpRespPtr, [self, _httpResp = std::move(_httpResp)](
                              boost::beast::error_code ec, std::size_t bytes_transferred) {
                if (auto session = self.lock())
                {
                    session->onWrite(_httpResp->need_eof(), ec, bytes_transferred);
                }
            });
    });

    session->setQueue(std::move(queue));
    session->setHttpStream(std::move(_httpStream));
    session->setRequestHandler(m_httpReqHandler);
    session->setWsUpgradeHandler(m_wsUpgradeHandler);
    session->setNodeId(std::move(_nodeId));

    return session;
}

/**
 * @brief: create http server
 * @param _listenIP: listen ip
 * @param _listenPort: listen port
 * @param _threadCount: thread count
 * @param _ioc: io_context
 * @param _ctx: ssl context
 * @return HttpServer::Ptr:
 */
HttpServer::Ptr HttpServerFactory::buildHttpServer(const std::string& _listenIP,
    uint16_t _listenPort, std::shared_ptr<boost::asio::io_context> _ioc,
    std::shared_ptr<boost::asio::ssl::context> _ctx)
{
    // create httpserver and launch a listening port
    auto server = std::make_shared<HttpServer>(_listenIP, _listenPort);
    auto acceptor = std::make_shared<boost::asio::ip::tcp::acceptor>(*_ioc);
    auto httpStreamFactory = std::make_shared<HttpStreamFactory>();
    server->setCtx(std::move(_ctx));
    server->setAcceptor(acceptor);
    server->setHttpStreamFactory(httpStreamFactory);

    HTTP_SERVER(INFO) << LOG_BADGE("buildHttpServer") << LOG_KV("listenIP", _listenIP)
                      << LOG_KV("listenPort", _listenPort);
    return server;
}
