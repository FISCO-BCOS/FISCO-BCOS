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
#include "NodeDir.h"
#include "OpsError.h"
#include "RpcClient.h"
#include "collect/RpcCollector.h"
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
        port = std::stoi(value.substr(colon + 1));
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

Connection connect(ConnectOptions const& _options)
{
    if (_options.rpc)
    {
        auto [host, port] = parseHostPort(*_options.rpc);
        auto connection =
            makeWsRpcCall(host, port, _options.connectTimeoutMs, _options.requestTimeoutMs);
        retainTransport(connection.keepAlive);
        connection.group = discoverGroup(connection.call);
        return connection;
    }
    auto node = NodeDir::load(_options.nodeDir);
    // the local socket (PR-03) is tried first when present; fall back to the node's RPC
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
    return connection;
}
}  // namespace bcos::ops
