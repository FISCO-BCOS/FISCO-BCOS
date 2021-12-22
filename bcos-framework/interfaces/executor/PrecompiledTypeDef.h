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
#include "../../libutilities/Common.h"

namespace bcos
{
namespace precompiled
{
/// precompiled contract path for wasm
const char SYS_CONFIG_NAME[] = "/sys/status";
const char TABLE_NAME[] = "/sys/table_storage";
const char CONSENSUS_NAME[] = "/sys/consensus";
const char CNS_NAME[] = "/sys/cns";
const char CONTRACT_AUTH_NAME[] = "/sys/auth";
const char PARALLEL_CONFIG_NAME[] = "/sys/parallel_config";
const char CONTRACT_LIFECYCLE_NAME[] = "/sys/contract_mgr";
const char KV_TABLE_NAME[] = "/sys/kv_storage";
const char CRYPTO_NAME[] = "/sys/crypto_tools";
const char DAG_TRANSFER_NAME[] = "/sys/dag_test";
const char BFS_NAME[] = "/sys/bfs";

/// precompiled contract for solidity
const char SYS_CONFIG_ADDRESS[] = "0000000000000000000000000000000000001000";
const char TABLE_ADDRESS[] = "0000000000000000000000000000000000001001";
const char CONSENSUS_ADDRESS[] = "0000000000000000000000000000000000001003";
const char CNS_ADDRESS[] = "0000000000000000000000000000000000001004";
const char CONTRACT_AUTH_ADDRESS[] = "0000000000000000000000000000000000001005";
const char PARALLEL_CONFIG_ADDRESS[] = "0000000000000000000000000000000000001006";
const char CONTRACT_LIFECYCLE_ADDRESS[] = "0000000000000000000000000000000000001007";
const char KV_TABLE_ADDRESS[] = "0000000000000000000000000000000000001009";
const char CRYPTO_ADDRESS[] = "000000000000000000000000000000000000100a";
const char WORKING_SEALER_MGR_ADDRESS[] = "000000000000000000000000000000000000100b";
const char DAG_TRANSFER_ADDRESS[] = "000000000000000000000000000000000000100c";
const char BFS_ADDRESS[] = "000000000000000000000000000000000000100e";

/// auth system contract for solidity
const char AUTH_INTERCEPT_ADDRESS[] = "0000000000000000000000000000000000010000";
const char AUTH_COMMITTEE_ADDRESS[] = "0000000000000000000000000000000000010001";
}  // namespace precompiled
}  // namespace bcos
