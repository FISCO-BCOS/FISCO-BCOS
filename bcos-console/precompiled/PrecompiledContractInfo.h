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
 * @brief: Precompiled contract ABI registry — addresses and ABI JSON for all
 *         FISCO BCOS system precompiled contracts.
 * @file: PrecompiledContractInfo.h
 */

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace bcos::console::precompiled
{

// Solidity address literals (hex, 40 chars, no 0x prefix)
namespace address
{
constexpr std::string_view SYSTEM_CONFIG = "0000000000000000000000000000000000001000";
constexpr std::string_view TABLE_MANAGER = "0000000000000000000000000000000000001002";
constexpr std::string_view CONSENSUS = "0000000000000000000000000000000000001003";
constexpr std::string_view CNS = "0000000000000000000000000000000000001004";
constexpr std::string_view CS = "0000000000000000000000000000000000001005";
constexpr std::string_view CONTRACT_LIFECYCLE = "0000000000000000000000000000000000001007";
constexpr std::string_view CHAIN_GOVERNANCE = "0000000000000000000000000000000000001008";
constexpr std::string_view BALANCE = "0000000000000000000000000000000000001009";
constexpr std::string_view BFS = "0000000000000000000000000000000000001010";
constexpr std::string_view CONTRACT_AUTH = "0000000000000000000000000000000000001005";
constexpr std::string_view COMMITTEE_MANAGER = "0000000000000000000000000000000000010001";
constexpr std::string_view PROPOSAL_MANAGER = "00000000000000000000000000000000000010000";
}  // namespace address

// Registered precompiled contract types
enum class PrecompiledType : uint8_t
{
    Consensus,
    SystemConfig,
    BFS,
    Balance,
    CRUD,
    ContractLifecycle,
    ChainGovernance,
    ContractAuth,
    CommitteeManager,
    ProposalManager,
    COUNT
};

// Map PrecompiledType to address string_view
constexpr std::string_view addressOf(PrecompiledType t)
{
    switch (t)
    {
    case PrecompiledType::Consensus:
        return address::CONSENSUS;
    case PrecompiledType::SystemConfig:
        return address::SYSTEM_CONFIG;
    case PrecompiledType::BFS:
        return address::BFS;
    case PrecompiledType::Balance:
        return address::BALANCE;
    case PrecompiledType::CRUD:
        return address::TABLE_MANAGER;
    case PrecompiledType::ContractLifecycle:
        return address::CONTRACT_LIFECYCLE;
    case PrecompiledType::ChainGovernance:
        return address::CHAIN_GOVERNANCE;
    case PrecompiledType::ContractAuth:
        return address::CONTRACT_AUTH;
    case PrecompiledType::CommitteeManager:
        return address::COMMITTEE_MANAGER;
    case PrecompiledType::ProposalManager:
        return address::PROPOSAL_MANAGER;
    default:
        return "";
    }
}

// ============================================================================
// ABI JSON strings — copied from Java SDK precompiled contract definitions
// ============================================================================

// ConsensusPrecompiled: addSealer, addObserver, remove, setWeight
inline const char* CONSENSUS_ABI = R"JSON(
[
  {"constant":false,"inputs":[{"name":"","type":"string"}],"name":"addObserver","outputs":[{"name":"","type":"int32"}],"payable":false,"stateMutability":"nonpayable","type":"function"},
  {"constant":false,"inputs":[{"name":"","type":"string"},{"name":"","type":"uint256"}],"name":"addSealer","outputs":[{"name":"","type":"int32"}],"payable":false,"stateMutability":"nonpayable","type":"function"},
  {"constant":false,"inputs":[{"name":"","type":"string"}],"name":"remove","outputs":[{"name":"","type":"int32"}],"payable":false,"stateMutability":"nonpayable","type":"function"},
  {"constant":false,"inputs":[{"name":"","type":"string"},{"name":"","type":"uint256"}],"name":"setWeight","outputs":[{"name":"","type":"int32"}],"payable":false,"stateMutability":"nonpayable","type":"function"}
]
)JSON";

// SystemConfigPrecompiled: setValueByKey
inline const char* SYSTEM_CONFIG_ABI = R"JSON(
[
  {"constant":false,"inputs":[{"name":"","type":"string"},{"name":"","type":"string"}],"name":"setValueByKey","outputs":[{"name":"","type":"int256"}],"payable":false,"stateMutability":"nonpayable","type":"function"}
]
)JSON";

// BFSPrecompiled: mkdir, list, link, readlink, fixBfs
inline const char* BFS_ABI = R"JSON(
[
  {"inputs":[{"internalType":"uint256","name":"","type":"uint256"}],"name":"fixBfs","outputs":[{"internalType":"int32","name":"","type":"int32"}],"stateMutability":"nonpayable","type":"function"},
  {"inputs":[{"internalType":"string","name":"name","type":"string"},{"internalType":"string","name":"version","type":"string"},{"internalType":"string","name":"_address","type":"string"},{"internalType":"string","name":"_abi","type":"string"}],"name":"link","outputs":[{"internalType":"int32","name":"","type":"int32"}],"stateMutability":"nonpayable","type":"function"},
  {"inputs":[{"internalType":"string","name":"absolutePath","type":"string"},{"internalType":"string","name":"_address","type":"string"},{"internalType":"string","name":"_abi","type":"string"}],"name":"link","outputs":[{"internalType":"int256","name":"","type":"int256"}],"stateMutability":"nonpayable","type":"function"},
  {"inputs":[{"internalType":"string","name":"absolutePath","type":"string"},{"internalType":"uint256","name":"offset","type":"uint256"},{"internalType":"uint256","name":"limit","type":"uint256"}],"name":"list","outputs":[{"internalType":"int256","name":"","type":"int256"},{"components":[{"internalType":"string","name":"file_name","type":"string"},{"internalType":"string","name":"file_type","type":"string"},{"internalType":"string[]","name":"ext","type":"string[]"}],"internalType":"struct BfsInfo[]","name":"","type":"tuple[]"}],"stateMutability":"view","type":"function"},
  {"inputs":[{"internalType":"string","name":"absolutePath","type":"string"}],"name":"list","outputs":[{"internalType":"int32","name":"","type":"int32"},{"components":[{"internalType":"string","name":"file_name","type":"string"},{"internalType":"string","name":"file_type","type":"string"},{"internalType":"string[]","name":"ext","type":"string[]"}],"internalType":"struct BfsInfo[]","name":"","type":"tuple[]"}],"stateMutability":"view","type":"function"},
  {"inputs":[{"internalType":"string","name":"absolutePath","type":"string"}],"name":"mkdir","outputs":[{"internalType":"int32","name":"","type":"int32"}],"stateMutability":"nonpayable","type":"function"},
  {"inputs":[{"internalType":"string","name":"absolutePath","type":"string"}],"name":"readlink","outputs":[{"internalType":"address","name":"","type":"address"}],"stateMutability":"view","type":"function"}
]
)JSON";

// BalancePrecompiled: getBalance, transfer, addBalance, subBalance,
// registerCaller, unregisterCaller, listCaller
inline const char* BALANCE_ABI = R"JSON(
[
  {"inputs":[{"internalType":"address","name":"account","type":"address"},{"internalType":"uint256","name":"amount","type":"uint256"}],"name":"addBalance","outputs":[],"stateMutability":"nonpayable","type":"function"},
  {"inputs":[{"internalType":"address","name":"account","type":"address"}],"name":"getBalance","outputs":[{"internalType":"uint256","name":"","type":"uint256"}],"stateMutability":"view","type":"function"},
  {"inputs":[],"name":"listCaller","outputs":[{"internalType":"address[]","name":"","type":"address[]"}],"stateMutability":"view","type":"function"},
  {"inputs":[{"internalType":"address","name":"account","type":"address"}],"name":"registerCaller","outputs":[],"stateMutability":"nonpayable","type":"function"},
  {"inputs":[{"internalType":"address","name":"account","type":"address"},{"internalType":"uint256","name":"amount","type":"uint256"}],"name":"subBalance","outputs":[],"stateMutability":"nonpayable","type":"function"},
  {"inputs":[{"internalType":"address","name":"from","type":"address"},{"internalType":"address","name":"to","type":"address"},{"internalType":"uint256","name":"amount","type":"uint256"}],"name":"transfer","outputs":[],"stateMutability":"nonpayable","type":"function"},
  {"inputs":[{"internalType":"address","name":"account","type":"address"}],"name":"unregisterCaller","outputs":[],"stateMutability":"nonpayable","type":"function"}
]
)JSON";

// Returns the ABI JSON string for a given precompiled contract type
inline const char* abiOf(PrecompiledType t)
{
    switch (t)
    {
    case PrecompiledType::Consensus:
        return CONSENSUS_ABI;
    case PrecompiledType::SystemConfig:
        return SYSTEM_CONFIG_ABI;
    case PrecompiledType::BFS:
        return BFS_ABI;
    case PrecompiledType::Balance:
        return BALANCE_ABI;
    // TODO: add CRUD, ContractLifecycle, ChainGovernance, ContractAuth ABIs
    default:
        return "";
    }
}

}  // namespace bcos::console::precompiled
