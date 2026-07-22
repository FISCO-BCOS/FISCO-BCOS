/*
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
 * @brief Top-level message execution over eth::state::EthHost.
 * @file ExecuteMessage.h
 */

#pragma once

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-evm/eth/state/VmHostPolicy.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.hpp>
#include <vector>

namespace bcos::evm
{
struct ChainCallTargetPort;
using LogEntry = state::LogEntry;

struct ExecuteMessageInput
{
    /// Mutable execution journal; pipeline passes &TxPipelineContext::state (ADR-019 Q14).
    state::State* state{nullptr};
    evmc::VM* vm{nullptr};
    evmc_message message{};
    bcos::u256 gasPrice{0};
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    bcos::evm_standard::RevisionConfig revisionConfig{};
    state::TransactionProperties txProps{};
    const Eip2930AccessList* accessList{nullptr};
    bool authorizationListPresent{false};
    std::vector<SetCodeAuthorization> authorizations;
    uint8_t web3TypedTxKind{0};
    state::VmHostPolicy* extension{nullptr};
    ChainCallTargetPort* chainPort{nullptr};
    bool fixStorageStatus{true};
    bool fixNonceInit{false};
    /// When true, orchestration (e.g. OpStack deposit finalizeDeposit) owns sender nonce bump.
    bool skipTopLevelSenderNonceBump{false};
    std::optional<bcos::h256> txHash;
};

struct ExecuteMessageOutput
{
    evmc::Result result{evmc_result{}};
    state::StateDiff stateDiff;
    std::vector<LogEntry> logs;
    int64_t gasRefund{0};
};

ExecuteMessageOutput executeMessage(ExecuteMessageInput input);

}  // namespace bcos::evm
