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
#include <bcos-utilities/IOServicePool.h>
#include <utility>
namespace bcos::boostssl::http
{
// The http server impl
class HttpServer : public std::enable_shared_from_this<HttpServer>
{
public:
    using Ptr = std::shared_ptr<HttpServer>;

    HttpServer(std::string _listenIP, uint16_t _listenPort, uint32_t _httpBodySizeLimit,
        CorsConfig _corsConfig)
      : m_listenIP(std::move(_listenIP)),
        m_listenPort(_listenPort),
        m_httpBodySizeLimit(_httpBodySizeLimit),
        m_corsConfig(std::move(_corsConfig))
    {}

    ~HttpServer() { stop(); }

    // start http server
    void start();
    void stop();

    // accept connection
    void accept();
    // handle connection
    void onAccept(boost::beast::error_code ec, boost::asio::ip::tcp::socket socket);

    HttpSession::Ptr buildHttpSession(
        HttpStream::Ptr _stream, std::shared_ptr<std::string> _nodeId);

    HttpReqHandler httpReqHandler() const { return m_httpReqHandler; }
    void setHttpReqHandler(HttpReqHandler _httpReqHandler)
    {
        m_httpReqHandler = std::move(_httpReqHandler);
    }

    std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor() const { return m_acceptor; }
    void setAcceptor(std::shared_ptr<boost::asio::ip::tcp::acceptor> _acceptor)
    {
        m_acceptor = std::move(_acceptor);
    }

    std::shared_ptr<boost::asio::ssl::context> ctx() const { return m_ctx; }
    void setCtx(std::shared_ptr<boost::asio::ssl::context> _ctx) { m_ctx = std::move(_ctx); }

    WsUpgradeHandler wsUpgradeHandler() const { return m_wsUpgradeHandler; }
    void setWsUpgradeHandler(WsUpgradeHandler _wsUpgradeHandler)
    {
        m_wsUpgradeHandler = std::move(_wsUpgradeHandler);
    }

    HttpStreamFactory::Ptr httpStreamFactory() const { return m_httpStreamFactory; }
    void setHttpStreamFactory(HttpStreamFactory::Ptr _httpStreamFactory)
    {
        m_httpStreamFactory = std::move(_httpStreamFactory);
    }

    bool disableSsl() const { return m_disableSsl; }
    void setDisableSsl(bool _disableSsl) { m_disableSsl = _disableSsl; }

    void setIOServicePool(bcos::IOServicePool::Ptr _ioservicePool)
    {
        m_ioservicePool = std::move(_ioservicePool);
    }

    uint32_t httpBodySizeLimit() const { return m_httpBodySizeLimit; }
    void setHttpBodySizeLimit(uint32_t _httpBodySizeLimit)
    {
        m_httpBodySizeLimit = _httpBodySizeLimit;
    }

    CorsConfig corsConfig() const { return m_corsConfig; }
    void setCorsConfig(CorsConfig _corsConfig) { m_corsConfig = std::move(_corsConfig); }

private:
    std::string m_listenIP;
    uint16_t m_listenPort;
    bool m_disableSsl = false;

    HttpReqHandler m_httpReqHandler;
    WsUpgradeHandler m_wsUpgradeHandler;

    std::shared_ptr<boost::asio::ip::tcp::acceptor> m_acceptor;
    std::shared_ptr<boost::asio::ssl::context> m_ctx;

    std::shared_ptr<HttpStreamFactory> m_httpStreamFactory;
    bcos::IOServicePool::Ptr m_ioservicePool;

    uint32_t m_httpBodySizeLimit;
    // cors config
    CorsConfig m_corsConfig;
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
     * @param _httpBodySizeLimit: http body size limit
     * @param _corsConfig: cors config
     * @return HttpServer::Ptr:
     */
    HttpServer::Ptr buildHttpServer(const std::string& _listenIP, uint16_t _listenPort,
        std::shared_ptr<boost::asio::io_context> _ioc,
        std::shared_ptr<boost::asio::ssl::context> _ctx, uint32_t _httpBodySizeLimit = -1,
        CorsConfig _corsConfig = CorsConfig());
};

}  // namespace bcos::boostssl::http
