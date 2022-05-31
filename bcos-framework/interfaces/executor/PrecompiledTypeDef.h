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
namespace precompiled
{
/// precompiled contract path for wasm
const char* const SYS_CONFIG_NAME = "/sys/status";
const char* const TABLE_NAME = "/sys/table_storage";
const char* const TABLE_MANAGER_NAME = "/sys/table_manager";
const char* const CONSENSUS_NAME = "/sys/consensus";
const char* const AUTH_MANAGER_NAME = "/sys/auth";
const char* const KV_TABLE_NAME = "/sys/kv_storage";
const char* const CRYPTO_NAME = "/sys/crypto_tools";
const char* const DAG_TRANSFER_NAME = "/sys/dag_test";
const char* const BFS_NAME = "/sys/bfs";

/// precompiled contract for solidity
const char* const SYS_CONFIG_ADDRESS = "0000000000000000000000000000000000001000";
const char* const TABLE_ADDRESS = "0000000000000000000000000000000000001001";
const char* const TABLE_MANAGER_ADDRESS = "0000000000000000000000000000000000001002";
const char* const CONSENSUS_ADDRESS = "0000000000000000000000000000000000001003";
const char* const AUTH_MANAGER_ADDRESS = "0000000000000000000000000000000000001005";
const char* const KV_TABLE_ADDRESS = "0000000000000000000000000000000000001009";
const char* const CRYPTO_ADDRESS = "000000000000000000000000000000000000100a";
const char* const WORKING_SEALER_MGR_ADDRESS = "000000000000000000000000000000000000100b";
const char* const DAG_TRANSFER_ADDRESS = "000000000000000000000000000000000000100c";
const char* const BFS_ADDRESS = "000000000000000000000000000000000000100e";
const char* const SYS_ADDRESS_PREFIX = "00000000000000000000000000000000000";


/// auth system contract for solidity
const char* const AUTH_INTERCEPT_ADDRESS = "0000000000000000000000000000000000010000";
const char* const AUTH_COMMITTEE_ADDRESS = "0000000000000000000000000000000000010001";
const char* const AUTH_CONTRACT_MGR_ADDRESS = "0000000000000000000000000000000000010002";

const std::set<std::string> c_systemTxsAddress = {bcos::precompiled::SYS_CONFIG_ADDRESS,
    bcos::precompiled::CONSENSUS_ADDRESS, bcos::precompiled::WORKING_SEALER_MGR_ADDRESS,
    bcos::precompiled::SYS_CONFIG_NAME, bcos::precompiled::CONSENSUS_NAME,
    bcos::precompiled::AUTH_COMMITTEE_ADDRESS};

/// for testing
// CpuHeavy test: 0x5200 ~ (0x5200 + 128)
const char* const CPU_HEAVY_START_ADDRESS = "0x5200";
const int CPU_HEAVY_CONTRACT_NUM = 128;

// Smallbank test: 0x6200 ~ (0x6200 + 128)
const char* const SMALLBANK_START_ADDRESS = "0x6200";
const int SMALLBANK_CONTRACT_NUM = 128;

}  // namespace precompiled
}  // namespace bcos
