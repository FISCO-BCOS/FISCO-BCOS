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
 * @brief: precompiled contract addresses and ABI encoding helpers
 * @file: PrecompiledHelper.h
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace bcos::console
{

// Precompiled system contract addresses (hex, without 0x).
namespace precompiled
{

// ---- Fixed Addresses ----
inline constexpr std::string_view SYS_CONFIG     = "0000000000000000000000000000000000001000";
inline constexpr std::string_view TABLE_MANAGER  = "0000000000000000000000000000000000001002";
inline constexpr std::string_view CONSENSUS      = "0000000000000000000000000000000000001003";
inline constexpr std::string_view CONTRACT_AUTH  = "0000000000000000000000000000000000001005";
inline constexpr std::string_view BFS            = "000000000000000000000000000000000000100e";
inline constexpr std::string_view CAST           = "000000000000000000000000000000000000100f";
inline constexpr std::string_view SHARDING       = "0000000000000000000000000000000000001010";
inline constexpr std::string_view BALANCE        = "0000000000000000000000000000000000001011";
inline constexpr std::string_view COMMITTEE_MGR  = "0000000000000000000000000000000000010001";
inline constexpr std::string_view ACCOUNT_MGR    = "0000000000000000000000000000000000010003";

// ---- ABI Function Signatures ----

// SystemConfig (0x...1000)
inline constexpr std::string_view SYS_SET_VALUE  = "setValueByKey(string,string)";
inline constexpr std::string_view SYS_GET_VALUE  = "getValueByKey(string)";

// Consensus (0x...1003)
inline constexpr std::string_view CS_ADD_SEALER   = "addSealer(string,uint256)";
inline constexpr std::string_view CS_ADD_OBSERVER = "addObserver(string)";
inline constexpr std::string_view CS_REMOVE_NODE  = "remove(string)";
inline constexpr std::string_view CS_SET_WEIGHT   = "setWeight(string,uint256)";

// BFS (0x...100e)
inline constexpr std::string_view BFS_LIST_DIR    = "list(string)";
inline constexpr std::string_view BFS_MKDIR       = "mkdir(string)";
inline constexpr std::string_view BFS_LINK        = "link(string,string,string)";
inline constexpr std::string_view BFS_READLINK    = "readlink(string)";
inline constexpr std::string_view BFS_TOUCH       = "touch(string,string)";

// TableManager / CRUD (0x...1002)
inline constexpr std::string_view TM_CREATE_TABLE = "createTable(string,string,string)";
inline constexpr std::string_view TM_DESC_TABLE   = "desc(string)";

// Balance (0x...1011)
inline constexpr std::string_view BAL_GET_BALANCE = "getBalance(string)";
inline constexpr std::string_view BAL_TRANSFER    = "transfer(string,string,uint256)";

// Committee (0x...10001)
inline constexpr std::string_view CM_GET_INFO     = "getCommitteeInfo()";

// Sharding (0x...1010)
inline constexpr std::string_view SH_GET_SHARD    = "getContractShard(string)";
inline constexpr std::string_view SH_MAKE_SHARD   = "makeShard(string)";
inline constexpr std::string_view SH_LINK_SHARD   = "linkShard(string,string)";

}  // namespace precompiled

// Simple utility: convert precompiled address to the "0x..." form used by RPC.
inline std::string prefixedAddress(std::string_view addr)
{
    return std::string("0x") + std::string(addr);
}

}  // namespace bcos::console
