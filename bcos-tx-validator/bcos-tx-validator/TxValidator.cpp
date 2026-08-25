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
#include <cctype>

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
        }
        co_return ledgerConfig;
    };
    std::optional<evmc_revision> revision;
    const auto rev = [&]() -> task::Task<evmc_revision> {
        if (!revision)
        {
            auto cfg = co_await config();
            // Admission judges a transaction against the block it would be executed in, which is
            // the next one, not the last one.
            revision = cfg->evmcRevisionForBlock(cfg->blockNumber() + 1).value_or(EVMC_FRONTIER);
        }
        co_return *revision;
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
            if (co_await rev() < requiredRevision(kind))
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
            auto chainIdConfig =
                co_await ledger::getSystemConfig(*m_ledger, ledger::SYSTEM_KEY_WEB3_CHAIN_ID);
            if (!chainIdConfig)
            {
                // Fail closed. Silently skipping the check when the chain is not configured
                // would accept transactions signed for any chain.
                co_return TransactionStatus::InvalidChainId;
            }
            auto const expected = ledger::parseWeb3ChainId(std::get<0>(chainIdConfig.value()));
            if (!expected)
            {
                co_return TransactionStatus::InvalidChainId;
            }
            // From the SIGNED envelope. The mirror cannot distinguish "no chainId"
            // (pre-EIP-155) from "chainId 0" -- both serialise to "0" -- and a typed
            // transaction may legitimately carry an explicit 0, which must still be compared.
            auto const envelopeChainId =
                rlp::protocol::web3ChainIdFromEnvelope(tx.extraTransactionBytes());
            if (!envelopeChainId)
            {
                // Absent is legitimate only for unprotected pre-EIP-155 legacy; a typed
                // envelope always carries one, so a missing value there is malformed.
                if (rlp::protocol::isTypedWeb3Envelope(tx.extraTransactionBytes()))
                {
                    co_return TransactionStatus::InvalidChainId;
                }
                break;
            }
            if (u256(*envelopeChainId) != *expected)
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
            if (state && state->nonce >= std::numeric_limits<uint64_t>::max())
            {
                co_return TransactionStatus::NonceHasMaxValue;
            }
            break;
        }
        case Check::Web3NonceWindow:
        {
            // Lower bound and queue depth are one check: they share a single account-nonce read
            // and the existing implementation expresses them in one comparison.
            u256 accountNonce{0};
            if (context == AdmissionContext::ProposalVerification)
            {
                // Consensus hot path: nonce only, no balance and no contract code.
                if (auto nonce = co_await m_accountNonceReader(tx.sender()))
                {
                    accountNonce = *nonce;
                }
            }
            else if (auto state = co_await senderState())
            {
                accountNonce = state->nonce;
            }
            auto const txNonce = u256(tx.nonce());
            if (txNonce < accountNonce)
            {
                co_return TransactionStatus::NonceCheckFail;  // already used
            }
            if (txNonce > accountNonce + DEFAULT_WEB3_NONCE_CHECK_LIMIT)
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
            if (co_await rev() >= EVMC_SHANGHAI && tx.to().empty() &&
                tx.input().size() > MAX_INITCODE_SIZE)
            {
                co_return TransactionStatus::MaxInitCodeSizeExceeded;
            }
            break;
        }
        case Check::Balance:
        {
            auto gasPriceConfig =
                co_await ledger::getSystemConfig(*m_ledger, ledger::SYSTEM_KEY_TX_GAS_PRICE);
            if (gasPriceConfig)
            {
                auto const& [gasPriceStr, _] = gasPriceConfig.value();
                if (gasPriceStr == "0" || gasPriceStr == "0x0")
                {
                    break;  // free-gas chain: nothing to pay for
                }
            }
            auto state = co_await senderState();
            u256 const balance = state ? state->balance : u256{0};
            // 512-bit, deliberately. bcos::u256 carries boost::multiprecision::unchecked, so
            // gasLimit * gasPrice + value wraps SILENTLY on it -- with maxFeePerGas near 2^256-1
            // the product comes back small and an unfundable transaction is admitted. Execution
            // already computes this with intx::umul; admission has to use the same width.
            auto maxTotalFee = intx::umul(intx::uint256{static_cast<uint64_t>(tx.gasLimit())},
                protocol::toIntxU256(protocol::effectiveGasPrice(tx)));
            maxTotalFee += intx::uint512{protocol::toIntxU256(tx.value())};
            if (intx::uint512{protocol::toIntxU256(balance)} < maxTotalFee)
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
            auto const cost = protocol::compute_tx_intrinsic_cost(co_await rev(), tx);
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
