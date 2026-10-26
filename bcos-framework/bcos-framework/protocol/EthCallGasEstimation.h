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
 *   - recordFrameEnter / recordFrameExit bracket every frame. EthereumHost::call, which
 *     every frame passes through, calls them.
 *   - recordChildAllowance derives the call-site prefix from the child's allowance.
 *   - recordStorageWrite marks the frame the SSTORE belongs to.
 *
 * This header deliberately depends on nothing beyond the standard library: bcos-rpc arms and
 * reads the session, while ethereum-executor feeds it, and the RPC layer must not acquire the
 * EVM headers (or a link dependency on them) to do so.
 *
 * Still out of scope, as in the model it implements: paths that depend on the budget itself
 * (gasleft() branches), value-call stipends beyond the allowance correction, explicit
 * call{gas: k} caps (over-estimated, see above), calls whose failure the caller survives, and
 * the CREATE-family rules that need modelling beyond f(L).
 */
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <thread>

namespace bcos::protocol::gas
{

/// Maximum call depth evmone permits; a frame's depth is < 1024, so 0..1023 (plus the root).
inline constexpr std::size_t c_maxCallFrames = 1025;

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

/// Everything one estimation run accumulates, per thread.
struct CallGasSession
{
    bool armed = false;
    /// Identity of the run that armed this thread's session. A CallGasEstimationScope only ever
    /// clears a session carrying its own generation, so a scope that is destroyed on a different
    /// thread -- or while another estimate has since armed the same thread -- cannot wipe that
    /// run's state. Without it the destructor cleared whatever session happened to be in the
    /// resuming thread's TLS, which could belong to a concurrent estimate.
    uint64_t generation = 0;
    std::array<CallFrame, c_maxCallFrames> frames{};
    /// Frame currently executing (the innermost one), so a host callback with no depth
    /// argument of its own -- set_storage -- can be attributed. -1 when no frame runs.
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

inline thread_local CallGasSession g_callGasSession;

/// Source of session generations. Relaxed is enough: the value only has to distinguish runs.
inline uint64_t nextCallGasGeneration() noexcept
{
    static std::atomic<uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

/// Bracket a frame. Both must be called for every frame the host enters, including the root.
inline void recordFrameEnter(int32_t depth, int64_t gas) noexcept
{
    auto& session = g_callGasSession;
    if (!session.armed || depth < 0 || static_cast<std::size_t>(depth) >= c_maxCallFrames)
        return;
    session.frames[depth] = CallFrame{.entryGas = gas};
    session.currentDepth = depth;
}

/// Report a child's gas allowance, as evmc_message::gas carries it at host entry.
///
/// @param childDepth the child's depth (evmc_message::depth); its caller is one level up.
/// @param allowance  the gas the child was given, stipend included.
/// @param hasValue   whether the call transfers value, in which case evmone added the 2300
///                   stipend on top of the forwarded share.
inline void recordChildAllowance(int32_t childDepth, int64_t allowance, bool hasValue) noexcept
{
    auto& session = g_callGasSession;
    auto const parentDepth = childDepth - 1;
    if (!session.armed || parentDepth < 0 ||
        static_cast<std::size_t>(parentDepth) >= c_maxCallFrames)
        return;

    // Undo the stipend so what remains is f(caller_remaining), then invert it. h() is the
    // least caller remainder consistent with that allowance; treating it as the remainder
    // makes the site's prefix over-estimated by at most one gas.
    auto const forwarded = allowance - (hasValue ? c_sstoreSentryGas : 0);
    auto const callerRemaining = minimumBudgetToForward(forwarded);

    auto& frame = session.frames[parentDepth];
    frame.pendingPrefix = frame.entryGas - callerRemaining;
    frame.pending = true;
}

/// Mark the frame that is executing right now as having written storage (EIP-2200 sentry).
inline void recordStorageWrite() noexcept
{
    auto& session = g_callGasSession;
    if (!session.armed || session.currentDepth < 0)
        return;
    session.frames[session.currentDepth].wroteStorage = true;
}

/// Close the frame at @p depth and fold its requirement into its caller.
inline void recordFrameExit(int32_t depth, int64_t gasLeft) noexcept
{
    auto& session = g_callGasSession;
    if (!session.armed || depth < 0 || static_cast<std::size_t>(depth) >= c_maxCallFrames)
        return;

    auto const& frame = session.frames[depth];
    // used + 2301 when the frame wrote storage: the SSTORE sentry is a remaining-gas floor, so
    // a frame that writes needs more than it spends. The prefix plus cost of that instruction
    // is at most used, which makes this a safe upper bound.
    auto required = std::max(frame.maxRequired, frame.entryGas - gasLeft);
    if (frame.wroteStorage)
        required += c_sstoreSentryGas + 1;

    session.currentDepth = depth - 1;

    if (depth == 0)
    {
        session.rootRequired = required;
        return;
    }

    auto& parent = session.frames[depth - 1];
    if (!parent.pending)
        return;  // The site was never reported; nothing to charge this child to.
    parent.maxRequired =
        std::max(parent.maxRequired, parent.pendingPrefix + minimumBudgetToForward(required));
    parent.pending = false;
}

/// The transaction's intrinsic cost and EIP-7623 floor, reported where gasLimit is split into
/// intrinsic + execution.
inline void recordIntrinsicGas(int64_t intrinsic, int64_t minCost) noexcept
{
    if (g_callGasSession.armed)
    {
        g_callGasSession.intrinsicGas = intrinsic;
        g_callGasSession.minCost = minCost;
    }
}

/// Arms the accounting for one call and exposes its result. Not reentrant.
class CallGasEstimationScope
{
public:
    CallGasEstimationScope() : m_generation(nextCallGasGeneration())
    {
        auto& session = g_callGasSession;
        session.armed = true;
        session.generation = m_generation;
        session.currentDepth = -1;
        session.intrinsicGas = 0;
        session.minCost = 0;
        session.rootRequired.reset();
        // frames is deliberately NOT cleared: recordFrameEnter overwrites the frame it enters
        // and no code reads a frame that was not entered by this run, so zeroing 1025 x 40 B
        // (32 KB) of thread-local storage on every estimate buys nothing.
    }

    ~CallGasEstimationScope() noexcept
    {
        // Only disarm the session this scope armed, on the thread that armed it. A coroutine
        // that armed on one thread and is destroyed on another must not clear the resumed
        // thread's session: that session may belong to a different, still-running estimate, and
        // clearing it would turn its answer into the "no accounting" fallback or -- worse --
        // leave stale frames visible to it.
        auto& session = g_callGasSession;
        if (session.generation != m_generation)
            return;
        session.armed = false;
        session.currentDepth = -1;
        session.intrinsicGas = 0;
        session.minCost = 0;
        session.rootRequired.reset();
    }

    CallGasEstimationScope(const CallGasEstimationScope&) = delete;
    CallGasEstimationScope& operator=(const CallGasEstimationScope&) = delete;

    /// Whether this scope's session is still the one on the current thread, i.e. whether the
    /// accounting can be attributed to this run at all. False when the coroutine resumed on a
    /// different thread than it armed on, or when a later scope took over this thread.
    [[nodiscard]] bool ownsCurrentThreadSession() const noexcept
    {
        auto const& session = g_callGasSession;
        return session.armed && session.generation == m_generation;
    }

    /// The root frame's minimum entry budget, or nullopt if the run never executed a frame
    /// (for instance when the executor ran on another thread).
    [[nodiscard]] std::optional<int64_t> executionBudget() const noexcept
    {
        if (!ownsCurrentThreadSession())
            return std::nullopt;
        return g_callGasSession.rootRequired;
    }

    [[nodiscard]] int64_t intrinsicGas() const noexcept
    {
        return ownsCurrentThreadSession() ? g_callGasSession.intrinsicGas : 0;
    }

    [[nodiscard]] int64_t minCost() const noexcept
    {
        return ownsCurrentThreadSession() ? g_callGasSession.minCost : 0;
    }

    /// Least transaction gas limit a caller needs: intrinsic plus the execution budget, but
    /// never below the EIP-7623 floor -- a calldata-heavy call can need more gas than its
    /// execution consumes, and admission checks max(intrinsic, min_cost) before the EVM runs.
    [[nodiscard]] std::optional<int64_t> gasLimit() const noexcept
    {
        auto const budget = executionBudget();
        if (!budget.has_value())
            return std::nullopt;
        return std::max(intrinsicGas() + *budget, minCost());
    }

private:
    uint64_t m_generation;
};

}  // namespace bcos::protocol::gas
