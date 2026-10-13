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
#include <bcos-framework/ledger/SystemConfigs.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <cstdint>

namespace bcos::rpc
{
/// op-geth's --gpo.minsuggestedpriorityfee floor (1e6 wei).
inline constexpr uint64_t c_minSuggestedPriorityFeeWei = 1'000'000;

/// True when the chain runs the Ethereum executor (>= ETHEREUM_EXECUTOR_VERSION): fee/gas
/// semantics follow geth (eth_gasPrice = head.baseFee + tip, never below the base fee).
/// This is the lane predicate the endpoints consume; OP mode itself is a fixed genesis
/// value (== OPSTACK_EXECUTOR_VERSION), decided at chain creation and never re-derived.
/// executor_version is the ONLY lane selector: the state shape (complete-trie MPT, /apps/
/// account tables) keys on the same value, so lane decisions never consult a feature flag.
inline bool usesEthereumFeeSemantics(int executorVersion)
{
    return executorVersion >= bcos::ledger::ETHEREUM_EXECUTOR_VERSION;
}

/// True on the OP lane (executor_version >= OPSTACK_EXECUTOR_VERSION): the only lane that
/// serves the challenger data plane (debug_getRawHeader / debug_dbGet, ADR 0007).
inline bool isOpStackLane(int executorVersion)
{
    return executorVersion >= bcos::ledger::OPSTACK_EXECUTOR_VERSION;
}

/// Suggested priority fee (wei): the Ethereum/OP lanes suggest a non-zero tip (OP floors at
/// 1e6 wei, matching op-geth); the legacy FISCO lane keeps its historic constant 0.
inline uint64_t suggestedPriorityFeeWei(int executorVersion)
{
    return usesEthereumFeeSemantics(executorVersion) ? c_minSuggestedPriorityFeeWei : 0;
}

/// True when the chain admits EIP-4844 blob transactions over RPC. Only the pure-Ethereum
/// executor (== ETHEREUM_EXECUTOR_VERSION) does: OP (>= OPSTACK_EXECUTOR_VERSION) refuses them
/// like any L2, and the legacy lane (0/1) has no blob support at all. EL-only concerns that are
/// not about admission (sidecar gossip, Engine blob handling) still key on ethereumELMode.
inline bool admitsBlobTransactions(int executorVersion)
{
    return executorVersion == bcos::ledger::ETHEREUM_EXECUTOR_VERSION;
}

/// The chain's executor_version, read from its one SYS_CONFIG row (not getLedgerConfig: the
/// OP-only endpoints call this per request). An absent row reads as 0, like getLedgerConfig.
inline task::Task<int> readExecutorVersion(bcos::ledger::LedgerInterface& ledger)
{
    auto const config = co_await bcos::ledger::getSystemConfig(
        ledger, magic_enum::enum_name(bcos::ledger::SystemConfig::executor_version));
    co_return config ? boost::lexical_cast<int>(std::get<0>(config.value())) : 0;
}

/// Gate for the OP-lane-only methods: off the OP lane they answer exactly what the dispatcher
/// answers an unregistered method.
inline task::Task<void> requireOpStackLane(
    bcos::ledger::LedgerInterface& ledger, std::string_view method)
{
    auto const executorVersion = co_await readExecutorVersion(ledger);
    if (!isOpStackLane(executorVersion))
    {
        WEB3_LOG(DEBUG) << LOG_DESC("OP-lane-only method on another lane")
                        << LOG_KV("method", method) << LOG_KV("executorVersion", executorVersion);
        BOOST_THROW_EXCEPTION(JsonRpcException(MethodNotFound, "Method not found"));
    }
}
}  // namespace bcos::rpc
