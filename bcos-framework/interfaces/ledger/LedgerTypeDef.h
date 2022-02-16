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
 * @file LedgerTypeDef.h
 * @author: kyonRay
 * @date 2021-04-30
 */

#pragma once
#include "../protocol/ProtocolTypeDef.h"
#include "bcos-utilities/Common.h"

namespace bcos::ledger
{
using MerkleProof = std::vector<std::pair<std::vector<std::string>, std::vector<std::string> > >;
using MerkleProofPtr = std::shared_ptr<const MerkleProof>;

// get block flag
static const int32_t FULL_BLOCK = 0xFFFF;
static const int32_t HEADER = 0x0008;
static const int32_t TRANSACTIONS = 0x0004;
static const int32_t RECEIPTS = 0x0002;

// get system config key
static const char* const SYSTEM_KEY_TX_GAS_LIMIT = "tx_gas_limit";
static const char* const SYSTEM_KEY_TX_COUNT_LIMIT = "tx_count_limit";
static const char* const SYSTEM_KEY_CONSENSUS_LEADER_PERIOD = "consensus_leader_period";

// system config struct
using SystemConfigEntry = std::tuple<std::string, bcos::protocol::BlockNumber>;

const unsigned TX_GAS_LIMIT_MIN = 100000;
// get consensus node list type
static const char* const CONSENSUS_SEALER = "consensus_sealer";
static const char* const CONSENSUS_OBSERVER = "consensus_observer";
static const char* const CONSENSUS_WORKING_SEALER = "consensus_working_sealer";

// get current state key
static const char* const SYS_KEY_CURRENT_NUMBER = "current_number";
static const char* const SYS_KEY_TOTAL_TRANSACTION_COUNT = "total_transaction_count";
static const char* const SYS_KEY_TOTAL_FAILED_TRANSACTION = "total_failed_transaction_count";

// sys table name
static const char* const SYS_CONSENSUS = "s_consensus";
static const char* const SYS_CONFIG = "s_config";
static const char* const SYS_CURRENT_STATE = "s_current_state";
static const char* const SYS_HASH_2_NUMBER = "s_hash_2_number";
static const char* const SYS_NUMBER_2_HASH = "s_number_2_hash";
static const char* const SYS_BLOCK_NUMBER_2_NONCES = "s_block_number_2_nonces";
static const char* const SYS_NUMBER_2_BLOCK_HEADER = "s_number_2_header";
static const char* const SYS_NUMBER_2_TXS = "s_number_2_txs";
static const char* const SYS_HASH_2_TX = "s_hash_2_tx";
static const char* const SYS_HASH_2_RECEIPT = "s_hash_2_receipt";
static const char* const SYS_CNS = "s_cns";
static const char* const DAG_TRANSFER = "/tables/dag_transfer";
}  // namespace bcos
