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
 * @file CheckSet.h
 * @brief Which admission checks run, as a function of (transaction kind, context, policy).
 * @date 2026/8/25
 */

#pragma once

#include <array>
#include <cstdint>

namespace bcos::txvalidator
{

/// Routing key. Decided from the SIGNED envelope's first byte -- by kindOf() in TxValidator.cpp,
/// once normalization has authenticated that envelope -- never from the tars mirror: a peer can
/// set the mirror kind to 0 and a legacy-keyed check set would then skip the fee-market rules a
/// 1559 envelope is subject to.
///
/// The enumerator values are NOT EIP-2718 type bytes (Web3AccessList is 2 here and 0x01 on the
/// wire). kindOf() is the only producer, dispatching on the decoded envelope; casting a type
/// byte to this enum is always wrong.
enum class TxKind : uint8_t
{
    Bcos,            ///< outer tars type == BCOSTransaction; no EIP-2718 envelope
    Web3Legacy,      ///< RLP list header, no type byte
    Web3AccessList,  ///< 0x01, EIP-2930
    Web3DynamicFee,  ///< 0x02, EIP-1559
    Web3SetCode,     ///< 0x04, EIP-7702
    Rejected,        ///< blob(0x03) / deposit(0x7e) / reserved -- TypeGate always fails
};

enum class AdmissionContext : uint8_t
{
    /// RPC and P2P broadcast into either transaction pool. Full check set.
    PoolAdmission,

    /// Consensus proposal verification. Returning a failure here fails the WHOLE proposal and
    /// triggers a view change -- it does not reject one transaction. Balance, the nonce window
    /// and the pool's pending-nonce set are local state that may legitimately differ from the
    /// leader's view, so running them here is a liveness risk, not a safety gain.
    ProposalVerification,

    /// EEST fixture replay against a live node. Fixtures use arbitrary nonces and unfunded
    /// accounts, so balance and the nonce window are dropped; everything else is unchanged.
    EESTReplay,
};

/// Whether this chain verifies signatures at all (experimental.check_transaction_signature).
///
/// This is CONFIGURATION and is orthogonal to the context. It is deliberately NOT derived from
/// Transaction::tainted(): `tainted` means "came from outside, MUST be verified" and defaults to
/// true, so reading it as "already verified" inverts the meaning. EthEndpoint::sendRawTransaction
/// constructs TransactionImpl directly, leaving m_tainted == true, so every ordinary
/// eth_sendRawTransaction would be treated as pre-verified and skip signature, EOA, nonce and
/// balance checks.
///
/// No "already verified" state is consulted at all. The Signature check re-taints the
/// transaction (clearSenderAndHash) before Transaction::verify(), so recovery runs on every call
/// and no flag has to be threaded through any call chain. The cost, one recovery per transaction
/// on the sync path, is what the pool-side validator this replaces paid too.
enum class SignaturePolicy : uint8_t
{
    Required,
    Disabled,
};

enum class Check : uint32_t
{
    None = 0,
    TypeGate = 1U << 0,  ///< blob / deposit / unsupported envelope
    ToFieldFormat = 1U << 1,
    Signature = 1U << 2,
    BcosGroupChainId = 1U << 3,
    TypeByRevision = 1U << 4,
    TipNotAboveCap = 1U << 5,
    SetCodeHasTo = 1U << 6,
    AuthListNonEmpty = 1U << 7,
    MaxGasLimit = 1U << 8,  ///< the Osaka constant cap and the tx_gas_limit config, one item
    FeeCapVsBaseFee = 1U << 9,
    ChainId = 1U << 10,
    SenderIsEOA = 1U << 11,
    NonceNotMax = 1U << 12,
    /// Lower bound and queue depth. The "committed nonce" it compares against is
    /// Web3NonceChecker's in-process LRU cache, with a ledger read only on a miss -- node-local
    /// state, which is why the proposal column drops it.
    Web3NonceWindow = 1U << 13,
    InitCodeSize = 1U << 14,
    Balance = 1U << 15,
    IntrinsicGas = 1U << 16,
    /// The nonce is already held by a PENDING transaction in this node's pool
    /// (TxPoolNonceChecker::checkNonce). Node-local state.
    BcosPoolNonce = 1U << 17,
    /// The nonce is already COMMITTED inside the block-limit window, or blockLimit itself is out
    /// of range (LedgerNonceChecker::checkNonce). Chain state.
    BcosLedgerNonce = 1U << 18,
};

constexpr Check operator|(Check lhs, Check rhs) noexcept
{
    return static_cast<Check>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}
constexpr Check operator&(Check lhs, Check rhs) noexcept
{
    return static_cast<Check>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}
constexpr Check operator~(Check value) noexcept
{
    return static_cast<Check>(~static_cast<uint32_t>(value));
}
constexpr bool contains(Check set, Check item) noexcept
{
    return (set & item) != Check::None;
}

/// The evaluation order, in three stages. The bitmask expresses membership only; the order
/// decides which code a transaction violating several rules reports -- and EEST fixtures assert
/// specific error codes, so that has to be deterministic.
///
/// A check belongs to the stage whose inputs it reads, and verify() fetches a stage's inputs
/// once, before that stage's first check, never per check:
///
///   gate  -- reads the transaction alone. FISCO's own pre-checks: a refused envelope type, a
///            malformed `to`, a bad signature or a foreign group/chain id is rejected without a
///            single read, which is what keeps unauthenticated P2P input cheap to refuse.
///   state -- evmone's validate_transaction (bcos-evm/bcos-evm/eth/state/state.cpp, mirrored by
///            ethereum-executor/EthereumTransition.h), in ITS order, judged against one chain
///            view -- a configuration snapshot plus the fee and chain-id keys -- and, only when
///            the set contains a sender-dependent check, one account read. evmone validates the
///            type-specific block BEFORE the shared fee rules -- for a 7702 envelope that is "to
///            present" and "authorization list non-empty" ahead of the tip/fee-cap comparison --
///            so a transaction violating both reports the same first error here as at execution.
///   pool  -- the BCOS nonce checkers, pool set before ledger, as the pool-side validator ran
///            them. Reads the pool, not the chain.
///
/// Per (kind, context) this yields exactly the reads the table implies: a BCOS transaction
/// reads nothing, a Web3 proposal reads the chain view, a Web3 admission reads the chain view
/// and the account.
inline constexpr std::array c_gateOrder{
    Check::TypeGate,
    Check::ToFieldFormat,
    Check::Signature,
    Check::BcosGroupChainId,
};
inline constexpr std::array c_stateOrder{
    Check::TypeByRevision,
    Check::SetCodeHasTo,
    Check::AuthListNonEmpty,
    Check::TipNotAboveCap,
    Check::MaxGasLimit,
    Check::FeeCapVsBaseFee,
    Check::ChainId,
    Check::SenderIsEOA,
    Check::NonceNotMax,
    Check::Web3NonceWindow,
    Check::InitCodeSize,
    Check::Balance,
    Check::IntrinsicGas,
};
inline constexpr std::array c_poolOrder{
    Check::BcosPoolNonce,
    Check::BcosLedgerNonce,
};

namespace detail
{
template <std::size_t... N>
constexpr std::array<Check, (N + ...)> concat(std::array<Check, N> const&... stages)
{
    std::array<Check, (N + ...)> out{};
    std::size_t index = 0;
    auto append = [&](auto const& stage) {
        for (auto check : stage)
        {
            out.at(index) = check;
            ++index;
        }
    };
    (append(stages), ...);
    return out;
}

template <std::size_t N>
constexpr Check unionOf(std::array<Check, N> const& stage)
{
    Check out = Check::None;
    for (auto check : stage)
    {
        out = out | check;
    }
    return out;
}

/// Position of @p check in @p stage, or the stage's size when absent. For static_asserts that
/// pin one check's place relative to another's within a stage.
template <std::size_t N>
constexpr std::size_t indexOf(std::array<Check, N> const& stage, Check check)
{
    std::size_t index = 0;
    for (auto entry : stage)
    {
        if (entry == check)
        {
            return index;
        }
        ++index;
    }
    return index;
}
}  // namespace detail

/// The single evaluation order: the three stages back to back.
inline constexpr std::array c_checkOrder = detail::concat(c_gateOrder, c_stateOrder, c_poolOrder);

/// Membership masks of the stages that read something, so verify() can ask "does this set need
/// the chain view / the pool" as one expression over the table.
inline constexpr Check c_stateStage = detail::unionOf(c_stateOrder);
inline constexpr Check c_poolStage = detail::unionOf(c_poolOrder);

/// Checks that cannot run without a recovered sender, and must therefore be skipped when
/// signature verification is switched off.
///
/// BcosPoolNonce and BcosLedgerNonce are deliberately NOT here. A BCOS transaction's nonce is a
/// global random value, not an account sequence number: TxPoolNonceChecker::checkNonce reads
/// only _tx.nonce() against a global set, LedgerNonceChecker::checkNonce the same against the
/// committed window, and checkBlockLimit reads only _tx.blockLimit(). None touches the sender,
/// so including them would make SignaturePolicy::Disabled silently switch off BCOS replay
/// protection as a side effect.
inline constexpr Check c_senderDependent =
    Check::SenderIsEOA | Check::NonceNotMax | Check::Web3NonceWindow | Check::Balance;

/// Checks shared by every Web3 kind.
inline constexpr Check c_web3Common =
    Check::TypeGate | Check::ToFieldFormat | Check::Signature | Check::MaxGasLimit |
    Check::FeeCapVsBaseFee | Check::ChainId | Check::SenderIsEOA | Check::NonceNotMax |
    Check::Web3NonceWindow | Check::InitCodeSize | Check::Balance | Check::IntrinsicGas;

/// The check set for @p kind under PoolAdmission. The other two contexts are derived from this
/// one rather than written out separately, so the columns cannot drift apart.
constexpr Check poolAdmissionCheckSet(TxKind kind) noexcept
{
    switch (kind)
    {
    case TxKind::Bcos:
        // A BCOS transaction's dataHash covers its whole TransactionData, so none of the
        // envelope-derived Web3 rules apply to it.
        return Check::TypeGate | Check::ToFieldFormat | Check::Signature | Check::BcosGroupChainId |
               Check::BcosPoolNonce | Check::BcosLedgerNonce;
    case TxKind::Web3Legacy:
        return c_web3Common;
    case TxKind::Web3AccessList:
        return c_web3Common | Check::TypeByRevision;
    case TxKind::Web3DynamicFee:
        return c_web3Common | Check::TypeByRevision | Check::TipNotAboveCap;
    case TxKind::Web3SetCode:
        return c_web3Common | Check::TypeByRevision | Check::TipNotAboveCap | Check::SetCodeHasTo |
               Check::AuthListNonEmpty;
    case TxKind::Rejected:
        // TypeGate alone, and it necessarily fails.
        return Check::TypeGate;
    }
    // Unreachable for a TxKind that kindOf() produced: every enumerator has a case above, and
    // -Wswitch (an error in this build) refuses a new enumerator without one. Fail closed all the
    // same: a value that is not a TxKind gets the gate alone, and the gate rejects any kind it
    // does not recognise. Returning None here would admit such a value without a single check.
    return Check::TypeGate;
}

constexpr Check checkSet(TxKind kind, AdmissionContext context) noexcept
{
    const auto base = poolAdmissionCheckSet(kind);
    switch (context)
    {
    case AdmissionContext::PoolAdmission:
        return base;
    case AdmissionContext::EESTReplay:
        return base & ~(Check::Balance | Check::Web3NonceWindow);
    case AdmissionContext::ProposalVerification:
        // Everything a leader could violate stays ON. These are protocol invariants: their
        // answer is a function of the transaction and the chain config, so every honest node
        // computes the same one and enforcing them cannot split a block. ChainId in particular
        // MUST be here -- nothing downstream re-checks it (EthereumTransition.h: "validate_
        // transaction does not check that field"), so admission is the only gate standing
        // between a malicious leader and a transaction signed for another chain.
        //
        // Note this KEEPS Signature, and that it costs a real recovery per transaction: the
        // check clears the wire-supplied hash before verifying (otherwise a forged dataHash
        // binds a stolen signature to an attacker's body), so it cannot short-circuit on an
        // already-verified transaction. Paying it here is the price of not trusting a hash a
        // peer chose.
        //
        // FIVE come off, each for a different reason:
        //
        //   Balance -- its answer depends on WHERE in the block a transaction sits. One funded
        //   by an earlier transaction in the same proposal fails it against pre-block state.
        //
        //   SenderIsEOA and NonceNotMax -- correctness-neutral here (execution enforces both)
        //   but each pulls a full account read onto the consensus hot path.
        //
        //   Web3NonceWindow -- the "committed nonce" it compares against is not read from chain
        //   state first: it comes from Web3NonceChecker's in-process LRU cache, and the ledger
        //   is consulted only on a miss. That cache is raised from every Web3 transaction in a
        //   committed block regardless of whether that transaction SUCCEEDED, is
        //   capacity-bounded and evicts, and starts empty after a restart. Two honest nodes can
        //   therefore hold different values for the same sender, and the one with the
        //   stale-high value rejects a legitimate proposal. On this path a failed check fails
        //   the whole proposal and triggers a view change -- so a node-local cache disagreement
        //   becomes a liveness fault, repeated every time that leader proposes. Dropping it
        //   costs no safety: execution compares the nonce for exact equality, which is strictly
        //   stronger than a [n, n+600000] window.
        //
        //   BcosPoolNonce -- the set it consults is this node's PENDING transactions, the same
        //   class of node-local state as that cache. A leader's transactions are legitimately
        //   absent from it, and two honest nodes' pools differ by whatever each has admitted
        //   and not yet sealed, so a hit here is not something every honest node agrees on.
        //   The pool-side validator already skipped it on this path
        //   (checkTransaction(tx, onlyCheckLedgerNonce = true)). BcosLedgerNonce, the
        //   committed-nonce and blockLimit half, is chain state and stays.
        return base & ~(Check::Balance | Check::SenderIsEOA | Check::NonceNotMax |
                          Check::Web3NonceWindow | Check::BcosPoolNonce);
    }
    // See poolAdmissionCheckSet: unreachable for a real AdmissionContext, closed for anything
    // else.
    return Check::TypeGate;
}

constexpr Check effectiveCheckSet(
    TxKind kind, AdmissionContext context, SignaturePolicy policy) noexcept
{
    const auto set = checkSet(kind, context);
    return policy == SignaturePolicy::Required ? set :
                                                 set & ~(Check::Signature | c_senderDependent);
}

}  // namespace bcos::txvalidator
