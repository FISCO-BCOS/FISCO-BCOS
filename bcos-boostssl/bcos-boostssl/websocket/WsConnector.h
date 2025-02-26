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
 * @file WsConnector.h
 * @author: octopus
 * @date 2021-08-23
 */
#pragma once
#include <bcos-boostssl/websocket/WsStream.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket.hpp>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

namespace bcos::boostssl::ws
{
class WsConnector : public std::enable_shared_from_this<WsConnector>
{
public:
    using Ptr = std::shared_ptr<WsConnector>;
    using ConstPtr = std::shared_ptr<const WsConnector>;

    explicit WsConnector(std::shared_ptr<boost::asio::ip::tcp::resolver> _resolver)
      : m_resolver(std::move(_resolver))
    {}

    /**
     * @brief: connect to the server
     * @param _host: the remote server host, support ipv4, ipv6, domain name
     * @param _port: the remote server port
     * @param _disableSsl: disable ssl
     * @param _callback:
     * @return void:
     */
    void connectToWsServer(const std::string& _host, uint16_t _port, bool _disableSsl,
        std::function<void(boost::beast::error_code, const std::string& extErrorMsg,
            std::shared_ptr<WsStreamDelegate>, std::shared_ptr<std::string>)>
            _callback);

    bool erasePendingConns(const std::string& _nodeIPEndpoint)
    {
        std::lock_guard<std::mutex> lock(x_pendingConns);
        return m_pendingConns.erase(_nodeIPEndpoint) != 0U;
    }

    bool insertPendingConns(const std::string& _nodeIPEndpoint)
    {
        std::lock_guard<std::mutex> lock(x_pendingConns);
        auto result = m_pendingConns.insert(_nodeIPEndpoint);
        return result.second;
    }

    void setResolver(std::shared_ptr<boost::asio::ip::tcp::resolver> _resolver)
    {
        m_resolver = std::move(_resolver);
    }
    std::shared_ptr<boost::asio::ip::tcp::resolver> resolver() const { return m_resolver; }

    void setIOServicePool(IOServicePool::Ptr _ioservicePool)
    {
        m_ioservicePool = std::move(_ioservicePool);
    }

    void setCtx(std::shared_ptr<boost::asio::ssl::context> _ctx) { m_ctx = std::move(_ctx); }
    std::shared_ptr<boost::asio::ssl::context> ctx() const { return m_ctx; }

    void setBuilder(std::shared_ptr<WsStreamDelegateBuilder> _builder)
    {
        m_builder = std::move(_builder);
    }
    std::shared_ptr<WsStreamDelegateBuilder> builder() const { return m_builder; }

private:
    std::shared_ptr<WsStreamDelegateBuilder> m_builder;
    std::shared_ptr<boost::asio::ip::tcp::resolver> m_resolver;
    std::shared_ptr<boost::asio::ssl::context> m_ctx;

    mutable std::mutex x_pendingConns;
    std::set<std::string> m_pendingConns;
    IOServicePool::Ptr m_ioservicePool;
};
}  // namespace bcos::boostssl::ws
