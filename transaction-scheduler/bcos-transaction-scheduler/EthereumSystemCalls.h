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
 *        EIP-7002/7251 execution-layer requests at block end): runs bcos-evm's
 *        evmone-ported system_call_block_start / system_call_block_end over the
 *        executed storage2 view through the shared Storage2State bridge and
 *        writes the resulting state diff back into the view.
 *
 *        Fork-activation contract-code model (why there is NO code injection
 *        here): all four system contracts are deployed "à la EIP-4788" — by an
 *        ordinary deployment transaction included in a pre-fork block (EIP-2935
 *        documents the synthetic deployment transaction explicitly; the EIP's
 *        "fail silently if no code exists" clause exists precisely for chains
 *        where the deployment never happened). Syncing the chain executes that
 *        deployment transaction like any other, so the code is already in state
 *        when the fork activates — geth injects nothing either. evmone's
 *        block-start call skips a code-less contract silently (per EIP-4788/
 *        2935); its block-end call fails (std::nullopt) when the EIP-7002/7251
 *        contract is missing, which we surface as an invalid block: on a real
 *        chain both contracts are deployed, so a nullopt means our state or the
 *        execution diverged — never a block to commit.
 * @date 2026/9/11
 */
#pragma once

#include "bcos-rlp-protocol/EthBlockHeader.h"
#include <bcos-evm/adapter/RecentBlockHashes.h>
#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/adapter/Storage2State.h>
#include <bcos-evm/eth/state/block.hpp>
#include <bcos-evm/eth/state/requests.hpp>
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.hpp>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bcos::scheduler_v1
{
namespace eth_system_calls_detail
{
/// The evmone BlockInfo for the system-call surface, built from the external
/// Ethereum header. EthBlockHeaderData timestamps are already wire SECONDS (unlike
/// the internal BlockHeader milliseconds), so no unit conversion here. Only the
/// fields the system contracts actually read matter (NUMBER/TIMESTAMP/GASLIMIT/
/// COINBASE opcodes, parent_beacon_block_root for EIP-4788); the rest stay default.
inline evmone::state::BlockInfo blockInfoForSystemCalls(
    protocol::EthBlockHeaderData const& ethHeader)
{
    evmone::state::BlockInfo block{};
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

/// Sanitize + write-back a system-call state diff, mapping every failure channel
/// (block-hash poison, read poison, write-back throw/poison) onto an error string.
template <class Storage>
std::optional<std::string> applySystemCallDiff(
    bcos::evm::evmstate::Storage2State<Storage>& stateView, evmone::state::StateDiff diff,
    std::optional<std::string> const& hashErr, char const* phase)
{
    if (hashErr.has_value())
    {
        return std::string(phase) + ": block-hash lookup failed: " + *hashErr;
    }
    if (stateView.poisoned())
    {
        return std::string(phase) + ": state read failed: " + stateView.firstError();
    }
    try
    {
        stateView.applyDiff(bcos::evm::sanitizeStateDiff(stateView, std::move(diff)));
    }
    catch (const std::exception& e)
    {
        return std::string(phase) + ": state write-back failed: " + e.what();
    }
    catch (...)
    {
        return std::string(phase) + ": state write-back failed: unknown exception";
    }
    if (stateView.poisoned())
    {
        return std::string(phase) + ": state write-back failed: " + stateView.firstError();
    }
    return std::nullopt;
}
}  // namespace eth_system_calls_detail

/// Block-start system calls: EIP-4788 beacon-roots write (Cancun+) and EIP-2935
/// historical block-hash write (Prague+). evmone gates each contract by revision
/// internally and silently skips a contract whose code is absent (per the EIPs).
/// The diff is applied to `view` in place; returns an error string on failure.
/// Call only when the block's revision >= EVMC_CANCUN.
template <class Storage>
std::optional<std::string> applyBlockStartSystemCalls(
    Storage& view, evmc::VM& vm, protocol::EthBlockHeaderData const& ethHeader,
    evmc_revision rev)
{
    auto block = eth_system_calls_detail::blockInfoForSystemCalls(ethHeader);
    std::optional<std::string> hashErr;
    bcos::evm::engine::detail::RecentBlockHashes<Storage> blockHashes(view, block.number,
        eth_system_calls_detail::toEvmcBytes32(ethHeader.parentInfo.blockHash), &hashErr);
    bcos::evm::evmstate::Storage2State<Storage> stateView(view);
    auto diff = evmone::state::system_call_block_start(stateView, block, blockHashes, rev, vm);
    return eth_system_calls_detail::applySystemCallDiff(
        stateView, std::move(diff), hashErr, "block-start system call (EIP-4788/2935)");
}

struct BlockEndSystemCallsResult
{
    std::optional<std::string> error;
    std::vector<evmone::state::Requests> requests;  ///< EIP-7002/7251 requests (EIP-7685)
};

/// Block-end system calls: EIP-7002 withdrawal requests and EIP-7251 consolidation
/// requests (Prague+). A std::nullopt from evmone (system contract code missing or
/// the call reverted) is an error here — on a real chain both contracts are deployed
/// by ordinary pre-fork transactions, so a failure means divergent local state.
/// Call only when the block's revision >= EVMC_PRAGUE.
template <class Storage>
BlockEndSystemCallsResult applyBlockEndSystemCalls(
    Storage& view, evmc::VM& vm, protocol::EthBlockHeaderData const& ethHeader,
    evmc_revision rev)
{
    BlockEndSystemCallsResult result;
    auto block = eth_system_calls_detail::blockInfoForSystemCalls(ethHeader);
    std::optional<std::string> hashErr;
    bcos::evm::engine::detail::RecentBlockHashes<Storage> blockHashes(view, block.number,
        eth_system_calls_detail::toEvmcBytes32(ethHeader.parentInfo.blockHash), &hashErr);
    bcos::evm::evmstate::Storage2State<Storage> stateView(view);
    auto endResult = evmone::state::system_call_block_end(stateView, block, blockHashes, rev, vm);
    if (!endResult.has_value())
    {
        result.error =
            "block-end system call (EIP-7002/7251) failed: system contract code missing "
            "or execution reverted";
        return result;
    }
    result.error = eth_system_calls_detail::applySystemCallDiff(stateView,
        std::move(endResult->state_diff), hashErr, "block-end system call (EIP-7002/7251)");
    result.requests = std::move(endResult->requests);
    return result;
}
}  // namespace bcos::scheduler_v1
