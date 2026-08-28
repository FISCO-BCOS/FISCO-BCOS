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

/// Routing key. Decided from the SIGNED envelope's first byte during normalization, never from
/// the tars mirror -- a peer can set the mirror kind to 0 and a legacy-keyed check set would
/// then skip the fee-market rules a 1559 envelope is subject to.
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
    /// triggers a view change -- it does not reject one transaction. Balance and the nonce upper
    /// bound are local state that may legitimately differ from the leader's view, so running
    /// them here is a liveness risk, not a safety gain.
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
/// The verified/not-verified STATE is read only inside the Signature check itself, where
/// Transaction::verify() short-circuits on `if (!tainted()) return;`. That makes the check
/// idempotent, so no "already verified" flag has to be threaded through any call chain.
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
    Web3NonceWindow = 1U << 13,  ///< lower bound and queue depth: one account-nonce read
    InitCodeSize = 1U << 14,
    Balance = 1U << 15,
    IntrinsicGas = 1U << 16,
    BcosPoolNonce = 1U << 17,
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

/// The single evaluation order. The bitmask expresses membership only; this array decides which
/// code a transaction violating several rules reports -- and EEST fixtures assert specific error
/// codes, so that has to be deterministic.
///
/// Follows evmone's validate_transaction, with the FISCO-only items slotted in: TypeGate first
/// (nothing else is meaningful for an envelope type that is refused outright), ToFieldFormat and
/// Signature next (without a recovered sender the account-state checks cannot run), ChainId
/// ahead of the account reads (one config read is cheaper than an account read), BcosPoolNonce
/// last.
inline constexpr std::array c_checkOrder{
    Check::TypeGate,
    Check::ToFieldFormat,
    Check::Signature,
    Check::BcosGroupChainId,
    Check::TypeByRevision,
    Check::TipNotAboveCap,
    Check::SetCodeHasTo,
    Check::AuthListNonEmpty,
    Check::MaxGasLimit,
    Check::FeeCapVsBaseFee,
    Check::ChainId,
    Check::SenderIsEOA,
    Check::NonceNotMax,
    Check::Web3NonceWindow,
    Check::InitCodeSize,
    Check::Balance,
    Check::IntrinsicGas,
    Check::BcosPoolNonce,
};

/// Checks that cannot run without a recovered sender, and must therefore be skipped when
/// signature verification is switched off.
///
/// BcosPoolNonce is deliberately NOT here. A BCOS transaction's nonce is a global random value,
/// not an account sequence number: TxPoolNonceChecker::checkNonce reads only _tx.nonce() against
/// a global set and LedgerNonceChecker::checkBlockLimit reads only _tx.blockLimit(). Neither
/// touches the sender, so including it would make SignaturePolicy::Disabled silently switch off
/// BCOS replay protection as a side effect.
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
               Check::BcosPoolNonce;
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
    return Check::None;
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
        // Note this KEEPS Signature. The sync path's parallel loop already verified almost every
        // transaction, and Transaction::verify() returns immediately for those, so the ones it
        // skipped get verified serially here at no cost to the common case -- and no boolean has
        // to be carried along to say which is which.
        //
        // Three come off. Balance is the one check whose answer depends on WHERE in the block a
        // transaction sits -- a transaction funded by an earlier one in the same proposal fails
        // it against pre-block state. SenderIsEOA and NonceNotMax are correctness-neutral here
        // but would each pull a full account read (balance + code) onto the consensus hot path;
        // Web3NonceWindow stays because it reads the committed nonce alone, which is identical
        // on every node at the height being verified.
        return base & ~(Check::Balance | Check::SenderIsEOA | Check::NonceNotMax);
    }
    return Check::None;
}

constexpr Check effectiveCheckSet(
    TxKind kind, AdmissionContext context, SignaturePolicy policy) noexcept
{
    const auto set = checkSet(kind, context);
    return policy == SignaturePolicy::Required ? set :
                                                 set & ~(Check::Signature | c_senderDependent);
}

}  // namespace bcos::txvalidator
