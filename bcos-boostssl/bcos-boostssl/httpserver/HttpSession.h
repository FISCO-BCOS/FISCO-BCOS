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
 * @file HttpSession.h
 * @author: octopus
 * @date 2021-07-08
 */
#pragma once
#include <bcos-boostssl/httpserver/Common.h>
#include <bcos-boostssl/httpserver/HttpQueue.h>
#include <bcos-boostssl/httpserver/HttpStream.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/ThreadPool.h>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/websocket.hpp>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>

namespace bcos::boostssl::http
{
// The http session for connection
class HttpSession : public std::enable_shared_from_this<HttpSession>
{
public:
    using Ptr = std::shared_ptr<HttpSession>;

public:
    explicit HttpSession()
    {
        HTTP_SESSION(DEBUG) << LOG_KV("[NEWOBJ][HTTPSESSION]", this);

        m_buffer = std::make_shared<boost::beast::flat_buffer>();
    }

    virtual ~HttpSession()
    {
        doClose();
        HTTP_SESSION(DEBUG) << LOG_KV("[DELOBJ][HTTPSESSION]", this);
    }

    // start the HttpSession
    void run()
    {
        boost::asio::dispatch(m_httpStream->stream().get_executor(),
            boost::beast::bind_front_handler(&HttpSession::doRead, shared_from_this()));
    }

    void doRead()
    {
        m_parser.emplace();
        // set limit to http request size, 100m
        m_parser->body_limit(PARSER_BODY_LIMITATION);

        auto buffer = m_buffer;
        auto session = shared_from_this();
        m_httpStream->asyncRead(*m_buffer, m_parser,
            [session, buffer](boost::system::error_code _ec, std::size_t bytes_transferred) {
                session->onRead(_ec, bytes_transferred);
            });
    }

    void onRead(boost::beast::error_code ec, std::size_t bytes_transferred)
    {
        try
        {
            // the peer client closed the connection
            if (ec == boost::beast::http::error::end_of_stream)
            {
                HTTP_SESSION(TRACE) << LOG_BADGE("onRead") << LOG_DESC("end of stream");
                // return doClose();
                return;
            }

            if (ec)
            {
                HTTP_SESSION(WARNING) << LOG_BADGE("onRead") << LOG_DESC("close the connection")
                                      << LOG_KV("failed", ec);
                // return doClose();
                return;
            }

            auto self = std::weak_ptr<HttpSession>(shared_from_this());
            if (boost::beast::websocket::is_upgrade(m_parser->get()))
            {
                HTTP_SESSION(INFO) << LOG_BADGE("onRead") << LOG_DESC("websocket upgrade");
                if (m_wsUpgradeHandler)
                {
                    auto httpSession = self.lock();
                    if (!httpSession)
                    {
                        return;
                    }
                    m_wsUpgradeHandler(m_httpStream, m_parser->release(), httpSession->nodeId());
                }
                else
                {
                    HTTP_SESSION(WARNING) << LOG_BADGE("onRead")
                                          << LOG_DESC(
                                                 "the session will be closed for "
                                                 "unsupported websocket upgrade");
                    // doClose();
                    return;
                }
                return;
            }

            HTTP_SESSION(TRACE) << LOG_BADGE("onRead") << LOG_DESC("receive http request");

            handleRequest(m_parser->release());
        }
        catch (...)
        {
            HTTP_SESSION(WARNING) << LOG_DESC("onRead exception")
                                  << LOG_KV("bytesSize", bytes_transferred)
                                  << LOG_KV("failed",
                                         boost::current_exception_diagnostic_information());
        }

        if (!m_queue->isFull())
        {
            doRead();
        }
    }

    void onWrite(bool close, boost::beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
        {
            HTTP_SESSION(WARNING) << LOG_BADGE("onWrite") << LOG_DESC("close the connection")
                                  << LOG_KV("failed", ec);
            doClose();
            return;
        }

        if (close)
        {
            // we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            doClose();
            return;
        }

        if (m_queue->onWrite())
        {
            // read the next request
            doRead();
        }
    }

    void doClose()
    {
        if (m_httpStream)
        {
            m_httpStream->close();
        }
    }

    /**
     * @brief: handle http request and send the response
     * @param req: http request object
     * @return void:
     */
    void handleRequest(HttpRequest&& _httpRequest)
    {
        HTTP_SESSION(DEBUG) << LOG_BADGE("handleRequest") << LOG_DESC("request")
                            << LOG_KV("method", _httpRequest.method_string())
                            << LOG_KV("target", _httpRequest.target())
                            << LOG_KV("body", _httpRequest.body())
                            << LOG_KV("keep_alive", _httpRequest.keep_alive())
                            << LOG_KV("need_eof", _httpRequest.need_eof());

        auto startT = utcTime();
        unsigned version = _httpRequest.version();
        auto self = std::weak_ptr<HttpSession>(shared_from_this());
        if (m_httpReqHandler)
        {
            std::string request = _httpRequest.body();
            m_httpReqHandler(request, [self, version, startT](bcos::bytes _content) {
                auto session = self.lock();
                if (!session)
                {
                    return;
                }
                auto resp = session->buildHttpResp(
                    boost::beast::http::status::ok, version, std::move(_content));
                // put the response into the queue and waiting to be send
                session->queue()->enqueue(resp);
                BCOS_LOG(TRACE) << LOG_BADGE("handleRequest") << LOG_DESC("response")
                                << LOG_KV("body", std::string_view((const char*)resp->body().data(),
                                                      resp->body().size()))
                                << LOG_KV("keep_alive", resp->keep_alive())
                                << LOG_KV("timecost", (utcTime() - startT));
            });
        }
        else
        {
            // unsupported http service
            auto resp =
                buildHttpResp(boost::beast::http::status::http_version_not_supported, version, {});
            auto session = self.lock();
            if (!session)
            {
                return;
            }
            // put the response into the queue and waiting to be send
            session->queue()->enqueue(resp);

            HTTP_SESSION(WARNING) << LOG_BADGE("handleRequest")
                                  << LOG_DESC("unsupported http service")
                                  << LOG_KV(
                                         "body", std::string_view((const char*)resp->body().data(),
                                                     resp->body().size()));
        }
    }

    /**
     * @brief: build http response object
     * @param status: http response status
     * @param content: http response content
     * @return HttpResponsePtr:
     */
    HttpResponsePtr buildHttpResp(
        boost::beast::http::status status, unsigned version, bcos::bytes content)
    {
        auto msg = std::make_shared<HttpResponse>(status, version);
        msg->set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        msg->set(boost::beast::http::field::content_type, "application/json");
        msg->keep_alive(true);  // default , keep alive
        msg->body() = std::move(content);
        msg->prepare_payload();
        return msg;
    }

    HttpReqHandler httpReqHandler() const { return m_httpReqHandler; }
    void setRequestHandler(HttpReqHandler _httpReqHandler)
    {
        m_httpReqHandler = std::move(_httpReqHandler);
    }

    WsUpgradeHandler wsUpgradeHandler() const { return m_wsUpgradeHandler; }
    void setWsUpgradeHandler(WsUpgradeHandler _wsUpgradeHandler)
    {
        m_wsUpgradeHandler = std::move(_wsUpgradeHandler);
    }
    std::shared_ptr<Queue> queue() { return m_queue; }
    void setQueue(std::shared_ptr<Queue> _queue) { m_queue = std::move(_queue); }

    HttpStream::Ptr httpStream() { return m_httpStream; }
    void setHttpStream(HttpStream::Ptr _httpStream) { m_httpStream = std::move(_httpStream); }

    std::shared_ptr<std::string> nodeId() { return m_nodeId; }
    void setNodeId(std::shared_ptr<std::string> _nodeId) { m_nodeId = std::move(_nodeId); }


private:
    HttpStream::Ptr m_httpStream;
    std::shared_ptr<boost::beast::flat_buffer> m_buffer;

    std::shared_ptr<Queue> m_queue;
    HttpReqHandler m_httpReqHandler;
    WsUpgradeHandler m_wsUpgradeHandler;
    // the parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    boost::optional<boost::beast::http::request_parser<boost::beast::http::string_body>> m_parser;

    std::shared_ptr<std::string> m_nodeId;
};

}  // namespace bcos::boostssl::http
