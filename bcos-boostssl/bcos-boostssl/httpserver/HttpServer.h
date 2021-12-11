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
#pragma once

#include <bcos-boostssl/httpserver/HttpSession.h>
#include <exception>
#include <thread>
namespace bcos
{
namespace boostssl
{
namespace http
{
// The http server impl
class HttpServer : public std::enable_shared_from_this<HttpServer>
{
public:
    using Ptr = std::shared_ptr<HttpServer>;

public:
    HttpServer(const std::string& _listenIP, uint16_t _listenPort)
      : m_listenIP(_listenIP), m_listenPort(_listenPort)
    {}

    ~HttpServer() { stop(); }

public:
    // start http server
    void start();
    void stop();

    // accept connection
    void doAccept();
    // handle connection
    void onAccept(boost::beast::error_code ec, boost::asio::ip::tcp::socket socket);

public:
    HttpSession::Ptr buildHttpSession(HttpStream::Ptr _stream);

public:
    HttpReqHandler httpReqHandler() const { return m_httpReqHandler; }
    void setHttpReqHandler(HttpReqHandler _httpReqHandler) { m_httpReqHandler = _httpReqHandler; }

    std::shared_ptr<boost::asio::io_context> ioc() const { return m_ioc; }
    void setIoc(std::shared_ptr<boost::asio::io_context> _ioc) { m_ioc = _ioc; }

    std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor() const { return m_acceptor; }
    void setAcceptor(std::shared_ptr<boost::asio::ip::tcp::acceptor> _acceptor)
    {
        m_acceptor = _acceptor;
    }

    std::shared_ptr<boost::asio::ssl::context> ctx() const { return m_ctx; }
    void setCtx(std::shared_ptr<boost::asio::ssl::context> _ctx) { m_ctx = _ctx; }

    WsUpgradeHandler wsUpgradeHandler() const { return m_wsUpgradeHandler; }
    void setWsUpgradeHandler(WsUpgradeHandler _wsUpgradeHandler)
    {
        m_wsUpgradeHandler = _wsUpgradeHandler;
    }

    HttpStreamFactory::Ptr httpStreamFactory() const { return m_httpStreamFactory; }
    void setHttpStreamFactory(HttpStreamFactory::Ptr _httpStreamFactory)
    {
        m_httpStreamFactory = _httpStreamFactory;
    }

    bool disableSsl() const { return m_disableSsl; }
    void setDisableSsl(bool _disableSsl) { m_disableSsl = _disableSsl; }

private:
    std::string m_listenIP;
    uint16_t m_listenPort;
    bool m_disableSsl;

    HttpReqHandler m_httpReqHandler;
    WsUpgradeHandler m_wsUpgradeHandler;

    std::shared_ptr<boost::asio::ip::tcp::acceptor> m_acceptor;
    std::shared_ptr<boost::asio::io_context> m_ioc;
    std::shared_ptr<boost::asio::ssl::context> m_ctx;

    std::shared_ptr<HttpStreamFactory> m_httpStreamFactory;
};

// The http server factory
class HttpServerFactory : public std::enable_shared_from_this<HttpServerFactory>
{
public:
    using Ptr = std::shared_ptr<HttpServerFactory>;

public:
    /**
     * @brief: create http server
     * @param _listenIP: listen ip
     * @param _listenPort: listen port
     * @param _ioc: io_context
     * @param _ctx: ssl context
     * @return HttpServer::Ptr:
     */
    HttpServer::Ptr buildHttpServer(const std::string& _listenIP, uint16_t _listenPort,
        std::shared_ptr<boost::asio::io_context> _ioc,
        std::shared_ptr<boost::asio::ssl::context> _ctx);
};

}  // namespace http
}  // namespace boostssl
}  // namespace bcos