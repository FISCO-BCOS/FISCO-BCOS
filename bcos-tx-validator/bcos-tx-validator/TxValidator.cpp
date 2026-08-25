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
#include <boost/exception/diagnostic_information.hpp>
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
    std::shared_ptr<ledger::LedgerInterface> ledger, LedgerConfigProvider ledgerConfigProvider,
    AccountStateReader accountStateReader, AccountNonceReader accountNonceReader,
    SystemTxPredicate isSystemTx, std::string groupId, std::string chainId)
  : m_cryptoSuite(std::move(cryptoSuite)),
    m_ledger(std::move(ledger)),
    m_ledgerConfigProvider(std::move(ledgerConfigProvider)),
    m_accountStateReader(std::move(accountStateReader)),
    m_accountNonceReader(std::move(accountNonceReader)),
    m_isSystemTx(std::move(isSystemTx)),
    m_groupId(std::move(groupId)),
    m_chainId(std::move(chainId))
{}

task::Task<TransactionStatus> TxValidator::admit(
    Transaction& tx, AdmissionContext context, SignaturePolicy policy, PoolNonceQuery const& pool)
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

    // Lazily fetched: several checks need them, many transactions need none of them.
    ledger::LedgerConfig::Ptr ledgerConfig;
    const auto config = [&]() -> task::Task<ledger::LedgerConfig::Ptr> {
        if (!ledgerConfig)
        {
            ledgerConfig = co_await m_ledgerConfigProvider();
            if (!ledgerConfig)
            {
                // Infrastructure, not a defect in the transaction: without the chain's config
                // the revision and gas cap are unknown. Reported as an exception rather than a
                // status so a wiring or storage problem cannot masquerade as a rejected
                // transaction (and so it is never silently dereferenced).
                BOOST_THROW_EXCEPTION(
                    std::runtime_error("admission: ledger config provider returned null"));
            }
        }
        co_return ledgerConfig;
    };
    std::optional<evmc_revision> revision;
    bool revisionResolved = false;
    // std::nullopt means the chain declares no EVM revision. The four checks below that depend
    // on one then stand down rather than guessing: guessing low (FRONTIER) would reject every
    // typed transaction on such a chain, and admission must never be STRICTER than execution.
    // A chain in that state cannot execute Web3 transactions at all -- EthereumExecutor throws
    // EvmcRevisionNotConfigured -- so letting them into the pool costs nothing and keeps
    // admission advisory, with execution authoritative.
    const auto rev = [&]() -> task::Task<std::optional<evmc_revision>> {
        if (!revisionResolved)
        {
            auto cfg = co_await config();
            // Judged against the block it would execute in, which is the next one.
            revision = cfg->evmcRevisionForBlock(cfg->blockNumber() + 1);
            revisionResolved = true;
        }
        co_return revision;
    };
    std::optional<AccountState> account;
    bool accountLoaded = false;
    const auto senderState = [&]() -> task::Task<std::optional<AccountState>> {
        if (!accountLoaded)
        {
            account = co_await m_accountStateReader(tx.sender());
            accountLoaded = true;
        }
        co_return account;
    };

    for (auto check : c_checkOrder)
    {
        if (!contains(checks, check))
        {
            continue;
        }
        switch (check)
        {
        case Check::TypeGate:
        {
            // normalize() already refused blob/deposit/reserved envelopes, so reaching here with
            // Rejected means the routing key and the gate disagree -- fail closed.
            if (kind == TxKind::Rejected) [[unlikely]]
            {
                co_return TransactionStatus::TxTypeNotSupported;
            }
            break;
        }
        case Check::ToFieldFormat:
        {
            if (!isValidToField(tx.to()))
            {
                co_return TransactionStatus::Malformed;
            }
            break;
        }
        case Check::Signature:
        {
            try
            {
                // Idempotent: verify() returns immediately when the transaction is no longer
                // tainted, so a transaction the sync path already verified costs one bool here.
                tx.verify(*m_cryptoSuite->hashImpl(), *m_cryptoSuite->signatureImpl());
            }
            catch (...)
            {
                co_return TransactionStatus::InvalidSignature;
            }
            if (m_isSystemTx && m_isSystemTx(tx))
            {
                tx.setSystemTx(true);
            }
            break;
        }
        case Check::BcosGroupChainId:
        {
            if (tx.groupId() != m_groupId) [[unlikely]]
            {
                co_return TransactionStatus::InvalidGroupId;
            }
            if (tx.chainId() != m_chainId) [[unlikely]]
            {
                co_return TransactionStatus::InvalidChainId;
            }
            break;
        }
        case Check::TypeByRevision:
        {
            if (auto revision = co_await rev();
                revision.has_value() && *revision < requiredRevision(kind))
            {
                co_return TransactionStatus::TxTypeNotSupported;
            }
            break;
        }
        case Check::TipNotAboveCap:
        {
            if (tx.maxPriorityFeePerGas() > tx.maxFeePerGas())
            {
                co_return TransactionStatus::TipGreaterThanFeeCap;
            }
            break;
        }
        case Check::SetCodeHasTo:
        {
            if (tx.to().empty())
            {
                co_return TransactionStatus::CreateSetCodeTx;
            }
            break;
        }
        case Check::AuthListNonEmpty:
        {
            if (tx.authorizationList().empty())
            {
                co_return TransactionStatus::EmptyAuthorizationList;
            }
            break;
        }
        case Check::MaxGasLimit:
        {
            // Two caps in one item: the EIP-7825 constant from Osaka onwards, and the chain's
            // tx_gas_limit at all times.
            if (co_await rev() >= EVMC_OSAKA && tx.gasLimit() > protocol::MAX_TX_GAS_LIMIT)
                [[unlikely]]
            {
                co_return TransactionStatus::MaxGasLimitExceeded;
            }
            // evmone compares against the block's remaining gas, which does not exist at
            // admission; FISCO has no block gas limit either -- SYSTEM_KEY_TX_GAS_LIMIT is a
            // per-transaction cap. This follows geth's head.GasLimit comparison instead.
            if (auto [limit, _] = (co_await config())->gasLimit();
                limit > 0 && static_cast<uint64_t>(tx.gasLimit()) > limit)
            {
                co_return TransactionStatus::MaxGasLimitExceeded;
            }
            break;
        }
        case Check::FeeCapVsBaseFee:
        {
            auto gasPriceConfig =
                co_await ledger::getSystemConfig(*m_ledger, ledger::SYSTEM_KEY_TX_GAS_PRICE);
            if (!gasPriceConfig)
            {
                break;  // unset: no baseline to compare against
            }
            auto const& [gasPriceStr, _] = gasPriceConfig.value();
            if (gasPriceStr == "0" || gasPriceStr == "0x0")
            {
                break;  // free-gas chain
            }
            if (protocol::effectiveGasPrice(tx) < u256(gasPriceStr))
            {
                // Today this is reported as InsufficientFunds, which tells the user their
                // balance is short when it is not.
                co_return TransactionStatus::FeeCapLessThanBaseFee;
            }
            break;
        }
        case Check::ChainId:
        {
            // From the SIGNED envelope. The mirror cannot distinguish "no chainId"
            // (pre-EIP-155) from "chainId 0" -- both serialise to "0" -- and a typed
            // transaction may legitimately carry an explicit 0, which must still be compared.
            auto const envelopeChainId =
                rlp::protocol::web3ChainIdFromEnvelope(tx.extraTransactionBytes());
            if (!envelopeChainId)
            {
                // A typed envelope always carries a chainId, so a missing one there is
                // malformed. An unprotected pre-EIP-155 legacy transaction makes no chainId
                // claim at all -- there is nothing to compare it against, so this check has
                // nothing to say about it and the chain's configuration is irrelevant.
                if (rlp::protocol::isTypedWeb3Envelope(tx.extraTransactionBytes()))
                {
                    co_return TransactionStatus::InvalidChainId;
                }
                break;
            }
            // The transaction claims a chain. From here the configuration is required: without
            // it the claim cannot be checked, and silently accepting an unverifiable claim
            // admits transactions signed for any chain -- which is what EthEndpoint does today
            // when web3_chain_id is unset.
            auto chainIdConfig =
                co_await ledger::getSystemConfig(*m_ledger, ledger::SYSTEM_KEY_WEB3_CHAIN_ID);
            if (!chainIdConfig)
            {
                co_return TransactionStatus::InvalidChainId;
            }
            auto const expected = ledger::parseWeb3ChainId(std::get<0>(chainIdConfig.value()));
            if (!expected || u256(*envelopeChainId) != *expected)
            {
                co_return TransactionStatus::InvalidChainId;
            }
            break;
        }
        case Check::SenderIsEOA:
        {
            auto state = co_await senderState();
            // EIP-3607. Delegated code (0xef0100...) still belongs to an EOA.
            if (state && !state->code.empty() && !isDelegatedCode(state->code))
            {
                co_return TransactionStatus::SenderNoEOA;
            }
            break;
        }
        case Check::NonceNotMax:
        {
            auto state = co_await senderState();
            // EIP-2681.
            if (state && state->nonce.has_value() &&
                *state->nonce >= std::numeric_limits<uint64_t>::max())
            {
                co_return TransactionStatus::NonceHasMaxValue;
            }
            break;
        }
        case Check::Web3NonceWindow:
        {
            // Lower bound and queue depth are one check: they share a single account-nonce read,
            // and the existing implementation expresses both in one comparison.
            std::optional<u256> accountNonce;
            if (context == AdmissionContext::ProposalVerification)
            {
                // Consensus hot path: nonce only, no balance and no contract code.
                accountNonce = co_await m_accountNonceReader(tx.sender());
            }
            else if (auto state = co_await senderState())
            {
                accountNonce = state->nonce;
            }
            if (!accountNonce.has_value())
            {
                // Account not on chain yet. The existing Web3NonceChecker also declines to
                // judge in this case (its storage-miss branch falls through without comparing),
                // and matching it keeps this a pure refactor. Whether an unknown account should
                // instead be treated as nonce 0 is a separate question -- it would tighten
                // queue-flooding behaviour, and it is not part of this change.
                break;
            }
            auto const txNonce = u256(tx.nonce());
            if (txNonce < *accountNonce)
            {
                co_return TransactionStatus::NonceCheckFail;  // already used
            }
            if (txNonce > *accountNonce + DEFAULT_WEB3_NONCE_CHECK_LIMIT)
            {
                co_return TransactionStatus::NonceCheckFail;  // too far ahead to queue
            }
            break;
        }
        case Check::InitCodeSize:
        {
            // EIP-3860 applies to contract CREATION only. The current implementation keys on
            // transaction type alone, so a 60000-byte call to a deployed contract is wrongly
            // rejected with MaxInitCodeSizeExceeded.
            if (auto revision = co_await rev(); revision.has_value() &&
                                                *revision >= EVMC_SHANGHAI && tx.to().empty() &&
                                                tx.input().size() > MAX_INITCODE_SIZE)
            {
                co_return TransactionStatus::MaxInitCodeSizeExceeded;
            }
            break;
        }
        case Check::Balance:
        {
            // What the sender must be able to cover depends on whether this chain charges gas
            // at all:
            //   tx_gas_price unset or "0"  -> gas is free; only `value` has to be covered
            //   tx_gas_price > 0           -> value + gasLimit * effectiveGasPrice
            // This mirrors the existing rule. It differs from evmone, which always charges
            // max_gas_price * gas_limit + value, because on a free-gas FISCO chain the sender
            // is never actually debited the fee cap they declared -- charging it at admission
            // would reject transactions that execute perfectly well.
            bool chargesGas = false;
            if (auto gasPriceConfig =
                    co_await ledger::getSystemConfig(*m_ledger, ledger::SYSTEM_KEY_TX_GAS_PRICE))
            {
                auto const& [gasPriceStr, _] = gasPriceConfig.value();
                chargesGas = !(gasPriceStr == "0" || gasPriceStr == "0x0");
            }

            auto state = co_await senderState();
            u256 const balance = state ? state->balance : u256{0};

            // 512-bit, deliberately. bcos::u256 carries boost::multiprecision::unchecked, so
            // gasLimit * gasPrice + value is reduced mod 2^256 with no signal -- with a
            // maxFeePerGas near 2^256-1 the product comes back small and an unfundable
            // transaction is admitted. Execution already computes this with intx::umul.
            intx::uint512 required{protocol::toIntxU256(tx.value())};
            if (chargesGas)
            {
                required += intx::umul(intx::uint256{static_cast<uint64_t>(tx.gasLimit())},
                    protocol::toIntxU256(protocol::effectiveGasPrice(tx)));
            }
            if (intx::uint512{protocol::toIntxU256(balance)} < required)
            {
                co_return TransactionStatus::InsufficientFunds;
            }
            break;
        }
        case Check::IntrinsicGas:
        {
            // Same formula the executor uses (TxGasModel.h). A separate copy would drift at the
            // next fork that moves EIP-7623's floor, and the drift shows up as "admitted, then
            // failed with OutOfGasLimit" -- a block carrying a certainly-failing transaction.
            auto const revision = co_await rev();
            if (!revision.has_value())
            {
                // The intrinsic cost is revision-dependent (EIP-7623's floor, the calldata token
                // price), so without one there is no figure to compare against.
                break;
            }
            auto const cost = protocol::compute_tx_intrinsic_cost(*revision, tx);
            if (tx.gasLimit() < std::max(cost.intrinsic, cost.min))
            {
                co_return TransactionStatus::OutOfGasLimit;
            }
            break;
        }
        case Check::BcosPoolNonce:
        {
            if (!pool.checkBcosNonce) [[unlikely]]
            {
                // Wiring error, not a defect in the transaction.
                TX_VALIDATOR_LOG(ERROR)
                    << LOG_DESC("BcosPoolNonce is in the check set but no query was wired");
                co_return TransactionStatus::Unknown;
            }
            if (auto status =
                    pool.checkBcosNonce(tx, context == AdmissionContext::ProposalVerification);
                status != TransactionStatus::None)
            {
                co_return status;
            }
            break;
        }
        case Check::None:
            break;
        }
    }
    co_return TransactionStatus::None;
}

}  // namespace bcos::txvalidator
