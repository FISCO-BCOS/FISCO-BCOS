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
#include <bcos-utilities/Common.h>

namespace bcos
{
/// System contract
constexpr const int SYS_CONTRACT_DEPLOY_NUMBER = 0;
inline bool isSysContractDeploy(protocol::BlockNumber _number)
{
    return _number == SYS_CONTRACT_DEPLOY_NUMBER;
}

namespace precompiled
{
/// precompiled contract path for wasm
constexpr const char* const SYS_CONFIG_NAME = "/sys/status";
constexpr const char* const TABLE_NAME = "/sys/table_storage";
constexpr const char* const TABLE_MANAGER_NAME = "/sys/table_manager";
constexpr const char* const CONSENSUS_NAME = "/sys/consensus";
constexpr const char* const AUTH_MANAGER_NAME = "/sys/auth";
constexpr const char* const KV_TABLE_NAME = "/sys/kv_storage";
constexpr const char* const CRYPTO_NAME = "/sys/crypto_tools";
constexpr const char* const DAG_TRANSFER_NAME = "/sys/dag_test";
constexpr const char* const BFS_NAME = "/sys/bfs";
constexpr const char* const GROUP_SIG_NAME = "/sys/group_sig";
constexpr const char* const RING_SIG_NAME = "/sys/ring_sig";
constexpr const char* const DISCRETE_ZKP_NAME = "/sys/discrete_zkp";
constexpr const char* const ACCOUNT_MANAGER_NAME = "/sys/account_manager";
constexpr static const uint8_t BFS_SYS_SUBS_COUNT = 13;
constexpr static const std::array<std::string_view, BFS_SYS_SUBS_COUNT> BFS_SYS_SUBS = {
    SYS_CONFIG_NAME, TABLE_NAME, TABLE_MANAGER_NAME, CONSENSUS_NAME, AUTH_MANAGER_NAME,
    KV_TABLE_NAME, CRYPTO_NAME, DAG_TRANSFER_NAME, BFS_NAME, GROUP_SIG_NAME, RING_SIG_NAME,
    DISCRETE_ZKP_NAME, ACCOUNT_MANAGER_NAME};

/// precompiled contract for solidity
/// precompiled address should range in [0x1000, 0x20000)
constexpr const char* const SYS_CONFIG_ADDRESS = "0000000000000000000000000000000000001000";
constexpr const char* const TABLE_ADDRESS = "0000000000000000000000000000000000001001";
constexpr const char* const TABLE_MANAGER_ADDRESS = "0000000000000000000000000000000000001002";
constexpr const char* const CONSENSUS_ADDRESS = "0000000000000000000000000000000000001003";
constexpr const char* const AUTH_MANAGER_ADDRESS = "0000000000000000000000000000000000001005";
constexpr const char* const KV_TABLE_ADDRESS = "0000000000000000000000000000000000001009";
constexpr const char* const CRYPTO_ADDRESS = "000000000000000000000000000000000000100a";
constexpr const char* const WORKING_SEALER_MGR_ADDRESS = "000000000000000000000000000000000000100b";
constexpr const char* const DAG_TRANSFER_ADDRESS = "000000000000000000000000000000000000100c";
constexpr const char* const BFS_ADDRESS = "000000000000000000000000000000000000100e";
constexpr const char* const SYS_ADDRESS_PREFIX = "00000000000000000000000000000000000";

// Contract address related to privacy computing
constexpr const char* const GROUP_SIG_ADDRESS = "0000000000000000000000000000000000005004";
constexpr const char* const RING_SIG_ADDRESS = "0000000000000000000000000000000000005005";
// for zkp
constexpr const char* const DISCRETE_ZKP_ADDRESS = "0000000000000000000000000000000000005100";


/// auth system contract for solidity
constexpr const char* const AUTH_INTERCEPT_ADDRESS = "0000000000000000000000000000000000010000";
constexpr const char* const AUTH_COMMITTEE_ADDRESS = "0000000000000000000000000000000000010001";
constexpr const char* const AUTH_CONTRACT_MGR_ADDRESS = "0000000000000000000000000000000000010002";
constexpr const char* const ACCOUNT_MGR_ADDRESS = "0000000000000000000000000000000000010003";
constexpr const char* const ACCOUNT_ADDRESS = "0000000000000000000000000000000000010004";

// clang-format off
/// name to address, only for create sys table
constexpr static const std::array<std::pair<std::string_view,std::string_view>, BFS_SYS_SUBS_COUNT>
    SYS_NAME_ADDRESS_MAP ={
    std::pair{SYS_CONFIG_NAME, SYS_CONFIG_ADDRESS},
    {TABLE_NAME, TABLE_ADDRESS},
    {TABLE_MANAGER_NAME, TABLE_MANAGER_ADDRESS},
    {CONSENSUS_NAME, CONSENSUS_ADDRESS},
    {AUTH_MANAGER_NAME, AUTH_MANAGER_ADDRESS},
    {KV_TABLE_NAME, KV_TABLE_ADDRESS},
    {CRYPTO_NAME, CRYPTO_ADDRESS},
    {DAG_TRANSFER_NAME, DAG_TRANSFER_ADDRESS},
    {BFS_NAME, BFS_ADDRESS},
    {GROUP_SIG_NAME, GROUP_SIG_ADDRESS},
    {RING_SIG_NAME, RING_SIG_ADDRESS},
    {DISCRETE_ZKP_NAME, DISCRETE_ZKP_ADDRESS},
    {ACCOUNT_MANAGER_NAME, ACCOUNT_MGR_ADDRESS}
};
// clang-format on

const std::set<std::string> c_systemTxsAddress = {bcos::precompiled::SYS_CONFIG_ADDRESS,
    bcos::precompiled::CONSENSUS_ADDRESS, bcos::precompiled::WORKING_SEALER_MGR_ADDRESS,
    bcos::precompiled::SYS_CONFIG_NAME, bcos::precompiled::CONSENSUS_NAME,
    bcos::precompiled::AUTH_COMMITTEE_ADDRESS, bcos::precompiled::AUTH_MANAGER_ADDRESS,
    bcos::precompiled::ACCOUNT_ADDRESS, bcos::precompiled::ACCOUNT_MGR_ADDRESS,
    bcos::precompiled::ACCOUNT_MANAGER_NAME};

/// for testing
// CpuHeavy test: 0x5200 ~ (0x5200 + 128)
const char* const CPU_HEAVY_START_ADDRESS = "0x5200";
const int CPU_HEAVY_CONTRACT_NUM = 128;

// Smallbank test: 0x6200 ~ (0x6200 + 128)
const char* const SMALLBANK_START_ADDRESS = "0x6200";
const int SMALLBANK_CONTRACT_NUM = 128;

}  // namespace precompiled
}  // namespace bcos
