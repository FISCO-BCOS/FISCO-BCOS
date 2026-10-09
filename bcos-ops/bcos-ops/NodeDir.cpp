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
 * @file NodeDir.cpp
 */
#include "NodeDir.h"
#include "OpsError.h"
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <filesystem>

namespace bcos::ops
{
namespace
{
std::string joinPath(std::string const& _dir, std::string const& _rel)
{
    std::filesystem::path path(_rel);
    if (path.is_absolute())
    {
        return path.string();
    }
    return (std::filesystem::path(_dir) / path).lexically_normal().string();
}
}  // namespace

NodeDir NodeDir::load(std::string const& _dir)
{
    NodeDir node;
    node.dir = _dir.empty() ? "." : _dir;
    auto iniPath = joinPath(node.dir, "config.ini");
    if (!std::filesystem::exists(iniPath))
    {
        throw OpsError(c_exitUsage, "config.ini not found in node dir: " + node.dir);
    }
    boost::property_tree::ptree ini;
    try
    {
        boost::property_tree::read_ini(iniPath, ini);
    }
    catch (std::exception const& e)
    {
        throw OpsError(c_exitUsage, std::string("cannot read ") + iniPath + ": " + e.what());
    }
    boost::property_tree::ptree genesis;
    auto genesisPath = joinPath(node.dir, "config.genesis");
    if (std::filesystem::exists(genesisPath))
    {
        try
        {
            boost::property_tree::read_ini(genesisPath, genesis);
        }
        catch (std::exception const&)
        {
            // chain section falls back to config.ini below
        }
    }
    // [chain] lives in config.genesis since 3.1; older nodes keep it in config.ini
    auto const& chainSource = genesis.get_child_optional("chain") ? genesis : ini;
    node.groupId = chainSource.get<std::string>("chain.group_id", "group");
    node.chainId = chainSource.get<std::string>("chain.chain_id", "chain");
    node.smCrypto = chainSource.get<bool>("chain.sm_crypto", false);
    node.authCheck = genesis.get<bool>("executor.is_auth_check", false);

    node.rpcListenIp = ini.get<std::string>("rpc.listen_ip", "0.0.0.0");
    node.rpcListenPort = static_cast<uint16_t>(ini.get<int>("rpc.listen_port", 20200));
    node.rpcDisableSsl = ini.get<bool>("rpc.disable_ssl", false);
    node.storagePath = ini.get<std::string>("storage.data_path", "data/" + node.groupId);
    node.logPath = ini.get<std::string>("log.log_path", "log");
    node.logFormat = ini.get<std::string>("log.format", "");
    node.txpoolLimit = ini.get<size_t>("txpool.limit", 15000);
    return node;
}

std::string NodeDir::rpcHost() const
{
    if (rpcListenIp == "0.0.0.0" || rpcListenIp == "::" || rpcListenIp.empty())
    {
        return "127.0.0.1";
    }
    return rpcListenIp;
}

std::string NodeDir::rpcEndpoint() const
{
    return rpcHost() + ":" + std::to_string(rpcListenPort);
}

std::string NodeDir::ipcPath() const
{
    return joinPath(dir, storagePath) + "/fisco-bcos.ipc";
}

std::string NodeDir::logDir() const
{
    return joinPath(dir, logPath);
}
}  // namespace bcos::ops
