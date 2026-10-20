/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file Connect.cpp
 */
#include "Connect.h"
#include "IpcClient.h"
#include "Mode.h"
#include "NodeDir.h"
#include "OpsError.h"
#include "RpcClient.h"
#include "collect/RpcCollector.h"
#include <filesystem>
#include <vector>

namespace bcos::ops
{
void retainTransport(std::shared_ptr<void> _transport)
{
    static auto* retained = new std::vector<std::shared_ptr<void>>();  // never freed on purpose
    retained->push_back(std::move(_transport));
}

ConnectOptions connectOptionsFrom(Args const& _args)
{
    ConnectOptions options;
    options.nodeDir = _args.optionOr("node-dir", ".");
    options.rpc = _args.option("rpc");
    options.connectTimeoutMs = _args.numberOr<int>("connect-timeout", 3000);
    options.requestTimeoutMs = _args.numberOr<int>("timeout", 15000);
    options.mode = _args.option("mode");
    options.web3 = _args.option("web3");
    return options;
}

std::pair<std::string, uint16_t> parseHostPort(std::string const& _endpoint)
{
    auto value = _endpoint;
    if (value.starts_with("ws://"))
    {
        value = value.substr(5);
    }
    else if (value.starts_with("http://"))
    {
        value = value.substr(7);
    }
    if (value.ends_with('/'))
    {
        value.pop_back();  // http://host:8545/
    }
    auto colon = value.rfind(':');
    if (colon == std::string::npos || colon + 1 >= value.size())
    {
        throw OpsError(c_exitUsage, "--rpc expects host:port, got: " + _endpoint);
    }
    auto host = value.substr(0, colon);
    if (host.size() > 2 && host.front() == '[' && host.back() == ']')
    {
        host = host.substr(1, host.size() - 2);
    }
    int port = 0;
    try
    {
        auto portText = value.substr(colon + 1);
        size_t used = 0;
        port = std::stoi(portText, &used);
        if (used != portText.size())
        {
            throw std::invalid_argument(portText);  // "20200abc" is not a port
        }
    }
    catch (std::exception const&)
    {
        throw OpsError(c_exitUsage, "--rpc expects host:port, got: " + _endpoint);
    }
    if (port <= 0 || port > 65535)
    {
        throw OpsError(c_exitUsage, "--rpc port out of range: " + _endpoint);
    }
    return {host, static_cast<uint16_t>(port)};
}

std::string connectionMode(ConnectOptions const& _options, NodeDir const* _node)
{
    if (_options.mode)
    {
        return checkedMode(*_options.mode, "--mode");
    }
    if (_node != nullptr)
    {
        return _node->mode;
    }
    return std::string(c_modeUnknown);
}

namespace
{
/// mode and web3 endpoint (connectionMode), and the request timeout every later call honours
Connection withMode(Connection _connection, ConnectOptions const& _options, NodeDir const* _node)
{
    _connection.requestTimeoutMs = _options.requestTimeoutMs;
    _connection.mode = connectionMode(_options, _node);
    if (_options.web3)
    {
        auto [host, port] = parseHostPort(*_options.web3);
        _connection.web3Endpoint = host + ":" + std::to_string(port);
    }
    else if (_node != nullptr && _node->web3Enable)
    {
        _connection.web3Endpoint = _node->web3Endpoint();
    }
    return _connection;
}
}  // namespace

Connection connect(ConnectOptions const& _options)
{
    if (_options.rpc)
    {
        auto [host, port] = parseHostPort(*_options.rpc);
        auto connection =
            makeWsRpcCall(host, port, _options.connectTimeoutMs, _options.requestTimeoutMs);
        retainTransport(connection.keepAlive);
        connection.group = discoverGroup(connection.call);
        return withMode(std::move(connection), _options, nullptr);
    }
    auto node = NodeDir::load(_options.nodeDir);
    // the local socket first: zero configuration and it carries admin_*; fall back to the
    // node's own RPC port when the socket is absent (node down, old node, ipc_enable=false)
    // one connect attempt, reused for the command: a separate reachability probe would leave an
    // aborted connection in the node's accept queue on every invocation
    if (_options.allowIpc && std::filesystem::exists(node.ipcPath()))
    {
        try
        {
            auto connection = makeIpcRpcCall(
                node.ipcPath(), _options.connectTimeoutMs, _options.requestTimeoutMs);
            connection.group = node.groupId;
            connection.nodeDir = node;
            return withMode(std::move(connection), _options, &node);
        }
        catch (OpsError const&)
        {
            // stale file or a node that is shutting down: fall back to RPC below
        }
    }
    if (!_options.allowRpc)
    {
        throw OpsError(c_exitUsage, "local socket " + node.ipcPath() +
                                        " is not available; is the node running with "
                                        "[rpc] ipc_enable=true?");
    }
    if (node.tls())
    {
        throw OpsError(c_exitUsage, "RPC port " + node.rpcEndpoint() +
                                        " has SSL enabled ([rpc] disable_ssl=false); this tool "
                                        "only supports plaintext RPC or the local socket");
    }
    auto connection = makeWsRpcCall(
        node.rpcHost(), node.rpcListenPort, _options.connectTimeoutMs, _options.requestTimeoutMs);
    retainTransport(connection.keepAlive);
    connection.group = node.groupId;
    connection.nodeDir = node;
    return withMode(std::move(connection), _options, &node);
}
}  // namespace bcos::ops
