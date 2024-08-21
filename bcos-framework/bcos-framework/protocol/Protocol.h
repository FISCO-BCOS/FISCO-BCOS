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
 * @brief Protocol for all the modules
 * @file Protocol.h
 * @author: yujiechen
 * @date 2021-04-21
 */
#pragma once
#include "bcos-utilities/BoostLog.h"
#include <fmt/compile.h>
#include <fmt/format.h>
#include <boost/algorithm/string.hpp>
#include <boost/throw_exception.hpp>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <variant>

namespace bcos::protocol
{
// Note: both MessageExtFieldFlag and NodeType occupy the ext fields
enum MessageExtFieldFlag : uint32_t
{
    NONE = 0,
    RESPONSE = 1,
    CONSENSUS_NODE = 1 << 1,
    OBSERVER_NODE = 1 << 2,
    FREE_NODE = 1 << 3,
    COMPRESS = 1 << 4,
    LIGHT_NODE = 1 << 5,
};
using NodeType = MessageExtFieldFlag;

enum NodeArchitectureType
{
    AIR = 0,
    PRO = 1,
    MAX = 2,
    LIGHT
};

enum MessageType
{
    HANDESHAKE = 0x100,         // 256
    BLOCK_NOTIFY = 0x101,       // 257
    RPC_REQUEST = 0x102,        // 258
    GROUP_NOTIFY = 0x103,       // 259
    EVENT_SUBSCRIBE = 0x120,    // 288
    EVENT_UNSUBSCRIBE = 0x121,  // 289
    EVENT_LOG_PUSH = 0x122,     // 290
};

// TODO: Allow add new module, exchange moduleid or version
enum ModuleID
{
    PBFT = 1000,
    Raft = 1001,

    BlockSync = 2000,
    TxsSync = 2001,
    ConsTxsSync = 2002,

    AMOP = 3000,

    LIGHTNODE_GET_BLOCK = 4000,
    LIGHTNODE_GET_TRANSACTIONS = 4001,
    LIGHTNODE_GET_RECEIPTS = 4002,
    LIGHTNODE_GET_STATUS = 4003,
    LIGHTNODE_SEND_TRANSACTION = 4004,
    LIGHTNODE_CALL = 4005,
    LIGHTNODE_GET_ABI = 4006,
    LIGHTNODE_END = 4999,

    SYNC_PUSH_TRANSACTION = 5000,
    SYNC_GET_TRANSACTIONS = 5001,
    SYNC_END = 5999,

    TREE_PUSH_TRANSACTION = 6000,
};

enum ProtocolModuleID : uint32_t
{
    NodeService = 0x0,
    GatewayService = 0x1,
    RpcService = 0x2,
    ExecutorService = 0x3,
    MAX_PROTOCOL_MODULE = ExecutorService,
};
enum ProtocolVersion : uint32_t
{
    V0 = 0,
    V1 = 1,
    V2 = 2,
};

// BlockVersion only present the data version with format major.minor.patch of 3 bytes, data should
// be compatible with the same major.minor version, the patch version should always be compatible,
// the last byte is reserved, so 3.1.0 is 0x03010000 and is compatible with 3.1.1 which is
// 0x03010100

enum class BlockVersion : uint32_t
{
    V3_11_0_VERSION = 0x030b0000,
    V3_10_0_VERSION = 0x030a0000,
    V3_9_0_VERSION = 0x03090000,
    V3_8_0_VERSION = 0x03080000,
    V3_7_3_VERSION = 0x03070300,
    V3_7_2_VERSION = 0x03070200,
    V3_7_1_VERSION = 0x03070100,
    V3_7_0_VERSION = 0x03070000,
    V3_6_1_VERSION = 0x03060100,
    V3_6_VERSION = 0x03060000,
    V3_5_VERSION = 0x03050000,
    V3_4_VERSION = 0x03040000,
    V3_3_VERSION = 0x03030000,
    V3_2_7_VERSION = 0x03020700,
    V3_2_6_VERSION = 0x03020600,
    V3_2_5_VERSION = 0x03020500,
    V3_2_4_VERSION = 0x03020400,
    V3_2_3_VERSION = 0x03020300,
    V3_2_2_VERSION = 0x03020200,
    V3_2_VERSION = 0x03020000,
    V3_1_VERSION = 0x03010000,
    V3_0_VERSION = 0x03000000,
    RC4_VERSION = 4,
    MIN_VERSION = RC4_VERSION,
    MAX_VERSION = V3_11_0_VERSION,
};

enum class TransactionVersion : uint32_t
{
    V0_VERSION = 0x00000000,
    V1_VERSION = 0x00000001,
    V2_VERSION = 0x00000002,

};

const std::string RC4_VERSION_STR = "3.0.0-rc4";
const std::string RC_VERSION_PREFIX = "3.0.0-rc";
const std::string V3_9_VERSION_STR = "3.9.0";

const BlockVersion DEFAULT_VERSION = bcos::protocol::BlockVersion::V3_11_0_VERSION;
const std::string DEFAULT_VERSION_STR = V3_9_VERSION_STR;
const uint8_t MAX_MAJOR_VERSION = std::numeric_limits<uint8_t>::max();
const uint8_t MIN_MAJOR_VERSION = 3;

[[nodiscard]] inline int versionCompareTo(
    std::variant<uint32_t, BlockVersion> const& _v1, BlockVersion const& _v2)
{
    int flag = 0;
    std::visit(
        [&_v2, &flag](auto&& arg) {
            auto ver1 = static_cast<uint32_t>(arg);
            auto ver2 = static_cast<uint32_t>(_v2);
            flag = ver1 > ver2 ? 1 : -1;
            flag = (ver1 == ver2) ? 0 : flag;
        },
        _v1);
    return flag;
}

constexpr auto operator<=>(BlockVersion lhs, auto rhs)
    requires std::same_as<decltype(rhs), BlockVersion> || std::same_as<decltype(rhs), uint32_t>
{
    return static_cast<uint32_t>(lhs) <=> static_cast<uint32_t>(rhs);
}

inline std::ostream& operator<<(std::ostream& out, bcos::protocol::BlockVersion version)
{
    if (version == bcos::protocol::BlockVersion::RC4_VERSION)
    {
        out << RC4_VERSION_STR;
        return out;
    }

    auto versionNumber = static_cast<uint32_t>(version);
    auto num1 = (versionNumber >> 24) & (0xff);
    auto num2 = (versionNumber >> 16) & (0xff);
    auto num3 = (versionNumber >> 8) & (0xff);

    out << fmt::format(FMT_COMPILE("{}.{}.{}"), num1, num2, num3);
    return out;
}


inline std::ostream& operator<<(std::ostream& _out, NodeType const& _nodeType)
{
    switch (_nodeType)
    {
    case NodeType::NONE:
        _out << "None";
        break;
    case NodeType::CONSENSUS_NODE:
        _out << "CONSENSUS_NODE";
        break;
    case NodeType::OBSERVER_NODE:
        _out << "OBSERVER_NODE";
        break;
    case NodeType::LIGHT_NODE:
        _out << "LIGHT_NODE";
        break;
    case NodeType::FREE_NODE:
        _out << "NODE_OUTSIDE_GROUP";
        break;
    default:
        _out << "Unknown";
        break;
    }
    return _out;
}

inline std::optional<ModuleID> stringToModuleID(const std::string& _moduleName)
{
    if (boost::iequals(_moduleName, "raft"))
    {
        return bcos::protocol::ModuleID::Raft;
    }
    else if (boost::iequals(_moduleName, "pbft"))
    {
        return bcos::protocol::ModuleID::PBFT;
    }
    else if (boost::iequals(_moduleName, "amop"))
    {
        return bcos::protocol::ModuleID::AMOP;
    }
    else if (boost::iequals(_moduleName, "block_sync"))
    {
        return bcos::protocol::ModuleID::BlockSync;
    }
    else if (boost::iequals(_moduleName, "txs_sync"))
    {
        return bcos::protocol::ModuleID::TxsSync;
    }
    else if (boost::iequals(_moduleName, "cons_txs_sync"))
    {
        return bcos::protocol::ModuleID::ConsTxsSync;
    }
    else if (boost::iequals(_moduleName, "light_node"))
    {
        return bcos::protocol::ModuleID::LIGHTNODE_GET_BLOCK;
    }
    else
    {
        return std::nullopt;
    }
}

inline std::string moduleIDToString(ModuleID _moduleID)
{
    switch (_moduleID)
    {
    case ModuleID::PBFT:
        return "pbft";
    case ModuleID::Raft:
        return "raft";
    case ModuleID::BlockSync:
        return "block_sync";
    case ModuleID::TxsSync:
        return "txs_sync";
    case ModuleID::ConsTxsSync:
        return "cons_txs_sync";
    case ModuleID::AMOP:
        return "amop";
    case ModuleID::LIGHTNODE_GET_BLOCK:
    case ModuleID::LIGHTNODE_GET_TRANSACTIONS:
    case ModuleID::LIGHTNODE_GET_RECEIPTS:
    case ModuleID::LIGHTNODE_GET_STATUS:
    case ModuleID::LIGHTNODE_SEND_TRANSACTION:
    case ModuleID::LIGHTNODE_CALL:
    case ModuleID::LIGHTNODE_GET_ABI:
        return "light_node";
    case ModuleID::SYNC_GET_TRANSACTIONS:
        return "sync_get";
    case ModuleID::SYNC_PUSH_TRANSACTION:
        return "sync_push";
    case ModuleID::TREE_PUSH_TRANSACTION:
        return "tree_push";
    default:
        BCOS_LOG(DEBUG) << LOG_BADGE("unrecognized module") << LOG_KV("moduleID", _moduleID);
        return "unrecognized module";
    };
}

}  // namespace bcos::protocol
