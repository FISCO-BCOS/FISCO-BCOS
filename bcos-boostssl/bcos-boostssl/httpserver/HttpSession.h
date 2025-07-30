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

namespace bcos::boostssl::http
{
// The http session for connection
class HttpSession : public std::enable_shared_from_this<HttpSession>
{
public:
    using Ptr = std::shared_ptr<HttpSession>;
    using ConstPtr = std::shared_ptr<const HttpSession>;

    HttpSession(uint32_t _httpBodySizeLimit, CorsConfig _corsConfig);
    HttpSession(const HttpSession&) = delete;
    HttpSession(HttpSession&&) = delete;
    HttpSession& operator=(const HttpSession&) = delete;
    HttpSession& operator=(HttpSession&&) = delete;

    virtual ~HttpSession();

    // start the HttpSession
    void run();

    void read();
    void onRead(boost::beast::error_code ec, std::size_t bytes_transferred);
    void onWrite(bool closeOnEOF, boost::beast::error_code ec,
        [[maybe_unused]] std::size_t bytes_transferred);
    void close();

    /**
     * @brief: handle http request and send the response
     * @param req: http request object
     * @return void:
     */
    void handleRequest(const HttpRequest& _httpRequest);

    /**
     * @brief: build http response object
     * @param status: http response status
     * @param content: http response content
     * @param corsConfig: cors config
     * @return HttpResponsePtr:
     */
    HttpResponsePtr buildHttpResp(boost::beast::http::status status, bool keepAlive,
        unsigned version, bcos::bytes content, const CorsConfig& corsConfig) const;

    HttpReqHandler httpReqHandler() const;
    void setRequestHandler(HttpReqHandler _httpReqHandler);

    WsUpgradeHandler wsUpgradeHandler() const;
    void setWsUpgradeHandler(WsUpgradeHandler _wsUpgradeHandler);
    Queue& queue();
    void setQueue(Queue _queue);

    HttpStream::Ptr httpStream();
    void setHttpStream(HttpStream::Ptr _httpStream);

    std::shared_ptr<std::string> nodeId();
    void setNodeId(std::shared_ptr<std::string> _nodeId);

    uint32_t httpBodySizeLimit() const { return m_httpBodySizeLimit; }
    void setHttpBodySizeLimit(uint32_t _httpBodySizeLimit)
    {
        m_httpBodySizeLimit = _httpBodySizeLimit;
    }
    CorsConfig corsConfig() const { return m_corsConfig; }
    void setCorsConfig(CorsConfig _corsConfig) { m_corsConfig = std::move(_corsConfig); }

private:
    HttpStream::Ptr m_httpStream;
    boost::beast::flat_buffer m_buffer;

    Queue m_queue;
    HttpReqHandler m_httpReqHandler;
    WsUpgradeHandler m_wsUpgradeHandler;
    // the parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    std::optional<boost::beast::http::request_parser<boost::beast::http::string_body>> m_parser;
    std::shared_ptr<std::string> m_nodeId;

    uint32_t m_httpBodySizeLimit;
    CorsConfig m_corsConfig;
};

}  // namespace bcos::boostssl::http
