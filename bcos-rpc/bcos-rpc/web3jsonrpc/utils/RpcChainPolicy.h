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
/// value (isOpLaneVersion), decided at chain creation and never re-derived.
/// Note the deliberate difference from the ledger's feature_l2_ethereum_compat state shape:
/// the Eth lane may carry that flag too (an MPT-state chain still sealed by the consensus
/// layer), so lane decisions key on executor_version and never on the flag.
inline bool usesEthereumFeeSemantics(int executorVersion)
{
    return executorVersion >= bcos::ledger::ETHEREUM_EXECUTOR_VERSION;
}

/// True when the chain runs the OP lane. Delegates to the ledger's one lane predicate
/// (isOpLaneVersion, >=): setVersion saturates a value above the newest wired slot onto it,
/// so such a chain RUNS the OP executor and must get OP fee semantics here — an == test
/// would answer FISCO-lane fees for a chain the executor prices as OP. This is a LANE
/// predicate, distinct from the ledger's feature_l2_ethereum_compat state shape: the Eth
/// lane may carry that flag too (an MPT-state chain still sealed by the consensus layer),
/// and on such a chain the OP base-fee rule must not apply. eth_feeHistory consumes this,
/// so feeHistory and eth_gasPrice agree on every configuration.
inline bool isOpStackLane(int executorVersion)
{
    return bcos::ledger::isOpLaneVersion(executorVersion);
}

/// True when EIP-7825's per-tx gas ceiling (MAX_TX_GAS_LIMIT, 2^24) is actually in force
/// at the target block: the chain runs Osaka+ rules there. OP lane: Karst activation is
/// timestamp-keyed (op-node's IsKarst: ts >= karst_time) on the chain's own resolved
/// schedule; an OP chain without the row (initialized before it existed) is treated as
/// pre-Karst here. Eth lane: the persisted revision map at the block's height. Everywhere
/// else — OP pre-Karst, Eth pre-Osaka, and the whole legacy FISCO lane (block gas up to
/// 3e9) — there is no per-tx ceiling, so an estimate budget must NOT be clamped to 2^24:
/// a transaction consuming between 2^24 and the block limit is admissible there and its
/// estimate must not fail (M1).
[[nodiscard]] inline bool eip7825InForceAt(bcos::ledger::LedgerConfig const& ledgerConfig,
    bcos::protocol::BlockNumber targetBlock, uint64_t targetTimestampSeconds)
{
    if (isOpStackLane(ledgerConfig.executorVersion()))
    {
        const auto& schedule = ledgerConfig.opForkSchedule();
        return schedule.has_value() && schedule->m_karstTime != bcos::ledger::c_opForkTimeUnset &&
               targetTimestampSeconds >= schedule->m_karstTime;
    }
    if (usesEthereumFeeSemantics(ledgerConfig.executorVersion()))
    {
        const auto revision = ledgerConfig.evmcRevisionForBlock(targetBlock);
        return revision.has_value() && *revision >= EVMC_OSAKA;
    }
    return false;
}


/// Suggested priority fee (wei): the Ethereum/OP lanes suggest a non-zero tip (OP floors at
/// 1e6 wei, matching op-geth); the legacy FISCO lane keeps its historic constant 0.
inline uint64_t suggestedPriorityFeeWei(int executorVersion)
{
    return usesEthereumFeeSemantics(executorVersion) ? c_minSuggestedPriorityFeeWei : 0;
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
