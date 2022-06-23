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
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket.hpp>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>

namespace bcos
{
namespace boostssl
{
namespace ws
{
class WsConnector : public std::enable_shared_from_this<WsConnector>
{
public:
    using Ptr = std::shared_ptr<WsConnector>;
    using ConstPtr = std::shared_ptr<const WsConnector>;

    WsConnector(std::shared_ptr<boost::asio::ip::tcp::resolver> _resolver,
        std::shared_ptr<boost::asio::io_context> _ioc)
      : m_resolver(_resolver), m_ioc(_ioc)
    {}

public:
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

public:
    bool erasePendingConns(const std::string& _nodeIPEndpoint)
    {
        std::lock_guard<std::mutex> l(x_pendingConns);
        return m_pendingConns.erase(_nodeIPEndpoint);
    }

    bool insertPendingConns(const std::string& _nodeIPEndpoint)
    {
        std::lock_guard<std::mutex> l(x_pendingConns);
        auto p = m_pendingConns.insert(_nodeIPEndpoint);
        return p.second;
    }

public:
    void setResolver(std::shared_ptr<boost::asio::ip::tcp::resolver> _resolver)
    {
        m_resolver = _resolver;
    }
    std::shared_ptr<boost::asio::ip::tcp::resolver> resolver() const { return m_resolver; }

    void setIoc(std::shared_ptr<boost::asio::io_context> _ioc) { m_ioc = _ioc; }
    std::shared_ptr<boost::asio::io_context> ioc() const { return m_ioc; }

    void setCtx(std::shared_ptr<boost::asio::ssl::context> _ctx) { m_ctx = _ctx; }
    std::shared_ptr<boost::asio::ssl::context> ctx() const { return m_ctx; }

    void setBuilder(std::shared_ptr<WsStreamDelegateBuilder> _builder) { m_builder = _builder; }
    std::shared_ptr<WsStreamDelegateBuilder> builder() const { return m_builder; }

    std::string moduleName() { return m_moduleName; }
    void setModuleName(std::string _moduleName) { m_moduleName = _moduleName; }

private:
    std::shared_ptr<WsStreamDelegateBuilder> m_builder;
    std::shared_ptr<boost::asio::ip::tcp::resolver> m_resolver;
    std::shared_ptr<boost::asio::io_context> m_ioc;
    std::shared_ptr<boost::asio::ssl::context> m_ctx;

    mutable std::mutex x_pendingConns;
    std::set<std::string> m_pendingConns;

    std::string m_moduleName = "DEFAULT";
};
}  // namespace ws
}  // namespace boostssl
}  // namespace bcos
