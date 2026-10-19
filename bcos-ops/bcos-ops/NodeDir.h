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
 * @brief what the ops commands need from a node directory: the RPC endpoint, whether it is TLS,
 *        the data path (socket lives there), the log directory and format, the txpool limit.
 *        Reads config.ini / config.genesis with the same keys and defaults NodeConfig uses, but
 *        only those keys, so a directory without certificates or peers still loads.
 * @file NodeDir.h
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace bcos::ops
{
struct NodeDir
{
    std::string dir;
    std::string rpcListenIp;
    uint16_t rpcListenPort = 20200;
    bool rpcDisableSsl = false;
    std::string groupId;
    std::string chainId;
    bool smCrypto = false;
    std::string storagePath;
    std::string logPath;
    std::string logFormat;  // empty == default format
    size_t txpoolLimit = 15000;
    bool authCheck = false;
    std::optional<int64_t> consensusTimeoutMs;  // max(3000, [consensus] min_seal_time + 1)
    // config.ini [ethereum] mode (NodeConfig::loadEthereumConfig): none / el / opstack-el
    std::string ethereumMode = "none";
    // config.ini [web3_rpc] (NodeConfig::loadWeb3RpcConfig defaults)
    bool web3Enable = false;
    std::string web3ListenIp = "127.0.0.1";
    uint16_t web3ListenPort = 8545;

    /// throws OpsError{1} when <dir>/config.ini is missing or unreadable
    static NodeDir load(std::string const& _dir);

    /// host:port to connect to; listen_ip 0.0.0.0 / :: becomes 127.0.0.1
    std::string rpcHost() const;
    std::string rpcEndpoint() const;
    bool tls() const { return !rpcDisableSsl; }
    /// <storagePath>/fisco-bcos.ipc (absolute if dir is)
    std::string ipcPath() const;
    std::string logDir() const;
    /// host:port of the web3 listener (0.0.0.0 / :: become 127.0.0.1)
    std::string web3Endpoint() const;
};
}  // namespace bcos::ops
