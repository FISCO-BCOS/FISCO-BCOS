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
 * @file PrecompiledTypeDef.h
 * @author: kyonRay
 * @date 2021-06-22
 */

#pragma once
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include <bcos-utilities/Common.h>
#include <charconv>

namespace bcos
{
/// System contract
constexpr const int SYS_CONTRACT_DEPLOY_NUMBER = 0;
constexpr inline bool isSysContractDeploy(protocol::BlockNumber _number)
{
    return _number == SYS_CONTRACT_DEPLOY_NUMBER;
}

constexpr unsigned int PRECOMPILED_ADDRESS_UPPER_BOUND = 0x20000;
inline bool isPrecompiledAddressRange(std::string_view _address)
{
    unsigned int address;
    auto result = std::from_chars(_address.data(), _address.data() + _address.size(), address, 16);
    return result.ec == std::errc() && address < PRECOMPILED_ADDRESS_UPPER_BOUND;
}

constexpr auto toSortedList(auto input)
{
    std::sort(input.begin(), input.end());
    return input;
}

namespace precompiled
{
/// precompiled contract path for wasm
constexpr static std::string_view SYS_CONFIG_NAME = "/sys/status";
constexpr static std::string_view TABLE_NAME = "/sys/table_storage";
constexpr static std::string_view TABLE_MANAGER_NAME = "/sys/table_manager";
constexpr static std::string_view CONSENSUS_TABLE_NAME = "/sys/consensus";
constexpr static std::string_view AUTH_MANAGER_NAME = "/sys/auth";
constexpr static std::string_view KV_TABLE_NAME = "/sys/kv_storage";
constexpr static std::string_view CRYPTO_NAME = "/sys/crypto_tools";
constexpr static std::string_view DAG_TRANSFER_NAME = "/sys/dag_test";
constexpr static std::string_view BFS_NAME = "/sys/bfs";
constexpr static std::string_view PAILLIER_SIG_NAME = "/sys/paillier";
constexpr static std::string_view GROUP_SIG_NAME = "/sys/group_sig";
constexpr static std::string_view RING_SIG_NAME = "/sys/ring_sig";
constexpr static std::string_view DISCRETE_ZKP_NAME = "/sys/discrete_zkp";
constexpr static std::string_view ACCOUNT_MANAGER_NAME = "/sys/account_manager";
constexpr static std::string_view CAST_NAME = "/sys/cast_tools";
constexpr static std::string_view SHARDING_PRECOMPILED_NAME = "/sys/sharding";
constexpr static std::string_view BALANCE_PRECOMPILED_NAME = "/sys/balance";
constexpr static uint8_t BFS_SYS_SUBS_COUNT = 15;
constexpr static auto BFS_SYS_SUBS = std::to_array<std::string_view>(
    {SYS_CONFIG_NAME, TABLE_NAME, TABLE_MANAGER_NAME, CONSENSUS_TABLE_NAME, AUTH_MANAGER_NAME,
        KV_TABLE_NAME, CRYPTO_NAME, DAG_TRANSFER_NAME, BFS_NAME, GROUP_SIG_NAME, RING_SIG_NAME,
        DISCRETE_ZKP_NAME, ACCOUNT_MANAGER_NAME, CAST_NAME, SHARDING_PRECOMPILED_NAME});

/// only for init v3.0.0 /sys/ in ledger, should never change it
constexpr static auto BFS_SYS_SUBS_V30 =
    std::to_array<std::string_view>({SYS_CONFIG_NAME, TABLE_NAME, TABLE_MANAGER_NAME,
        CONSENSUS_TABLE_NAME, AUTH_MANAGER_NAME, KV_TABLE_NAME, CRYPTO_NAME, DAG_TRANSFER_NAME,
        BFS_NAME, GROUP_SIG_NAME, RING_SIG_NAME, DISCRETE_ZKP_NAME, ACCOUNT_MANAGER_NAME});

/// precompiled contract for solidity
/// precompiled address should range in [0x1000, 0x20000)
constexpr static std::string_view SYS_CONFIG_ADDRESS = "0000000000000000000000000000000000001000";
constexpr static std::string_view TABLE_ADDRESS = "0000000000000000000000000000000000001001";
constexpr static std::string_view TABLE_MANAGER_ADDRESS =
    "0000000000000000000000000000000000001002";
constexpr static std::string_view CONSENSUS_ADDRESS = "0000000000000000000000000000000000001003";
constexpr static std::string_view AUTH_MANAGER_ADDRESS = "0000000000000000000000000000000000001005";
constexpr static std::string_view KV_TABLE_ADDRESS = "0000000000000000000000000000000000001009";
constexpr static std::string_view CRYPTO_ADDRESS = "000000000000000000000000000000000000100a";
constexpr static std::string_view WORKING_SEALER_MGR_ADDRESS =
    "000000000000000000000000000000000000100b";
constexpr static std::string_view DAG_TRANSFER_ADDRESS = "000000000000000000000000000000000000100c";
constexpr static std::string_view BFS_ADDRESS = "000000000000000000000000000000000000100e";
constexpr static std::string_view CAST_ADDRESS = "000000000000000000000000000000000000100f";
constexpr static std::string_view SHARDING_PRECOMPILED_ADDRESS =
    "0000000000000000000000000000000000001010";
constexpr static std::string_view BALANCE_PRECOMPILED_ADDRESS =
    "0000000000000000000000000000000000001011";
constexpr std::string_view SYS_ADDRESS_PREFIX = "00000000000000000000000000000000000";
constexpr std::string_view EVM_PRECOMPILED_PREFIX = "000000000000000000000000000000000000000";
constexpr std::string_view EMPTY_ADDRESS = "0000000000000000000000000000000000000000";
// Contract address related to privacy computing
constexpr static std::string_view PAILLIER_ADDRESS = "0000000000000000000000000000000000005003";
constexpr static std::string_view GROUP_SIG_ADDRESS = "0000000000000000000000000000000000005004";
constexpr static std::string_view RING_SIG_ADDRESS = "0000000000000000000000000000000000005005";
// for zkpstatic std::string_view
constexpr static std::string_view DISCRETE_ZKP_ADDRESS = "0000000000000000000000000000000000005100";
/// auth system contract for solidity
constexpr static std::string_view AUTH_INTERCEPT_ADDRESS =
    "0000000000000000000000000000000000010000";
constexpr static std::string_view AUTH_COMMITTEE_ADDRESS =
    "0000000000000000000000000000000000010001";
constexpr static std::string_view AUTH_CONTRACT_MGR_ADDRESS =
    "0000000000000000000000000000000000010002";
constexpr static std::string_view ACCOUNT_MGR_ADDRESS = "0000000000000000000000000000000000010003";
constexpr static std::string_view ACCOUNT_ADDRESS = "0000000000000000000000000000000000010004";

// clang-format off
/// name to address, only for create sys table
constexpr static auto SYS_NAME_ADDRESS_MAP = std::to_array<std::pair<std::string_view,std::string_view>>({
    std::pair{SYS_CONFIG_NAME, SYS_CONFIG_ADDRESS},
    {TABLE_NAME, TABLE_ADDRESS},
    {TABLE_MANAGER_NAME, TABLE_MANAGER_ADDRESS},
    {CONSENSUS_TABLE_NAME, CONSENSUS_ADDRESS},
    {AUTH_MANAGER_NAME, AUTH_MANAGER_ADDRESS},
    {KV_TABLE_NAME, KV_TABLE_ADDRESS},
    {CRYPTO_NAME, CRYPTO_ADDRESS},
    {DAG_TRANSFER_NAME, DAG_TRANSFER_ADDRESS},
    {BFS_NAME, BFS_ADDRESS},
    {GROUP_SIG_NAME, GROUP_SIG_ADDRESS},
    {RING_SIG_NAME, RING_SIG_ADDRESS},
    {DISCRETE_ZKP_NAME, DISCRETE_ZKP_ADDRESS},
    {ACCOUNT_MANAGER_NAME, ACCOUNT_MGR_ADDRESS},
    {CAST_NAME, CAST_ADDRESS},
    {SHARDING_PRECOMPILED_NAME, SHARDING_PRECOMPILED_ADDRESS}
});
// clang-format on

constexpr static auto c_systemTxsAddress =
    toSortedList(std::to_array<std::string_view>({bcos::precompiled::SYS_CONFIG_ADDRESS,
        bcos::precompiled::CONSENSUS_ADDRESS, bcos::precompiled::WORKING_SEALER_MGR_ADDRESS,
        bcos::precompiled::SYS_CONFIG_NAME, bcos::precompiled::CONSENSUS_TABLE_NAME,
        bcos::precompiled::AUTH_COMMITTEE_ADDRESS, bcos::precompiled::AUTH_MANAGER_ADDRESS,
        bcos::precompiled::ACCOUNT_ADDRESS, bcos::precompiled::ACCOUNT_MGR_ADDRESS,
        bcos::precompiled::ACCOUNT_MANAGER_NAME, bcos::precompiled::SHARDING_PRECOMPILED_ADDRESS}));

constexpr static struct Contains
{
    template <class Arg>
    bool operator()(::ranges::input_range auto const& args, const Arg& arg) const
        requires std::same_as<std::decay_t<::ranges::range_value_t<decltype(args)>>,
            std::decay_t<Arg>>
    {
        return ::ranges::binary_search(args, arg);
    }
} contains;

/// for testing
// CpuHeavy test: 0x5200 ~ (0x5200 + 128)
const char* const CPU_HEAVY_START_ADDRESS = "0x5200";
const int CPU_HEAVY_CONTRACT_NUM = 128;

// Smallbank test: 0x6200 ~ (0x6200 + 128)
const char* const SMALLBANK_START_ADDRESS = "0x6200";
const int SMALLBANK_CONTRACT_NUM = 128;

constexpr const char* const WSM_METHOD_ROTATE_STR =
    "rotateWorkingSealer(std::string,std::string,std::string)";

}  // namespace precompiled
}  // namespace bcos
