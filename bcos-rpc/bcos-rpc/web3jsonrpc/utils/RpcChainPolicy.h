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
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-rlp-protocol/BlockHeaderHash.h>
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

/// A committed block's base fee under the lane rules. OP-Stack headers are NON_ETH yet
/// carry a real base fee (rebuildOpEthHeader deliberately leaves ethBlockVersion NON_ETH)
/// — check that case before the NON_ETH short-circuit, which is for native FISCO headers
/// that have no base fee at all. Eth-lane headers take the London+ rule (0 pre-London).
inline bcos::u256 blockBaseFee(bcos::protocol::BlockHeader const& header)
{
    auto const versionAtLeast = [](bcos::protocol::EthBlockVersion version,
                                    bcos::protocol::EthBlockVersion fork) {
        return static_cast<std::uint8_t>(version) >= static_cast<std::uint8_t>(fork);
    };
    if (bcos::protocol::isOpEthereumBlock(header))
    {
        return header.baseFee().value_or(0);
    }
    if (!versionAtLeast(header.ethBlockVersion(), bcos::protocol::EthBlockVersion::LONDON))
    {
        return 0;
    }
    return header.baseFee().value_or(0);
}
}  // namespace bcos::rpc
