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
 * @file TxValidator.cpp
 * @date 2026/8/25
 */

#include "bcos-tx-validator/TxValidator.h"
#include "bcos-framework/engine/RawTransactionDispatch.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/protocol/TxGasModel.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-rlp-protocol/Web3TxEnvelope.h"
#include "bcos-tx-validator/Normalize.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <cctype>
#include <stdexcept>

using namespace bcos;
using namespace bcos::protocol;

#define TX_VALIDATOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TXVALIDATOR")

namespace bcos::txvalidator
{
namespace
{
/// EIP-7702 delegation designator. Code with this prefix still belongs to an EOA, so EIP-3607
/// must let it send transactions.
constexpr std::array<byte, 3> kDelegationPrefix{0xef, 0x01, 0x00};

bool isDelegatedCode(bytes const& code) noexcept
{
    return code.size() >= kDelegationPrefix.size() &&
           std::equal(kDelegationPrefix.begin(), kDelegationPrefix.end(), code.begin());
}

/// Route on the SIGNED envelope, never on web3TypedTxKind: the mirror is unauthenticated, and a
/// peer that mis-declares a 1559 envelope as legacy would otherwise dodge TipNotAboveCap and
/// TypeByRevision.
TxKind kindOf(Transaction const& tx)
{
    if (tx.type() == static_cast<uint8_t>(TransactionType::BCOSTransaction))
    {
        return TxKind::Bcos;
    }
    switch (engine::dispatchRawTransaction(tx.extraTransactionBytes()))
    {
    case engine::RawTransactionKind::Legacy:
        return TxKind::Web3Legacy;
    case engine::RawTransactionKind::AccessList:
        return TxKind::Web3AccessList;
    case engine::RawTransactionKind::DynamicFee:
        return TxKind::Web3DynamicFee;
    case engine::RawTransactionKind::SetCode:
        return TxKind::Web3SetCode;
    default:
        return TxKind::Rejected;
    }
}

/// The hard fork each typed transaction requires.
evmc_revision requiredRevision(TxKind kind) noexcept
{
    switch (kind)
    {
    case TxKind::Web3AccessList:
        return EVMC_BERLIN;  // EIP-2930
    case TxKind::Web3DynamicFee:
        return EVMC_LONDON;  // EIP-1559
    case TxKind::Web3SetCode:
        return EVMC_PRAGUE;  // EIP-7702
    default:
        return EVMC_FRONTIER;
    }
}
}  // namespace

bool isValidToField(std::string_view toField)
{
    if (toField.empty() || g_BCOSConfig.isWasm())
    {
        return true;
    }
    if (toField.starts_with("0x") || toField.starts_with("0X"))
    {
        toField.remove_prefix(2);
    }
    constexpr size_t addressHexLength = 40;
    return toField.size() == addressHexLength &&
           std::ranges::all_of(
               toField, [](unsigned char character) { return std::isxdigit(character) != 0; });
}

TxValidator::TxValidator(crypto::CryptoSuite::Ptr cryptoSuite,
    std::shared_ptr<ledger::LedgerInterface> ledger,
    ledger::LedgerConfigState::Ptr ledgerConfigState, NonceCheckerInterface::Ptr txPoolNonceChecker,
    Web3NonceChecker::Ptr web3NonceChecker, SystemTxPredicate isSystemTx, std::string groupId,
    std::string chainId)
  : m_cryptoSuite(std::move(cryptoSuite)),
    m_ledger(std::move(ledger)),
    m_ledgerConfigState(std::move(ledgerConfigState)),
    m_txPoolNonceChecker(std::move(txPoolNonceChecker)),
    m_web3NonceChecker(std::move(web3NonceChecker)),
    m_isSystemTx(std::move(isSystemTx)),
    m_groupId(std::move(groupId)),
    m_chainId(std::move(chainId))
{
    if (!m_ledgerConfigState)
    {
        // Every revision-dependent check reads through this. A null holder would silently turn
        // all of them into stand-downs, which is exactly the "admission weaker than execution"
        // failure this module exists to prevent -- so refuse to be built that way.
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("TxValidator: ledgerConfigState must not be null"));
    }
}

void TxValidator::setLedgerNonceChecker(std::shared_ptr<LedgerNonceChecker> ledgerNonceChecker)
{
    WriteGuard guard(x_lateBound);
    m_ledgerNonceChecker = std::move(ledgerNonceChecker);
}

void TxValidator::setScheduler(std::weak_ptr<scheduler::SchedulerInterface> scheduler)
{
    WriteGuard guard(x_lateBound);
    m_scheduler = std::move(scheduler);
}

namespace
{
/// The inputs a check may read beyond the transaction itself. Each one costs a ledger or
/// storage round-trip, so admit() fetches one only when a check that actually runs declares it,
/// and never fetches the same one twice.
enum class Need : uint8_t
{
    None = 0,
    Config = 1U << 0,       ///< LedgerConfig (block number, gas limit, EVM revision)
    Revision = 1U << 1,     ///< evmc_revision for the next block; implies Config
    SenderState = 1U << 2,  ///< balance + nonce + code, one account read
    SenderNonce = 1U << 3,  ///< nonce alone; source depends on the context, see admit()
    TxGasPrice = 1U << 4,   ///< SYSTEM_KEY_TX_GAS_PRICE
    Web3ChainId = 1U << 5,  ///< SYSTEM_KEY_WEB3_CHAIN_ID
};

constexpr Need operator|(Need lhs, Need rhs) noexcept
{
    return static_cast<Need>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}
constexpr Need operator&(Need lhs, Need rhs) noexcept
{
    return static_cast<Need>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}
constexpr Need operator~(Need value) noexcept
{
    return static_cast<Need>(static_cast<uint8_t>(~static_cast<uint8_t>(value)));
}
constexpr bool contains(Need set, Need item) noexcept
{
    return (set & item) != Need::None;
}

/// Everything a check function is allowed to see. The resolved fields below are filled in by
/// admit() before the check runs, according to the Need mask its registry row declares -- so a
/// check body contains rules only, never I/O, and stays synchronous and directly testable.
struct AdmissionInputs
{
    protocol::Transaction& tx;
    TxKind kind;
    AdmissionContext context;
    /// The nonce checkers, as raw pointers. ledgerNonceChecker is a snapshot the verify() frame
    /// owns; txPoolNonceChecker points at a member of the validator, as do cryptoSuite,
    /// isSystemTx, groupId and chainId below. So this struct is only valid while the validator
    /// outlives the coroutine -- the ordinary contract for a Task-returning member, but worth
    /// stating because these are read AFTER resumption. Either checker may be null: see
    /// checkBcosPoolNonce and checkBcosLedgerNonce.
    NonceCheckerInterface* txPoolNonceChecker;
    LedgerNonceChecker* ledgerNonceChecker;
    crypto::CryptoSuite& cryptoSuite;
    SystemTxPredicate const& isSystemTx;
    std::string_view groupId;
    std::string_view chainId;

    // --- resolved on demand ---
    /// const: LedgerConfigState hands out a snapshot nobody may mutate, and a check receiving
    /// AdmissionInputs const& could otherwise still write through this pointer.
    std::shared_ptr<const ledger::LedgerConfig> config;
    /// nullopt = the chain declares no EVM revision; revision-dependent checks stand down rather
    /// than guess, because admission must never be STRICTER than execution.
    std::optional<evmc_revision> revision;
    /// nullopt = the account does not exist on chain yet.
    std::optional<AccountState> senderState;
    std::optional<u256> senderNonce;
    /// nullopt = the key is unset in the chain config (distinct from "not resolved": a field is
    /// only read by a check that declared the matching Need).
    std::optional<std::string> txGasPrice;
    std::optional<std::string> web3ChainId;
};

// ---------------------------------------------------------------- the checks
//
// One function per Check bit. Pure rules: no I/O, no co_await, no access to the validator's
// members beyond what AdmissionInputs carries. Returns None to pass, any other status to reject.

TransactionStatus checkTypeGate(AdmissionInputs const& in)
{
    // An allow-list, not a deny-list. normalize() already refused blob/deposit/reserved
    // envelopes, so Rejected here means the routing key and the gate disagree; and a value that
    // is no TxKind at all (which kindOf() cannot produce, but the table resolves to this gate
    // alone precisely so that such a value is judged by something) must fall through to the
    // rejection too. Fail closed either way.
    switch (in.kind)
    {
    case TxKind::Bcos:
    case TxKind::Web3Legacy:
    case TxKind::Web3AccessList:
    case TxKind::Web3DynamicFee:
    case TxKind::Web3SetCode:
        return TransactionStatus::None;
    case TxKind::Rejected:
        break;
    }
    return TransactionStatus::TxTypeNotSupported;
}

TransactionStatus checkToFieldFormat(AdmissionInputs const& in)
{
    return isValidToField(in.tx.to()) ? TransactionStatus::None : TransactionStatus::Malformed;
}

TransactionStatus checkSignature(AdmissionInputs const& in)
{
    try
    {
        // clearSenderAndHash() first, and it is NOT redundant with verify(). It wipes two things,
        // and only one of them is the sender:
        //
        //   - the wire-supplied hash. For a BCOS transaction, TarsHashable.h returns dataHash
        //     verbatim when it is non-empty, so calculateHash() inside verify() would recompute
        //     nothing and signature recovery would run against a hash the sender chose. Any
        //     (hash, signature) pair from a victim's broadcast transaction could then be attached
        //     to an attacker's body and recover the victim as sender.
        //   - the tainted flag. verify() early-returns on an untainted transaction, so without
        //     this a transaction that arrived pre-marked skips verification entirely.
        //
        // This costs one signature recovery per transaction on the sync path, which the pool-side
        // validator this replaces also paid. Trading it away for the branch below is what
        // reintroduces the bypass.
        in.tx.clearSenderAndHash();
        in.tx.verify(*in.cryptoSuite.hashImpl(), *in.cryptoSuite.signatureImpl());
    }
    catch (...)
    {
        // Catch-all, deliberately, and the same policy the pool-side validator this replaces
        // used: admission runs on unauthenticated input and must fail closed, and letting an
        // exception escape a coroutine the caller does not wrap would take the node down over
        // one bad transaction. The cost is that a genuine infrastructure fault -- a
        // misconfigured crypto suite, say -- is reported as InvalidSignature, so log the reason
        // rather than discard it: a burst of these with identical diagnostics is the signal
        // that the problem is the node, not the traffic. DEBUG because the frequency is
        // attacker-controlled.
        TX_VALIDATOR_LOG(DEBUG) << LOG_DESC("signature verification threw")
                                << LOG_KV(
                                       "reason", boost::current_exception_diagnostic_information());
        return TransactionStatus::InvalidSignature;
    }
    if (in.isSystemTx && in.isSystemTx(in.tx))
    {
        in.tx.setSystemTx(true);
    }
    return TransactionStatus::None;
}

TransactionStatus checkBcosGroupChainId(AdmissionInputs const& in)
{
    if (in.tx.groupId() != in.groupId) [[unlikely]]
    {
        return TransactionStatus::InvalidGroupId;
    }
    if (in.tx.chainId() != in.chainId) [[unlikely]]
    {
        return TransactionStatus::InvalidChainId;
    }
    return TransactionStatus::None;
}

TransactionStatus checkTypeByRevision(AdmissionInputs const& in)
{
    if (in.revision.has_value() && *in.revision < requiredRevision(in.kind))
    {
        return TransactionStatus::TxTypeNotSupported;
    }
    return TransactionStatus::None;
}

TransactionStatus checkTipNotAboveCap(AdmissionInputs const& in)
{
    return in.tx.maxPriorityFeePerGas() > in.tx.maxFeePerGas() ?
               TransactionStatus::TipGreaterThanFeeCap :
               TransactionStatus::None;
}

TransactionStatus checkSetCodeHasTo(AdmissionInputs const& in)
{
    return in.tx.to().empty() ? TransactionStatus::CreateSetCodeTx : TransactionStatus::None;
}

TransactionStatus checkAuthListNonEmpty(AdmissionInputs const& in)
{
    return in.tx.authorizationList().empty() ? TransactionStatus::EmptyAuthorizationList :
                                               TransactionStatus::None;
}

TransactionStatus checkMaxGasLimit(AdmissionInputs const& in)
{
    // gasLimit() is int64_t, but the Web3 envelope declares it as uint64: Web3TarsBridge assigns
    // it straight across, so a declared value >= 2^63 lands here negative. Both comparisons below
    // are signed, and a negative value passes them -- the EIP-7825 cap would miss the only inputs
    // that can exceed it. Reject up front so the reported status matches the rule that was
    // violated (EEST fixtures assert on the specific code, not just on rejection).
    if (in.tx.gasLimit() < 0) [[unlikely]]
    {
        return TransactionStatus::MaxGasLimitExceeded;
    }
    // Two caps in one item: the EIP-7825 constant from Osaka onwards, and the chain's
    // tx_gas_limit at all times. The optional comparison is deliberate -- a nullopt revision is
    // ordered below every value, so an unconfigured chain does not get the Osaka cap.
    if (in.revision >= EVMC_OSAKA && in.tx.gasLimit() > protocol::MAX_TX_GAS_LIMIT) [[unlikely]]
    {
        return TransactionStatus::MaxGasLimitExceeded;
    }
    // evmone compares against the block's remaining gas, which does not exist at admission;
    // FISCO has no block gas limit either -- SYSTEM_KEY_TX_GAS_LIMIT is a per-transaction cap.
    // This follows geth's head.GasLimit comparison instead.
    if (auto [limit, _] = in.config->gasLimit();
        limit > 0 && static_cast<uint64_t>(in.tx.gasLimit()) > limit)
    {
        return TransactionStatus::MaxGasLimitExceeded;
    }
    return TransactionStatus::None;
}

TransactionStatus checkFeeCapVsBaseFee(AdmissionInputs const& in)
{
    if (!in.txGasPrice)
    {
        return TransactionStatus::None;  // unset: no baseline to compare against
    }
    if (*in.txGasPrice == "0" || *in.txGasPrice == "0x0")
    {
        return TransactionStatus::None;  // free-gas chain
    }
    if (protocol::effectiveGasPrice(in.tx) < u256(*in.txGasPrice))
    {
        // Today this is reported as InsufficientFunds, which tells the user their balance is
        // short when it is not.
        return TransactionStatus::FeeCapLessThanBaseFee;
    }
    return TransactionStatus::None;
}

TransactionStatus checkChainId(AdmissionInputs const& in)
{
    // From the SIGNED envelope. The mirror cannot distinguish "no chainId" (pre-EIP-155) from
    // "chainId 0" -- both serialise to "0" -- and a typed transaction may legitimately carry an
    // explicit 0, which must still be compared.
    //
    // The three-way classifier, NOT web3ChainIdFromEnvelope: that walker collapses Unprotected
    // and Malformed into the same nullopt, and a gate built on it grants the pre-EIP-155
    // exemption to a legacy envelope whose v is neither 27/28 nor a valid EIP-155 value --
    // executing a transaction op-geth's signer would reject. The exemption must be fail-closed,
    // so it is granted only to an envelope positively classified as Unprotected.
    auto const classified =
        rlp::protocol::classifyWeb3EnvelopeChainId(in.tx.extraTransactionBytes());
    if (classified.kind == rlp::protocol::Web3EnvelopeChainIdKind::Malformed)
    {
        return TransactionStatus::InvalidChainId;
    }
    if (classified.kind == rlp::protocol::Web3EnvelopeChainIdKind::Unprotected)
    {
        // A pre-EIP-155 legacy transaction makes no chainId claim at all -- there is nothing to
        // compare it against, so this check has nothing to say about it.
        return TransactionStatus::None;
    }
    // The transaction claims a chain. From here the configuration is required: silently
    // accepting an unverifiable claim admits transactions signed for any chain -- which is what
    // EthEndpoint does today when web3_chain_id is unset.
    if (!in.web3ChainId)
    {
        return TransactionStatus::InvalidChainId;
    }
    auto const expected = ledger::parseWeb3ChainId(*in.web3ChainId);
    if (!expected || u256(classified.chainId) != *expected)
    {
        return TransactionStatus::InvalidChainId;
    }
    return TransactionStatus::None;
}

TransactionStatus checkSenderIsEOA(AdmissionInputs const& in)
{
    // EIP-3607. Delegated code (0xef0100...) still belongs to an EOA.
    if (in.senderState && !in.senderState->code.empty() && !isDelegatedCode(in.senderState->code))
    {
        return TransactionStatus::SenderNoEOA;
    }
    return TransactionStatus::None;
}

TransactionStatus checkNonceNotMax(AdmissionInputs const& in)
{
    // EIP-2681.
    if (in.senderState && in.senderState->nonce.has_value() &&
        *in.senderState->nonce >= std::numeric_limits<uint64_t>::max())
    {
        return TransactionStatus::NonceHasMaxValue;
    }
    return TransactionStatus::None;
}

TransactionStatus checkWeb3NonceWindow(AdmissionInputs const& in)
{
    // Lower bound and queue depth are one check: they share a single account-nonce read, and the
    // existing implementation expresses both in one comparison.
    if (!in.senderNonce.has_value())
    {
        // Account not on chain yet. The existing Web3NonceChecker also declines to judge in this
        // case (its storage-miss branch falls through without comparing), and matching it keeps
        // this a pure refactor. Whether an unknown account should instead be treated as nonce 0
        // is a separate question -- it would tighten queue-flooding behaviour.
        return TransactionStatus::None;
    }
    auto const txNonce = u256(in.tx.nonce());
    if (txNonce < *in.senderNonce)
    {
        return TransactionStatus::NonceCheckFail;  // already used
    }
    if (txNonce > *in.senderNonce + DEFAULT_WEB3_NONCE_CHECK_LIMIT)
    {
        return TransactionStatus::NonceCheckFail;  // too far ahead to queue
    }
    return TransactionStatus::None;
}

TransactionStatus checkInitCodeSize(AdmissionInputs const& in)
{
    // EIP-3860 applies to contract CREATION only. The current implementation keys on transaction
    // type alone, so a 60000-byte call to a deployed contract is wrongly rejected with
    // MaxInitCodeSizeExceeded.
    if (in.revision.has_value() && *in.revision >= EVMC_SHANGHAI && in.tx.to().empty() &&
        in.tx.input().size() > MAX_INITCODE_SIZE)
    {
        return TransactionStatus::MaxInitCodeSizeExceeded;
    }
    return TransactionStatus::None;
}

TransactionStatus checkBalance(AdmissionInputs const& in)
{
    // What the sender must be able to cover depends on whether this chain charges gas at all:
    //   tx_gas_price unset or "0"  -> gas is free; only `value` has to be covered
    //   tx_gas_price > 0           -> value + gasLimit * effectiveGasPrice
    // This mirrors the existing rule. It differs from evmone, which always charges
    // max_gas_price * gas_limit + value, because on a free-gas FISCO chain the sender is never
    // actually debited the fee cap they declared -- charging it at admission would reject
    // transactions that execute perfectly well.
    const bool chargesGas = in.txGasPrice && !(*in.txGasPrice == "0" || *in.txGasPrice == "0x0");

    u256 const balance = in.senderState ? in.senderState->balance : u256{0};

    // 512-bit, deliberately. bcos::u256 carries boost::multiprecision::unchecked, so
    // gasLimit * gasPrice + value is reduced mod 2^256 with no signal -- with a maxFeePerGas
    // near 2^256-1 the product comes back small and an unfundable transaction is admitted.
    // Widening FIRST is what makes this correct: two 256-bit operands multiply into at most 512
    // bits, so the u512 product cannot wrap even though u512 is itself `unchecked`.
    u512 required{in.tx.value()};
    if (chargesGas)
    {
        required += u512{in.tx.gasLimit()} * u512{protocol::effectiveGasPrice(in.tx)};
    }
    if (u512{balance} < required)
    {
        return TransactionStatus::InsufficientFunds;
    }
    return TransactionStatus::None;
}

TransactionStatus checkIntrinsicGas(AdmissionInputs const& in)
{
    // Same formula the executor uses (TxGasModel.h). A separate copy would drift at the next
    // fork that moves EIP-7623's floor, and the drift shows up as "admitted, then failed with
    // OutOfGasLimit" -- a block carrying a certainly-failing transaction.
    if (!in.revision.has_value())
    {
        // The intrinsic cost is revision-dependent (EIP-7623's floor, the calldata token price),
        // so without one there is no figure to compare against.
        return TransactionStatus::None;
    }
    auto const cost = protocol::gas::compute_tx_intrinsic_cost(*in.revision, in.tx);
    if (in.tx.gasLimit() < std::max(cost.intrinsic, cost.min))
    {
        return TransactionStatus::OutOfGasLimit;
    }
    return TransactionStatus::None;
}

// The two halves of the old pool-side TxValidator::checkTransaction, minus its Web3 branch (Web3
// nonce rules need only the account nonce and are their own check, Web3NonceWindow). They are
// separate bits because they answer to different state: the pool set is node-local and the
// proposal column drops it, the committed window is chain state and every column keeps it. That
// decision lives in CheckSet.h, so neither function looks at the context.

TransactionStatus checkBcosPoolNonce(AdmissionInputs const& in)
{
    if (in.txPoolNonceChecker == nullptr)
    {
        // No pool to be pending in (the mempool-side validator, or a test harness).
        return TransactionStatus::None;
    }
    return in.txPoolNonceChecker->checkNonce(in.tx);
}

TransactionStatus checkBcosLedgerNonce(AdmissionInputs const& in)
{
    if (in.ledgerNonceChecker == nullptr)
    {
        // Bound only once the pool has read the chain's block limit. Before that there is no
        // window to check against, and passing is correct: execution still enforces the nonce.
        return TransactionStatus::None;
    }
    return in.ledgerNonceChecker->checkNonce(in.tx);
}

// ---------------------------------------------------------------- the registry

struct CheckEntry
{
    Check bit;
    Need needs;
    TransactionStatus (*run)(AdmissionInputs const&);
};

/// Adding a check is three edits, and the compiler enforces two of them: write the function
/// above, add one row here declaring which inputs it reads, and slot the bit into c_checkOrder
/// (CheckSet.h) where it belongs in the evaluation order. The static_asserts below refuse to
/// compile if the registry and the order disagree in either direction.
constexpr std::array<CheckEntry, c_checkOrder.size()> c_checkRegistry{{
    {Check::TypeGate, Need::None, &checkTypeGate},
    {Check::ToFieldFormat, Need::None, &checkToFieldFormat},
    {Check::Signature, Need::None, &checkSignature},
    {Check::BcosGroupChainId, Need::None, &checkBcosGroupChainId},
    {Check::TypeByRevision, Need::Revision, &checkTypeByRevision},
    {Check::SetCodeHasTo, Need::None, &checkSetCodeHasTo},
    {Check::AuthListNonEmpty, Need::None, &checkAuthListNonEmpty},
    {Check::TipNotAboveCap, Need::None, &checkTipNotAboveCap},
    {Check::MaxGasLimit, Need::Revision | Need::Config, &checkMaxGasLimit},
    {Check::FeeCapVsBaseFee, Need::TxGasPrice, &checkFeeCapVsBaseFee},
    {Check::ChainId, Need::Web3ChainId, &checkChainId},
    {Check::SenderIsEOA, Need::SenderState, &checkSenderIsEOA},
    {Check::NonceNotMax, Need::SenderState, &checkNonceNotMax},
    {Check::Web3NonceWindow, Need::SenderNonce, &checkWeb3NonceWindow},
    {Check::InitCodeSize, Need::Revision, &checkInitCodeSize},
    {Check::Balance, Need::TxGasPrice | Need::SenderState, &checkBalance},
    {Check::IntrinsicGas, Need::Revision, &checkIntrinsicGas},
    {Check::BcosPoolNonce, Need::None, &checkBcosPoolNonce},
    {Check::BcosLedgerNonce, Need::None, &checkBcosLedgerNonce},
}};

constexpr CheckEntry const* findCheck(Check bit) noexcept
{
    for (auto const& entry : c_checkRegistry)
    {
        if (entry.bit == bit)
        {
            return &entry;
        }
    }
    return nullptr;
}

static_assert(
    [] {
        for (auto check : c_checkOrder)
        {
            if (findCheck(check) == nullptr)
            {
                return false;
            }
        }
        return true;
    }(),
    "a check listed in c_checkOrder has no implementation in c_checkRegistry");

static_assert(
    [] {
        for (auto const& entry : c_checkRegistry)
        {
            if (std::ranges::find(c_checkOrder, entry.bit) == c_checkOrder.end())
            {
                return false;
            }
        }
        return true;
    }(),
    "c_checkRegistry implements a check that c_checkOrder never runs");

}  // namespace

namespace
{
/// Both balance planes are written with u256::str({}, {}) -- decimal, no prefix -- so both are
/// read with the same parser. They used to disagree: the committed plane used u256(str), which
/// silently yields 0 on malformed input, while the pending plane used boost::lexical_cast, which
/// throws. One field with two parsers whose failure modes are opposites is how a discrepancy
/// becomes a wrong balance on one path and an exception on the other.
///
/// A malformed balance is corruption, not unavailability, and is reported as such: returning 0
/// would surface as InsufficientFunds and send the user hunting for funds they have.
u256 parseBalance(std::string_view raw)
{
    try
    {
        return boost::lexical_cast<u256>(raw);
    }
    catch (boost::bad_lexical_cast const&)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(
            "admission: account balance is not a parseable u256: '" + std::string(raw) + "'"));
    }
}
}  // namespace

task::Task<std::optional<AccountState>> TxValidator::readAccountState(std::string_view sender)
{
    AccountState state;

    // Committed plane, through the FIB-59 cache. A miss leaves nonce unset rather than 0: the
    // nonce window declines to judge an unknown account, and collapsing the two would start
    // rejecting first-time senders whose nonce sits beyond the window measured from zero.
    // Balance is read regardless -- an account absent from the committed plane may still hold a
    // pending balance.
    state.nonce = co_await m_web3NonceChecker->committedNonce(sender);

    std::shared_ptr<scheduler::SchedulerInterface> scheduler;
    {
        ReadGuard guard(x_lateBound);
        scheduler = m_scheduler.lock();
    }

    auto const senderHex = toHex(sender);
    if (!scheduler)
    {
        // No scheduler bound (engine-driven mode, or before the pool wires one): fall back to
        // the COMMITTED balance. The old pool validator described this fallback in a comment but
        // never performed it -- it logged and left the balance at 0, so every transaction came
        // back InsufficientFunds whenever the scheduler was missing.
        //
        // Contract code is unreadable without the scheduler, so `code` stays empty and EIP-3607
        // consequently passes. That weakens ONE check in a configuration whose alternative is
        // refusing all traffic; balance and nonce are still enforced.
        if (auto const storageState = co_await m_ledger->getStorageState(senderHex, 0))
        {
            if (auto const& balance = storageState.value().balance; !balance.empty())
            {
                state.balance = parseBalance(balance);
            }
        }
        co_return state;
    }

    auto const blockNumber = co_await ledger::getCurrentBlockNumber(*m_ledger);
    // Pending plane: the latest executed-but-uncommitted layer.
    if (auto entry = co_await scheduler->getPendingStorageAt(
            senderHex, ledger::ACCOUNT_TABLE_FIELDS::BALANCE, blockNumber))
    {
        if (auto const value = entry->get(); !value.empty())
        {
            state.balance = parseBalance(value);
        }
    }
    // Contract code is not read, so `code` stays empty and EIP-3607 (Check::SenderIsEOA) does
    // not fire at admission on any path. Execution still enforces it, so this is a missed early
    // rejection, not a hole: a contract-sender transaction occupies the pool until the executor
    // rejects it. The check is kept because the rule is correct and starts working the moment
    // this function fills the field.
    //
    // What it would take, since a previous version of this comment got it wrong: the code IS
    // reachable from here. ACCOUNT_TABLE_FIELDS::CODE_HASH and ::CODE go through the same
    // getPendingStorageAt coroutine used for BALANCE above -- SchedulerInterface::getCode is not
    // the only route, and no callback is involved.
    //
    // It is left undone because codeHash ALONE is not sufficient and would be wrong. An EIP-7702
    // delegated account has non-empty code and is still an EOA, so rejecting on
    // "codeHash != EMPTY_CODE_HASH" would refuse transactions the executor accepts -- admission
    // stricter than execution, the one failure this module exists to avoid. Doing it correctly
    // needs the code BYTES to test the 0xef0100 delegation prefix, i.e. a second read on the
    // rare non-empty-codeHash path. That is a behaviour change and belongs in its own commit.
    co_return state;
}

task::Task<TransactionStatus> TxValidator::verify(
    Transaction& tx, AdmissionContext context, SignaturePolicy policy)
{
    // A transaction already marked invalid failed normalization or signature recovery upstream
    // (TransactionSync's catch block is the only non-test setInvalid(true) caller), so its
    // mirror may not be trustworthy. Refuse before reading anything from it.
    if (tx.invalid()) [[unlikely]]
    {
        co_return TransactionStatus::InvalidSignature;
    }

    // Normalization is unconditional and precedes the whole check sequence: every check below
    // that reads a business field reads the mirror, and until this runs the mirror is whatever
    // the sender chose. It is not a Check bit precisely so that no context can switch it off and
    // nothing can order a mirror-reading check ahead of it.
    if (auto status = normalize(tx); status != TransactionStatus::None)
    {
        co_return status;
    }

    const auto kind = kindOf(tx);
    const auto checks = effectiveCheckSet(kind, context, policy);

    // Copied out under the lock and held for the rest of the call: the checks run synchronously
    // against this handle, and re-reading it mid-pass could have two checks judge the same
    // transaction against different windows.
    std::shared_ptr<LedgerNonceChecker> ledgerNonceChecker;
    {
        ReadGuard guard(x_lateBound);
        ledgerNonceChecker = m_ledgerNonceChecker;
    }

    // Every member is named, including the six resolved-on-demand ones. GCC's
    // -Wmissing-field-initializers (in -Wextra, and -Werror here) fires on a designated
    // initializer that skips a member with no default member initializer, even when the empty
    // state is the intended one, so the empties are spelled out.
    AdmissionInputs inputs{.tx = tx,
        .kind = kind,
        .context = context,
        .txPoolNonceChecker = m_txPoolNonceChecker.get(),
        .ledgerNonceChecker = ledgerNonceChecker.get(),
        .cryptoSuite = *m_cryptoSuite,
        .isSystemTx = m_isSystemTx,
        .groupId = m_groupId,
        .chainId = m_chainId,
        .config = nullptr,
        .revision = std::nullopt,
        .senderState = std::nullopt,
        .senderNonce = std::nullopt,
        .txGasPrice = std::nullopt,
        .web3ChainId = std::nullopt};

    // Which inputs have already been fetched for this transaction. Resolution is driven by the
    // registry rather than by each check, so two checks reading the same key (Balance and
    // FeeCapVsBaseFee both want tx_gas_price) cost one ledger read, not two.
    Need resolved = Need::None;

    for (auto check : c_checkOrder)
    {
        if (!contains(checks, check))
        {
            continue;
        }
        auto const* entry = findCheck(check);

        auto needs = entry->needs;
        if (contains(needs, Need::Revision))
        {
            needs = needs | Need::Config;  // the revision is read off the config
        }
        if (contains(needs, Need::SenderNonce) && context != AdmissionContext::ProposalVerification)
        {
            // Outside consensus the nonce comes from the full account read, which other checks
            // in the same pass want anyway; proposal verification would read the nonce alone.
            //
            // "would", because as the table stands the condition cannot be false here:
            // Web3NonceWindow is the only check declaring Need::SenderNonce, and
            // ProposalVerification strips it. The guard and the ProposalVerification arm below
            // are kept rather than deleted so that re-enabling that check for consensus does
            // not silently start pulling a full account read onto the consensus hot path --
            // which is the cost the routing table drops it to avoid.
            needs = needs | Need::SenderState;
        }
        const auto missing = needs & ~resolved;

        if (contains(missing, Need::Config))
        {
            // A snapshot, not a fetch: whoever commits a block republishes the configuration,
            // and this is a pointer copy. Holding it for the rest of the pass means every check
            // in one verify() call judges against a single block's configuration, even if a
            // commit lands mid-pass.
            inputs.config = m_ledgerConfigState->get();
            if (!inputs.config)
            {
                // LedgerConfigState never publishes null, so reaching here means the holder was
                // corrupted. Infrastructure, not a defect in the transaction -- reported as an
                // exception so it cannot masquerade as a rejected transaction.
                BOOST_THROW_EXCEPTION(
                    std::runtime_error("admission: ledger config state returned null"));
            }
        }
        if (contains(missing, Need::Revision))
        {
            // Judged against the block it would execute in, which is the next one.
            inputs.revision = inputs.config->evmcRevisionForBlock(inputs.config->blockNumber() + 1);
        }
        if (contains(missing, Need::SenderState))
        {
            inputs.senderState = co_await readAccountState(tx.sender());
        }
        if (contains(missing, Need::SenderNonce))
        {
            inputs.senderNonce =
                context == AdmissionContext::ProposalVerification ?
                    co_await m_web3NonceChecker->committedNonce(tx.sender()) :
                    (inputs.senderState ? inputs.senderState->nonce : std::optional<u256>{});
        }
        if (contains(missing, Need::TxGasPrice))
        {
            auto config =
                co_await ledger::getSystemConfig(*m_ledger, ledger::SYSTEM_KEY_TX_GAS_PRICE);
            inputs.txGasPrice =
                config ? std::optional{std::get<0>(config.value())} : std::optional<std::string>{};
        }
        if (contains(missing, Need::Web3ChainId))
        {
            auto config =
                co_await ledger::getSystemConfig(*m_ledger, ledger::SYSTEM_KEY_WEB3_CHAIN_ID);
            inputs.web3ChainId =
                config ? std::optional{std::get<0>(config.value())} : std::optional<std::string>{};
        }
        resolved = resolved | needs;

        if (auto status = entry->run(inputs); status != TransactionStatus::None)
        {
            co_return status;
        }
    }
    co_return TransactionStatus::None;
}

}  // namespace bcos::txvalidator
