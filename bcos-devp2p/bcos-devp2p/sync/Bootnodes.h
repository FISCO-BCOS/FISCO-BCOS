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
 * @file Bootnodes.h
 * @brief Ethereum L1 EL-mode bootnode configuration: parse geth-style enode://
 *        URIs and load a bootnodes.json file (a JSON array of enode strings).
 * @date 2026/8/18
 */
#pragma once

#include "bcos-devp2p/rlpx/Client.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/throw_exception.hpp>
#include <fstream>
#include <json/json.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace bcos::devp2p::sync
{

/// Parse a geth-style enode URI into a PeerConfig.
/// Format: enode://<64-byte-hex-pubkey>@<ip>:<tcp-port>[?discport=<udp-port>]
/// The discovery port suffix is accepted and ignored (we only connect via TCP).
/// Throws std::invalid_argument on malformed input.
inline bcos::devp2p::rlpx::PeerConfig parseEnode(std::string const& enode)
{
    constexpr std::string_view prefix = "enode://";
    if (enode.rfind(prefix, 0) != 0)
    {
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("parseEnode: enode must start with 'enode://': " + enode));
    }
    auto rest = std::string_view(enode).substr(prefix.size());
    auto at = rest.find('@');
    if (at == std::string_view::npos)
    {
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("parseEnode: missing '@' (expected enode://<pubkey>@<host>:<port>): " +
                                  enode));
    }
    auto pubHex = rest.substr(0, at);
    // 64 hex chars = 32 bytes = 64 hex digits uncompressed X coordinate (secp256k1
    // uncompressed public key is 64 bytes = 128 hex chars; geth enodes carry the
    // 64-byte uncompressed key, i.e. 128 hex chars).
    auto pubBytes = bcos::fromHex(std::string(pubHex));
    auto endpoint = rest.substr(at + 1);
    auto q = endpoint.find('?');
    auto hostPort = endpoint.substr(0, q);
    auto colon = hostPort.rfind(':');
    if (colon == std::string_view::npos)
    {
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("parseEnode: missing ':port' in endpoint: " + enode));
    }
    auto host = hostPort.substr(0, colon);
    auto portStr = hostPort.substr(colon + 1);
    uint16_t port = 0;
    try
    {
        auto parsed = std::stoul(std::string(portStr));
        if (parsed == 0 || parsed > 65535)
        {
            BOOST_THROW_EXCEPTION(std::out_of_range("port"));
        }
        port = static_cast<uint16_t>(parsed);
    }
    catch (std::exception const&)
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument(
            "parseEnode: invalid port '" + std::string(portStr) + "' in: " + enode));
    }

    bcos::devp2p::rlpx::PeerConfig config;
    config.host = std::string(host);
    config.port = port;
    config.peerPublicKey = std::move(pubBytes);
    return config;
}

/// Load a bootnodes.json file: either a JSON array of enode strings, or an object
/// {"bootnodes": [ ... ]}. Returns the parsed PeerConfig list (empty file/section
/// yields an empty list; a missing file throws).
inline std::vector<bcos::devp2p::rlpx::PeerConfig> loadBootnodes(std::string const& path)
{
    std::ifstream in(path);
    if (!in)
    {
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("loadBootnodes: cannot open bootnodes file: " + path));
    }
    std::stringstream ss;
    ss << in.rdbuf();
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(ss.str(), root))
    {
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("loadBootnodes: invalid JSON in " + path));
    }
    Json::Value arr;
    if (root.isArray())
    {
        arr = root;
    }
    else if (root.isObject() && root.isMember("bootnodes") && root["bootnodes"].isArray())
    {
        arr = root["bootnodes"];
    }
    else
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument(
            "loadBootnodes: expected a JSON array of enode strings, or {\"bootnodes\": [...]}: " +
            path));
    }
    std::vector<bcos::devp2p::rlpx::PeerConfig> nodes;
    nodes.reserve(arr.size());
    for (auto const& entry : arr)
    {
        if (!entry.isString())
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("loadBootnodes: non-string entry in " + path));
        }
        nodes.push_back(parseEnode(entry.asString()));
    }
    return nodes;
}

}  // namespace bcos::devp2p::sync
