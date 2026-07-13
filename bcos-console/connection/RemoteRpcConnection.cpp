/**
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
 *  limitations under the License.
 *
 * @brief: remote RPC via Boost Beast HTTP JSON-RPC client
 * @file: RemoteRpcConnection.cpp
 */

#include "RemoteRpcConnection.h"

#include <bcos-utilities/BoostLog.h>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <json/json.h>
#include <iostream>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

#define CONSOLE_REMOTE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[CONSOLE][REMOTE]"

using namespace bcos::console;

// ================ Constructor / destructor / connect ================

RemoteRpcConnection::RemoteRpcConnection(ConsoleConfig const& config)
  : m_config(config)
{}

RemoteRpcConnection::~RemoteRpcConnection()
{
    disconnect();
}

void RemoteRpcConnection::connect()
{
    if (m_connected.load()) return;
    if (m_config.peers.empty())
    {
        CONSOLE_REMOTE_LOG(WARNING) << "No peers configured";
        return;
    }

    auto& peer = m_config.peers[0];
    auto colonPos = peer.find(':');
    if (colonPos == std::string::npos)
    {
        CONSOLE_REMOTE_LOG(WARNING) << "Invalid peer: " << peer;
        return;
    }
    m_host = peer.substr(0, colonPos);
    m_port = peer.substr(colonPos + 1);

    try
    {
        tcp::resolver resolver(m_ioc);
        auto results = resolver.resolve(m_host, m_port);
        // Try each resolved endpoint until one succeeds (handles IPv4/IPv6 mismatch)
        beast::error_code ec;
        tcp::socket socket(m_ioc);
        net::connect(socket, results, ec);
        if (ec)
        {
            CONSOLE_REMOTE_LOG(WARNING) << "Connect failed: " << ec.message();
            m_connected.store(false);
            return;
        }
        socket.close();
        m_connected.store(true);
        CONSOLE_REMOTE_LOG(INFO) << "Connected to " << m_host << ":" << m_port;
    }
    catch (std::exception const& e)
    {
        CONSOLE_REMOTE_LOG(WARNING) << "Connect failed: " << e.what();
        m_connected.store(false);
    }
}

void RemoteRpcConnection::disconnect()
{
    m_connected.store(false);
}

// ================ Core JSON-RPC over HTTP ================

void RemoteRpcConnection::sendJsonRpc(
    std::string method, Json::Value params, RpcRespFunc respFunc)
{
    if (!m_connected.load())
    {
        Json::Value e;
        respFunc(BCOS_ERROR_PTR(-1, "Not connected"), e);
        return;
    }

    Json::Value reqBody;
    reqBody["jsonrpc"] = "2.0";
    reqBody["method"] = std::move(method);
    reqBody["params"] = std::move(params);
    reqBody["id"] = 1;

    Json::FastWriter writer;
    auto bodyStr = writer.write(reqBody);

    try
    {
        m_ioc.restart();
        tcp::resolver resolver(m_ioc);
        auto results = resolver.resolve(m_host, m_port);
        beast::tcp_stream stream(m_ioc);
        stream.connect(results);

        http::request<http::string_body> req{http::verb::post, "/", 11};
        req.set(http::field::host, m_host);
        req.set(http::field::content_type, "application/json");
        req.body() = bodyStr;
        req.prepare_payload();
        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(res.body(), root))
        {
            Json::Value e;
            respFunc(BCOS_ERROR_PTR(-1, "Invalid JSON response"), e);
            return;
        }

        if (root.isMember("error") && !root["error"].isNull())
        {
            auto jErr = root["error"];
            Json::Value e;
            respFunc(BCOS_ERROR_PTR(
                         jErr["code"].asInt64(), jErr["message"].asString()),
                e);
            return;
        }

        if (root.isMember("result"))
        {
            auto& result = root["result"];
            // If the result is a JSON string, parse it into a real object
            if (result.isString())
            {
                Json::Value parsed;
                Json::Reader r2;
                auto const& str = result.asString();
                if (r2.parse(str.data(), str.data() + str.size(), parsed))
                {
                    respFunc(nullptr, parsed);
                    return;
                }
            }
            respFunc(nullptr, result);
            return;
        }

        Json::Value e;
        respFunc(BCOS_ERROR_PTR(-1, "Unexpected: " + res.body()), e);
    }
    catch (std::exception const& ex)
    {
        Json::Value e;
        respFunc(BCOS_ERROR_PTR(-1,
                     std::string("RPC error: ") + ex.what()),
            e);
    }
}

// ================ RPC methods using callRpc template ================

void RemoteRpcConnection::getBlockNumber(
    std::string_view g, std::string_view n, RpcRespFunc f)
{
    callRpc("getBlockNumber", std::move(f), resolveGroup(g), resolveNode(n));
}

void RemoteRpcConnection::getPbftView(
    std::string_view g, std::string_view n, RpcRespFunc f)
{
    callRpc("getPbftView", std::move(f), resolveGroup(g), resolveNode(n));
}

void RemoteRpcConnection::getSealerList(
    std::string_view g, std::string_view n, RpcRespFunc f)
{
    callRpc("getSealerList", std::move(f), resolveGroup(g), resolveNode(n));
}

void RemoteRpcConnection::getObserverList(
    std::string_view g, std::string_view n, RpcRespFunc f)
{
    callRpc("getObserverList", std::move(f), resolveGroup(g), resolveNode(n));
}

void RemoteRpcConnection::getPendingTxSize(
    std::string_view g, std::string_view n, RpcRespFunc f)
{
    callRpc("getPendingTxSize", std::move(f), resolveGroup(g), resolveNode(n));
}

void RemoteRpcConnection::getSyncStatus(
    std::string_view g, std::string_view n, RpcRespFunc f)
{
    callRpc("getSyncStatus", std::move(f), resolveGroup(g), resolveNode(n));
}

void RemoteRpcConnection::getConsensusStatus(
    std::string_view g, std::string_view n, RpcRespFunc f)
{
    callRpc("getConsensusStatus", std::move(f), resolveGroup(g), resolveNode(n));
}

void RemoteRpcConnection::getTotalTransactionCount(
    std::string_view g, std::string_view n, RpcRespFunc f)
{
    callRpc("getTotalTransactionCount", std::move(f), resolveGroup(g), resolveNode(n));
}

void RemoteRpcConnection::getGroupNodeInfo(
    std::string_view g, std::string_view n, RpcRespFunc f)
{
    callRpc("getGroupNodeInfo", std::move(f), resolveGroup(g), resolveNode(n));
}

// ---- No group/node params ----

void RemoteRpcConnection::getPeers(RpcRespFunc f)
{
    callRpc("getPeers", std::move(f));
}

void RemoteRpcConnection::getGroupList(RpcRespFunc f)
{
    callRpc("getGroupList", std::move(f));
}

void RemoteRpcConnection::getGroupInfoList(RpcRespFunc f)
{
    callRpc("getGroupInfoList", std::move(f));
}

void RemoteRpcConnection::getGroupBlockNumber(RpcRespFunc f)
{
    callRpc("getGroupBlockNumber", std::move(f));
}

// ---- Group-only param ----

void RemoteRpcConnection::getGroupPeers(std::string_view g, RpcRespFunc f)
{
    callRpc("getGroupPeers", std::move(f), resolveGroup(g));
}

void RemoteRpcConnection::getGroupInfo(std::string_view g, RpcRespFunc f)
{
    callRpc("getGroupInfo", std::move(f), resolveGroup(g));
}

// ---- Block / tx query methods with extra params ----

void RemoteRpcConnection::getBlockByNumber(
    std::string_view g, std::string_view n, int64_t bn, bool hdr, bool txh,
    RpcRespFunc f)
{
    callRpc("getBlockByNumber", std::move(f),
        resolveGroup(g), resolveNode(n), bn, hdr, txh);
}

void RemoteRpcConnection::getBlockByHash(
    std::string_view g, std::string_view n, std::string_view h, bool hdr,
    bool txh, RpcRespFunc f)
{
    callRpc("getBlockByHash", std::move(f),
        resolveGroup(g), resolveNode(n), std::string(h), hdr, txh);
}

void RemoteRpcConnection::getBlockHashByNumber(
    std::string_view g, std::string_view n, int64_t bn, RpcRespFunc f)
{
    callRpc("getBlockHashByNumber", std::move(f),
        resolveGroup(g), resolveNode(n), bn);
}

void RemoteRpcConnection::getTransaction(
    std::string_view g, std::string_view n, std::string_view h, bool proof,
    RpcRespFunc f)
{
    callRpc("getTransaction", std::move(f),
        resolveGroup(g), resolveNode(n), std::string(h), proof);
}

void RemoteRpcConnection::getTransactionReceipt(
    std::string_view g, std::string_view n, std::string_view h, bool proof,
    RpcRespFunc f)
{
    callRpc("getTransactionReceipt", std::move(f),
        resolveGroup(g), resolveNode(n), std::string(h), proof);
}

void RemoteRpcConnection::getCode(
    std::string_view g, std::string_view n, std::string_view addr, RpcRespFunc f)
{
    callRpc("getCode", std::move(f),
        resolveGroup(g), resolveNode(n), std::string(addr));
}

void RemoteRpcConnection::getABI(
    std::string_view g, std::string_view n, std::string_view addr, RpcRespFunc f)
{
    callRpc("getABI", std::move(f),
        resolveGroup(g), resolveNode(n), std::string(addr));
}

void RemoteRpcConnection::getNodeListByType(
    std::string_view g, std::string_view n, std::string_view t, RpcRespFunc f)
{
    callRpc("getNodeListByType", std::move(f),
        resolveGroup(g), resolveNode(n), std::string(t));
}

void RemoteRpcConnection::getSystemConfigByKey(
    std::string_view g, std::string_view n, std::string_view k, RpcRespFunc f)
{
    callRpc("getSystemConfigByKey", std::move(f),
        resolveGroup(g), resolveNode(n), std::string(k));
}

// ---- Transaction / call methods ----

void RemoteRpcConnection::call(
    std::string_view g, std::string_view n, std::string_view to,
    std::string_view data, RpcRespFunc f)
{
    callRpc("call", std::move(f),
        resolveGroup(g), resolveNode(n), std::string(to), std::string(data));
}

void RemoteRpcConnection::call(
    std::string_view g, std::string_view n, std::string_view to,
    std::string_view data, std::string_view sign, RpcRespFunc f)
{
    callRpc("call", std::move(f),
        resolveGroup(g), resolveNode(n), std::string(to), std::string(data), std::string(sign));
}

void RemoteRpcConnection::sendTransaction(
    std::string_view g, std::string_view n, std::string_view data,
    bool proof, RpcRespFunc f)
{
    callRpc("sendTransaction", std::move(f),
        resolveGroup(g), resolveNode(n), std::string(data), proof);
}
