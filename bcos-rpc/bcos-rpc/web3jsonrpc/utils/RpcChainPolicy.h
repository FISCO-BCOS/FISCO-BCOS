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
 * @file RpcChainPolicy.h
 * @brief The RPC fee/gas "lane", keyed on the chain's executor_version (the canonical lane
 *        selector): the Ethereum/OP lanes share geth's fee semantics, the legacy FISCO lane
 *        keeps the historic behaviour.
 */
#pragma once

#include <bcos-framework/ledger/LedgerConfig.h>
#include <cstdint>

namespace bcos::rpc
{
/// op-geth's --gpo.minsuggestedpriorityfee floor (1e6 wei).
inline constexpr uint64_t c_minSuggestedPriorityFeeWei = 1'000'000;

/// True when the chain runs the Ethereum executor (>= ETHEREUM_EXECUTOR_VERSION): fee/gas
/// semantics follow geth (eth_gasPrice = head.baseFee + tip, never below the base fee).
/// This is the lane predicate the endpoints consume; OP mode itself is a fixed genesis
/// value (== OPSTACK_EXECUTOR_VERSION), decided at chain creation and never re-derived.
inline bool usesEthereumFeeSemantics(int executorVersion)
{
    return executorVersion >= bcos::ledger::ETHEREUM_EXECUTOR_VERSION;
}

/// True when the chain runs the OP lane (exactly OPSTACK_EXECUTOR_VERSION). This is a LANE
/// predicate, distinct from the ledger's feature_l2_ethereum_compat state shape: the Eth
/// lane may carry that flag too (an MPT-state chain still sealed by the consensus layer),
/// and on such a chain the OP base-fee rule must not apply. eth_feeHistory consumes this,
/// so feeHistory and eth_gasPrice agree on every configuration.
inline bool isOpStackLane(int executorVersion)
{
    return executorVersion == bcos::ledger::OPSTACK_EXECUTOR_VERSION;
}

/// Suggested priority fee (wei): the Ethereum/OP lanes suggest a non-zero tip (OP floors at
/// 1e6 wei, matching op-geth); the legacy FISCO lane keeps its historic constant 0.
inline uint64_t suggestedPriorityFeeWei(int executorVersion)
{
    return usesEthereumFeeSemantics(executorVersion) ? c_minSuggestedPriorityFeeWei : 0;
}
}  // namespace bcos::rpc
