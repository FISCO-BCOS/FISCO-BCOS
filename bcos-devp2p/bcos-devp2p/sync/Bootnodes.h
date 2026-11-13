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

#include "bcos-devp2p/disc/Enr.h"
#include "bcos-devp2p/rlpx/Client.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
#include <json/json.h>
#include <boost/throw_exception.hpp>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace bcos::devp2p::sync
{

/// A bootstrap node as written in bootnodes.json: the RLPx (tcp) and discovery (udp)
/// ports plus the 64-byte public key.
struct BootnodeUri
{
    std::string host;
    uint16_t tcpPort{30303};
    uint16_t udpPort{30303};
    bcos::bytes publicKey;
};

/// Parse a geth-style enode URI.
/// Format: enode://<64-byte-hex-pubkey>@<ip>:<tcp-port>[?discport=<udp-port>]
/// Without ?discport the discovery port equals the TCP port (geth enode semantics);
/// `?discport=0` marks a TCP-only bootnode (geth --nodiscover) that discovery skips.
/// Throws std::invalid_argument on malformed input.
inline BootnodeUri parseEnodeUri(std::string const& enode)
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
        BOOST_THROW_EXCEPTION(std::invalid_argument(
            "parseEnode: missing '@' (expected enode://<pubkey>@<host>:<port>): " + enode));
    }
    auto pubHex = rest.substr(0, at);
    // geth enodes carry the 64-byte uncompressed secp256k1 public key
    // (128 hex chars).
    auto pubBytes = bcos::fromHex(std::string(pubHex));
    if (pubBytes.size() != 64)
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument(
            "parseEnode: public key must be 64 bytes (128 hex chars): " + enode));
    }
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

    uint16_t discPort = port;
    if (q != std::string_view::npos)
    {
        constexpr std::string_view discKey = "discport=";
        auto query = endpoint.substr(q + 1);
        auto at = query.find(discKey);
        if (at != std::string_view::npos)
        {
            auto value = query.substr(at + discKey.size());
            value = value.substr(0, value.find('&'));
            try
            {
                // 0 is valid here (unlike the TCP port): geth writes `?discport=0` for a
                // --nodiscover node. Discovery drops a zero UDP port (Discv4Service::addSeen),
                // so such a bootnode is dialed over TCP only.
                auto parsed = std::stoul(std::string(value));
                if (parsed > 65535)
                {
                    BOOST_THROW_EXCEPTION(std::out_of_range("discport"));
                }
                discPort = static_cast<uint16_t>(parsed);
            }
            catch (std::exception const&)
            {
                BOOST_THROW_EXCEPTION(std::invalid_argument(
                    "parseEnode: invalid discport '" + std::string(value) + "' in: " + enode));
            }
        }
    }
    BootnodeUri uri;
    uri.host = std::string(host);
    uri.tcpPort = port;
    uri.udpPort = discPort;
    uri.publicKey = std::move(pubBytes);
    return uri;
}

/// Parse one bootnodes.json entry: an enode:// URI or an `enr:` record (geth's
/// params/bootnodes.go ships Sepolia's EF bootnodes as ENRs). An ENR must verify under
/// the v4 scheme and carry an IPv4 `ip`; `udp` defaults to 30303 and `tcp` to `udp`.
/// Throws std::invalid_argument on malformed input.
inline BootnodeUri parseBootnode(std::string const& text)
{
    if (text.rfind("enr:", 0) != 0)
    {
        return parseEnodeUri(text);
    }
    auto record = bcos::devp2p::disc::Enr::fromText(text);
    if (!record)
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument(
            "parseBootnode: invalid ENR (" + record.error().message + "): " + text));
    }
    if (!record->verify())
    {
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("parseBootnode: ENR signature does not verify: " + text));
    }
    auto ip = record->ip();
    auto key = record->publicKey();
    if (!ip || !key)
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument(
            "parseBootnode: ENR without an IPv4 `ip` entry cannot be dialed: " + text));
    }
    BootnodeUri uri;
    uri.host = record->ipString();
    uri.udpPort = record->udp().value_or(30303);
    uri.tcpPort = record->tcp().value_or(uri.udpPort);
    uri.publicKey = std::move(*key);
    return uri;
}

inline bcos::devp2p::rlpx::PeerConfig toPeerConfig(BootnodeUri const& uri)
{
    bcos::devp2p::rlpx::PeerConfig config;
    config.host = uri.host;
    config.port = uri.tcpPort;
    config.peerPublicKey = uri.publicKey;
    return config;
}

/// Parse a geth-style enode URI into a PeerConfig (the RLPx dial target; the discovery
/// port is dropped here — see parseEnodeUri for it).
inline bcos::devp2p::rlpx::PeerConfig parseEnode(std::string const& enode)
{
    return toPeerConfig(parseEnodeUri(enode));
}

/// Load a bootnodes.json file: either a JSON array of enode:// / enr: strings, or an
/// object {"bootnodes": [ ... ]}. Returns the parsed entries with both ports (empty
/// file/section yields an empty list; a missing file throws).
inline std::vector<BootnodeUri> loadBootnodeUris(std::string const& path)
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
        BOOST_THROW_EXCEPTION(std::invalid_argument("loadBootnodes: invalid JSON in " + path));
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
    std::vector<BootnodeUri> nodes;
    nodes.reserve(arr.size());
    for (auto const& entry : arr)
    {
        if (!entry.isString())
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("loadBootnodes: non-string entry in " + path));
        }
        nodes.push_back(parseBootnode(entry.asString()));
    }
    return nodes;
}

/// The RLPx dial list of a bootnodes.json file (see loadBootnodeUris).
inline std::vector<bcos::devp2p::rlpx::PeerConfig> loadBootnodes(std::string const& path)
{
    std::vector<bcos::devp2p::rlpx::PeerConfig> peers;
    for (auto const& uri : loadBootnodeUris(path))
    {
        peers.push_back(toPeerConfig(uri));
    }
    return peers;
}

}  // namespace bcos::devp2p::sync
