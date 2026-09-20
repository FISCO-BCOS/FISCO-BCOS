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
 * @file EthereumSystemCalls.h
 * @brief Production wiring for the Cancun/Prague block-level system calls
 *        (EIP-4788 beacon roots, EIP-2935 historical block hashes at block start;
 *        EIP-7002/7251 execution-layer requests at block end): adapts the
 *        external Ethereum header to ethereum-executor's EthSystemCalls.h
 *        (systemCallBlockStart / systemCallBlockEnd), which executes the
 *        system contracts over the executor's own EthereumState/EthereumHost
 *        and writes the state updates straight into the view. This header is
 *        a thin adapter only — no bcos-evm / evmone::state adapter types
 *        (StateView / BlockHashes / StateDiff) are involved anymore.
 *
 *        Fork-activation contract-code model (why there is NO code injection
 *        here): all four system contracts are deployed "à la EIP-4788" — by an
 *        ordinary deployment transaction included in a pre-fork block (EIP-2935
 *        documents the synthetic deployment transaction explicitly; the EIP's
 *        "fail silently if no code exists" clause exists precisely for chains
 *        where the deployment never happened). Syncing the chain executes that
 *        deployment transaction like any other, so the code is already in state
 *        when the fork activates — geth injects nothing either. The block-start
 *        call skips a code-less contract silently (per EIP-4788/2935); the
 *        block-end call fails when the EIP-7002/7251 contract is missing, which
 *        we surface as an invalid block: on a real chain both contracts are
 *        deployed, so a failure means our state or the execution diverged —
 *        never a block to commit.
 * @date 2026/9/11
 */
#pragma once

#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "ethereum-executor/EthSystemCalls.h"
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.hpp>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace bcos::scheduler_v1
{
namespace eth_system_calls_detail
{
/// The EthBlockInfo for the system-call surface, built from the external
/// Ethereum header. EthBlockHeaderData timestamps are already wire SECONDS (unlike
/// the internal BlockHeader milliseconds), so no unit conversion here. Only the
/// fields the system contracts actually read matter (NUMBER/TIMESTAMP/GASLIMIT/
/// COINBASE opcodes, parent_beacon_block_root for EIP-4788); the rest stay default.
inline executor_v1::eth::EthBlockInfo blockInfoForSystemCalls(
    protocol::EthBlockHeaderData const& ethHeader)
{
    executor_v1::eth::EthBlockInfo block{};
    block.number = ethHeader.number;
    block.timestamp = ethHeader.timestamp;
    block.gas_limit = ethHeader.gasLimit > u256(std::numeric_limits<int64_t>::max()) ?
                          std::numeric_limits<int64_t>::max() :
                          static_cast<int64_t>(ethHeader.gasLimit);
    std::memcpy(block.coinbase.bytes, ethHeader.coinbase.data(), sizeof(evmc_address));
    if (ethHeader.baseFee)
    {
        block.base_fee = *ethHeader.baseFee > u256(std::numeric_limits<uint64_t>::max()) ?
                             std::numeric_limits<uint64_t>::max() :
                             static_cast<uint64_t>(*ethHeader.baseFee);
    }
    std::memcpy(
        block.prev_randao.bytes, ethHeader.prevRandao.data(), sizeof(evmc_bytes32::bytes));
    if (ethHeader.parentBeaconRoot)
    {
        std::memcpy(block.parent_beacon_block_root.bytes, ethHeader.parentBeaconRoot->data(),
            sizeof(evmc_bytes32::bytes));
    }
    return block;
}

inline evmc::bytes32 toEvmcBytes32(bcos::h256 const& hash)
{
    evmc::bytes32 out{};
    std::memcpy(out.bytes, hash.data(), sizeof(out.bytes));
    return out;
}
}  // namespace eth_system_calls_detail

/// Block-start system calls: EIP-4788 beacon-roots write (Cancun+) and EIP-2935
/// historical block-hash write (Prague+), each gated by revision and skipped
/// silently when the contract has no code (per the EIPs). The state updates are
/// written into `view` in place; returns an error string on failure.
/// Call only when the block's revision >= EVMC_CANCUN.
template <class Storage>
task::Task<std::optional<std::string>> applyBlockStartSystemCalls(
    Storage& view, evmc::VM& vm, protocol::EthBlockHeaderData const& ethHeader,
    evmc_revision rev)
{
    co_return co_await executor_v1::eth::systemCallBlockStart(view, vm,
        eth_system_calls_detail::blockInfoForSystemCalls(ethHeader),
        eth_system_calls_detail::toEvmcBytes32(ethHeader.parentInfo.blockHash), rev);
}

struct BlockEndSystemCallsResult
{
    std::optional<std::string> error;
    std::vector<executor_v1::eth::EthRequests> requests;  ///< EIP-7002/7251 requests (EIP-7685)
};

/// Block-end system calls: EIP-7002 withdrawal requests and EIP-7251 consolidation
/// requests (Prague+). A missing contract code or a reverted call is an error
/// here — on a real chain both contracts are deployed by ordinary pre-fork
/// transactions, so a failure means divergent local state.
/// Call only when the block's revision >= EVMC_PRAGUE.
template <class Storage>
task::Task<BlockEndSystemCallsResult> applyBlockEndSystemCalls(
    Storage& view, evmc::VM& vm, protocol::EthBlockHeaderData const& ethHeader,
    evmc_revision rev)
{
    auto result = co_await executor_v1::eth::systemCallBlockEnd(
        view, vm, eth_system_calls_detail::blockInfoForSystemCalls(ethHeader), rev);
    co_return BlockEndSystemCallsResult{std::move(result.error), std::move(result.requests)};
}
}  // namespace bcos::scheduler_v1
