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
 * @file EthCallGasEstimation.h
 * @brief Per-frame minimum gas requirement, reconstructed from one EVM simulation.
 *
 * eth_estimateGas cannot report the gas a call *used*: EIP-150 hands a nested frame only
 * 63/64 of what its caller has left, so a frame's minimum entry budget is larger than its
 * consumption, and scaling the total by (64/63)^depth over-counts (intrinsic gas is never
 * forwarded, and the depth is a maximum over the whole trace rather than a per-site fact).
 *
 * The requirement is defined per frame instead:
 *
 *     used(A)      = A's entry gas - A's gas left              (A plus all descendants)
 *     required(A)  = max(used(A) [+ 2301 if A wrote storage],
 *                        max over call sites i of prefix(i) + h(required(child_i)))
 *
 * where prefix(i) is the gas A had consumed before child i's share was carved out of what
 * remained, and h is the exact inverse of the 63/64 rule:
 *
 *     f(L) = L - floor(L / 64)          most gas forwardable from L
 *     h(r) = 0                          if r == 0
 *          = r + floor((r - 1) / 63)    least L with f(L) >= r
 *
 * h is not ceil(64r/63): for r = 64 that is 66 while the true minimum is 65.
 *
 * Call sites take the maximum, never the sum: gas a frame keeps after a child returns is
 * still spendable afterwards. Taking the maximum with used(A) is what covers the tail the
 * frame still needs after its last child returns.
 *
 * The session belongs to the execution that produces it
 * -----------------------------------------------------
 * This used to be a `thread_local` global that the RPC armed and later read. That cannot work:
 * the RPC arms on the handler thread, the EVM runs on whichever thread the scheduler hands it
 * to (an enqueued arena task, a TBB worker resumed after a storage read), and the coroutine
 * resumes wherever its completion lands. A `thread_local` is per *thread*, so the recorder and
 * the reader were frequently looking at two different objects -- the accounting silently
 * produced nothing, and eth_estimateGas fell back to the raw gasUsed, which is precisely the
 * under-estimate this header exists to fix.
 *
 * So the session is now an ordinary object with one owner, on the coroutine frame of the
 * transaction being simulated: the executor's ExecuteContext creates it for a dry run and
 * publishes its address through EthCallParams, which already reaches both recording sites --
 * validateTransaction (the intrinsic split) and EthereumHost::call (the frame brackets). There
 * is no arming, no generation and no lookup by thread: the pointer is captured before any
 * suspension and dereferenced after, so it identifies the same object on any thread. The result
 * travels back to the RPC as a value on the receipt the caller already receives.
 *
 * Only the EVMC host interface is used -- no evmone changes
 * ---------------------------------------------------------
 * prefix(i) needs the caller's remaining gas at the moment it carves out the child's share,
 * which no EVMC callback carries (evmc_message::gas is the *callee's* allowance). It is
 * recovered from that allowance instead: evmone sets
 *
 *     allowance = min(requested, f(caller_remaining))
 *
 * so with the usual "forward everything" call (`GAS` opcode, no explicit cap) the allowance
 * is exactly f(caller_remaining), and h(allowance) is the least caller_remaining consistent
 * with it. Fed back as if it were the caller's remainder, that differs from the true prefix
 * by at most one gas per call site, always in the safe (over-estimating) direction. An
 * explicit `call{gas: k}` cap breaks the identity and is over-estimated -- never
 * under-estimated, which is the direction that matters.
 *
 * The SSTORE sentry is handled from the host too: EIP-2200 fails an SSTORE whose frame has
 * `gas_left <= 2300` *before* charging anything, so a frame that writes storage needs 2301
 * more than it consumes. `EthereumHost::set_storage` is called once per SSTORE, which is
 * enough to mark the frame; charging used(A) + 2301 is a safe upper bound because the prefix
 * plus cost of that instruction is at most used(A).
 *
 * Data flow (one simulation, bottom-up):
 *   - frameEnter / frameExit bracket every frame. EthereumHost::call, which every frame
 *     passes through, calls them.
 *   - childAllowance derives the call-site prefix from the child's allowance.
 *   - storageWrite marks the frame the SSTORE belongs to.
 *
 * This header deliberately depends on nothing beyond the standard library: bcos-rpc reads the
 * result off the receipt, while ethereum-executor feeds it, and the RPC layer must not acquire
 * the EVM headers (or a link dependency on them) to do so.
 *
 * Still out of scope, as in the model it implements: paths that depend on the budget itself
 * (gasleft() branches), explicit call{gas: k} caps (over-estimated, see above), calls whose
 * failure the caller survives, and the CREATE-family rules that need modelling beyond f(L).
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace bcos::protocol::gas
{

/// The deepest frame evmone permits (depth < 1024, so 0..1023).
inline constexpr int32_t c_maxCallDepth = 1024;

/// EIP-2200 SSTORE sentry: the frame must hold MORE than this many gas before the write.
inline constexpr int64_t c_sstoreSentryGas = 2300;

/// Least budget a frame must hold so that EIP-150 lets it forward at least @p required gas.
/// Exact inverse of `L - L / 64`, not the ceil(64r/63) approximation.
inline constexpr int64_t minimumBudgetToForward(int64_t required) noexcept
{
    return required <= 0 ? 0 : required + (required - 1) / 63;
}

/// One frame of the simulation's call stack.
struct CallFrame
{
    /// Gas the frame was entered with (evmc_message::gas for this frame).
    int64_t entryGas = 0;
    /// Largest initial budget any processed call site of this frame demands.
    int64_t maxRequired = 0;
    /// Gas consumed by this frame before the pending child's share was carved out.
    int64_t pendingPrefix = 0;
    /// A call site has been reported and its child frame has not returned yet.
    bool pending = false;
    /// This frame executed an SSTORE, so it must clear the EIP-2200 sentry as well.
    bool wroteStorage = false;
};

/// One estimation run. Owned by the execution that produces it (the executor's ExecuteContext),
/// never by a thread, so the recorder and the reader are the same object no matter which thread
/// either one runs on.
///
/// `frames` grows to the depth actually reached instead of reserving the 1024-frame worst case:
/// a shallow call pays for a handful of frames, and a deep one still cannot exceed the EVM's own
/// limit.
class CallGasSession
{
public:
    /// Bracket a frame. Both must be called for every frame the host enters, including the root.
    void frameEnter(int32_t depth, int64_t gas) noexcept
    {
        if (depth < 0 || depth >= c_maxCallDepth)
            return;
        auto const index = static_cast<std::size_t>(depth);
        if (frames.size() <= index)
            frames.resize(index + 1);
        frames[index] = CallFrame{.entryGas = gas};
        currentDepth = depth;
    }

    /// Report a child's gas allowance, as evmc_message::gas carries it at host entry.
    ///
    /// @param childDepth the child's depth (evmc_message::depth); its caller is one level up.
    /// @param allowance  the gas the child was given, stipend included.
    /// @param hasValue   whether the child received the 2300 stipend (CALL/CALLCODE with value).
    void childAllowance(int32_t childDepth, int64_t allowance, bool hasValue) noexcept
    {
        auto const parentDepth = childDepth - 1;
        if (parentDepth < 0 || parentDepth >= c_maxCallDepth ||
            static_cast<std::size_t>(parentDepth) >= frames.size())
            return;

        // Undo the stipend so what remains is f(caller_remaining), then invert it. h() is the
        // least caller remainder consistent with that allowance; treating it as the remainder
        // makes the site's prefix over-estimated by at most one gas.
        auto const forwarded = allowance - (hasValue ? c_sstoreSentryGas : 0);
        auto const callerRemaining = minimumBudgetToForward(forwarded);

        auto& frame = frames[static_cast<std::size_t>(parentDepth)];
        frame.pendingPrefix = frame.entryGas - callerRemaining;
        frame.pending = true;
    }

    /// Mark the frame that is executing right now as having written storage (EIP-2200 sentry).
    void storageWrite() noexcept
    {
        if (currentDepth < 0 || static_cast<std::size_t>(currentDepth) >= frames.size())
            return;
        frames[static_cast<std::size_t>(currentDepth)].wroteStorage = true;
    }

    /// Close the frame at @p depth and fold its requirement into its caller.
    void frameExit(int32_t depth, int64_t gasLeft) noexcept
    {
        if (depth < 0 || depth >= c_maxCallDepth ||
            static_cast<std::size_t>(depth) >= frames.size())
            return;

        auto const& frame = frames[static_cast<std::size_t>(depth)];
        // used + 2301 when the frame wrote storage: the SSTORE sentry is a remaining-gas floor,
        // so a frame that writes needs more than it spends. The prefix plus cost of that
        // instruction is at most used, which makes this a safe upper bound.
        auto required = std::max(frame.maxRequired, frame.entryGas - gasLeft);
        if (frame.wroteStorage)
            required += c_sstoreSentryGas + 1;

        currentDepth = depth - 1;

        if (depth == 0)
        {
            rootRequired = required;
            return;
        }

        auto& parent = frames[static_cast<std::size_t>(depth - 1)];
        if (!parent.pending)
            return;  // The site was never reported; nothing to charge this child to.
        parent.maxRequired =
            std::max(parent.maxRequired, parent.pendingPrefix + minimumBudgetToForward(required));
        parent.pending = false;
    }

    /// The transaction's intrinsic cost and EIP-7623 floor, reported where gasLimit is split
    /// into intrinsic + execution.
    void intrinsicCost(int64_t intrinsic, int64_t minCost) noexcept
    {
        intrinsicGas = intrinsic;
        this->minCost = minCost;
    }

    /// Least transaction gas limit the caller needs, or nullopt when the run never reached the
    /// EVM (a transaction rejected before execution has no per-frame requirement to report).
    [[nodiscard]] std::optional<int64_t> gasLimit() const noexcept
    {
        if (!rootRequired.has_value())
            return std::nullopt;
        return std::max(intrinsicGas + *rootRequired, minCost);
    }

    [[nodiscard]] std::optional<int64_t> executionBudget() const noexcept { return rootRequired; }
    [[nodiscard]] int64_t intrinsicGasValue() const noexcept { return intrinsicGas; }
    [[nodiscard]] int64_t minCostValue() const noexcept { return minCost; }

private:
    std::vector<CallFrame> frames;
    /// Frame currently executing (the innermost one), so a host callback with no depth argument
    /// of its own -- set_storage -- can be attributed. -1 when no frame runs.
    int32_t currentDepth = -1;
    /// Intrinsic (calldata + base) cost of the transaction, charged before the root frame.
    int64_t intrinsicGas = 0;
    /// EIP-7623 floor: the least gas the transaction must be given, floor(21000 + 10 * tokens).
    /// Independent of what execution consumes, so it has to be reported separately -- the
    /// admission check rejects anything below max(intrinsic, min_cost) before the EVM runs.
    int64_t minCost = 0;
    /// Root frame's required budget; absent when the run never reached the EVM.
    std::optional<int64_t> rootRequired;
};

}  // namespace bcos::protocol::gas
