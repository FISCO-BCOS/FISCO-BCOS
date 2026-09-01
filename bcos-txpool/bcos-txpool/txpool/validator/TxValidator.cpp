/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief implementation of TxValidator
 * @file TxValidator.cpp
 * @author: yujiechen
 * @date 2021-05-11
 */
#include "TxValidator.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"  // SECP256K1_SIGNATURE_*_LEN
#include "bcos-framework/bcos-framework/engine/RawTransactionDispatch.h"
#include "bcos-framework/bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-rlp-protocol/Web3TxEnvelope.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/DataConvertUtility.h"

#include <cctype>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;

bool bcos::txpool::isValidToField(std::string_view toField)
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

TransactionStatus TxValidator::verify(bcos::protocol::Transaction& _tx)
{
    if (_tx.invalid()) [[unlikely]]
    {
        return TransactionStatus::InvalidSignature;
    }
    // Reject unknown transaction types to prevent signature binding bypass
    if (_tx.type() != static_cast<uint8_t>(TransactionType::BCOSTransaction) &&
        _tx.type() != static_cast<uint8_t>(TransactionType::Web3Transaction)) [[unlikely]]
    {
        return TransactionStatus::Malformed;
    }
    if (_tx.type() == static_cast<uint8_t>(TransactionType::BCOSTransaction))
    {
        // check groupId and chainId
        if (_tx.groupId() != m_groupId) [[unlikely]]
        {
            return TransactionStatus::InvalidGroupId;
        }
        if (_tx.chainId() != m_chainId) [[unlikely]]
        {
            return TransactionStatus::InvalidChainId;
        }
    }

    // should check the transaction signature first, because the sender of transaction will be force
    // remove in front module check signature
    try
    {
        // Defensively reset sender/hash and mark tx tainted so verify() always performs
        // signature recovery even when the input transaction was previously clean.
        _tx.clearSenderAndHash();
        _tx.verify(*m_cryptoSuite->hashImpl(), *m_cryptoSuite->signatureImpl());
        // P2P tars skips decode(); apply EIP-2 here. Empty sig (deposits) skips.
        // Shorter-than-65 signatures were already rejected by verify() above.
        if (_tx.type() == TransactionType::Web3Transaction)
        {
            auto const sig = _tx.signatureData();
            if (sig.size() == static_cast<size_t>(bcos::crypto::SECP256K1_SIGNATURE_LEN))
            {
                if (bcos::checkEip2Signature(
                        sig.getCroppedData(0, bcos::crypto::SECP256K1_SIGNATURE_R_LEN),
                        sig.getCroppedData(bcos::crypto::SECP256K1_SIGNATURE_R_LEN,
                            bcos::crypto::SECP256K1_SIGNATURE_S_LEN)) != nullptr)
                {
                    return TransactionStatus::InvalidSignature;
                }
            }
        }
    }
    catch (...)
    {
        return TransactionStatus::InvalidSignature;
    }

    // should check the transaction signature first, because sender is empty
    if (const auto status = checkTransaction(_tx); status != TransactionStatus::None)
    {
        return status;
    }

    if (isSystemTransaction(_tx))
    {
        _tx.setSystemTx(true);
    }
    // Nonce insertion is deferred to after all validation steps complete in
    // verifyAndSubmitTransaction(), so that a failure in validateTransaction() or
    // validateChainId() does not leave a leaked nonce in the pool (FIB-50)
    return TransactionStatus::None;
}

bcos::protocol::TransactionStatus TxValidator::checkTransaction(
    const bcos::protocol::Transaction& _tx, bool onlyCheckLedgerNonce)
{
    if (_tx.type() == static_cast<uint8_t>(TransactionType::Web3Transaction))
    {
        return checkWeb3Nonce(_tx, onlyCheckLedgerNonce);
    }
    // compare with nonces cached in memory, only check nonce in txpool
    if (!onlyCheckLedgerNonce)
    {
        if (auto status = checkTxpoolNonce(_tx); status != TransactionStatus::None)
        {
            return status;
        }
    }
    // check ledger nonce and block limit
    auto status = checkLedgerNonceAndBlockLimit(_tx);
    return status;
}


TransactionStatus TxValidator::checkLedgerNonceAndBlockLimit(const bcos::protocol::Transaction& _tx)
{
    // compare with nonces stored on-chain, and check block limit inside
    auto status = m_ledgerNonceChecker->checkNonce(_tx);
    if (status != TransactionStatus::None)
    {
        return status;
    }
    if (isSystemTransaction(_tx))
    {
        _tx.setSystemTx(true);
    }
    return TransactionStatus::None;
}

TransactionStatus TxValidator::checkTxpoolNonce(const bcos::protocol::Transaction& _tx)
{
    return m_txPoolNonceChecker->checkNonce(_tx);
}

bcos::protocol::TransactionStatus TxValidator::checkWeb3Nonce(
    const bcos::protocol::Transaction& _tx, bool onlyCheckLedgerNonce)
{
    if (_tx.type() != static_cast<uint8_t>(TransactionType::Web3Transaction)) [[likely]]
    {
        return TransactionStatus::None;
    }
    return task::syncWait(web3NonceChecker()->checkWeb3Nonce(_tx, onlyCheckLedgerNonce));
}

TransactionStatus TxValidator::validateTransaction(const bcos::protocol::Transaction& _tx)
{
    // Issue #5318: reject a malformed `to` at admission time so it can never reach a block.
    if (!isValidToField(_tx.to()))
    {
        TX_VALIDATOR_CHECKER_LOG(WARNING)
            << LOG_BADGE("ValidateTransaction") << LOG_DESC("RejectTransactionWithInvalidTo")
            << LOG_KV("to", _tx.to()) << LOG_KV("hash", _tx.hash().abridged());
        return TransactionStatus::Malformed;
    }
    // EIP-3860: Limit and meter initcode
    if (_tx.type() == TransactionType::Web3Transaction)
    {
        if (_tx.input().size() > MAX_INITCODE_SIZE)
        {
            TX_VALIDATOR_CHECKER_LOG(TRACE) << LOG_BADGE("ValidateTransaction")
                                            << LOG_DESC("RejectTransactionWithLargeInitCode")
                                            << LOG_KV("txSize", _tx.input().size())
                                            << LOG_KV("maxInitCodeSize", MAX_INITCODE_SIZE);
            // Reject transactions with initcode larger than MAX_INITCODE_SIZE
            return TransactionStatus::MaxInitCodeSizeExceeded;
        }
    }

    return TransactionStatus::None;
}

task::Task<TransactionStatus> TxValidator::validateBalance(
    const bcos::protocol::Transaction& _tx, std::shared_ptr<bcos::ledger::LedgerInterface> _ledger)
{
    if (_tx.type() != static_cast<uint8_t>(TransactionType::Web3Transaction))
    {
        co_return TransactionStatus::None;
    }
    auto sender = toHex(_tx.sender());

    u256 balanceValue{};

    // Try to get pending balance from scheduler first
    if (auto scheduler = m_scheduler.lock())
    {
        try
        {
            const auto currentBlockNumber = co_await ledger::getCurrentBlockNumber(*_ledger);
            if (const auto balanceEntry = co_await scheduler->getPendingStorageAt(
                    sender, ledger::ACCOUNT_TABLE_FIELDS::BALANCE, currentBlockNumber))
            {
                if (const auto balanceStr = balanceEntry->get(); !balanceStr.empty())
                {
                    balanceValue = boost::lexical_cast<u256>(balanceStr);
                    TX_VALIDATOR_CHECKER_LOG(TRACE)
                        << LOG_BADGE("ValidateTransactionWithState")
                        << LOG_DESC("Get balance from scheduler pending storage")
                        << LOG_KV("sender", sender) << LOG_KV("balance", balanceValue);
                }
            }
        }
        catch (std::exception const& e)
        {
            TX_VALIDATOR_CHECKER_LOG(WARNING)
                << LOG_BADGE("ValidateTransactionWithState")
                << LOG_DESC("Failed to get balance from scheduler, fallback to ledger")
                << LOG_KV("error", boost::diagnostic_information(e));
        }
    }
    // Gas price config handling:
    // - config set to 0  → skip all balance checks (free-gas chain)
    // - config unset     → only check value (no baseline to validate gas cost against)
    // - config set > 0   → FIB-75: also validate tx.effectiveGasPrice >= config and
    //                       include gasLimit * effectiveGasPrice in required amount
    bool skipBalanceCheck = false;
    u256 systemGasPrice{0};
    if (auto gasPriceConfig =
            co_await ledger::getSystemConfig(*_ledger, ledger::SYSTEM_KEY_TX_GAS_PRICE))
    {
        auto& [gasPriceStr, blockNumber] = gasPriceConfig.value();
        if (gasPriceStr == "0x0" || gasPriceStr == "0")
        {
            skipBalanceCheck = true;
            TX_VALIDATOR_CHECKER_LOG(TRACE) << LOG_BADGE("validateBalance")
                                            << LOG_DESC("Skip balance check due to zero gas price")
                                            << LOG_KV("gasPrice", gasPriceStr);
        }
        else
        {
            systemGasPrice = u256(gasPriceStr);
        }
    }
    if (!skipBalanceCheck)
    {
        u256 gasCost{0};
        // Only validate gas price / gas cost when systemGasPrice is configured (> 0)
        if (systemGasPrice > 0)
        {
            // effectiveGasPrice() handles legacy (gasPrice field) and EIP-1559 (maxFeePerGas)
            const auto txGasPrice = protocol::effectiveGasPrice(_tx);
            if (txGasPrice < systemGasPrice)
            {
                TX_VALIDATOR_CHECKER_LOG(TRACE)
                    << LOG_BADGE("ValidateTransactionWithState")
                    << LOG_DESC("tx gasPrice below system minimum") << LOG_KV("sender", sender)
                    << LOG_KV("txGasPrice", txGasPrice) << LOG_KV("systemGasPrice", systemGasPrice);
                co_return TransactionStatus::InsufficientFunds;
            }
            if (_tx.gasLimit() > 0)
            {
                gasCost = u256(_tx.gasLimit()) * txGasPrice;
            }
        }

        auto txValue = _tx.value();
        if (auto totalRequired = txValue + gasCost;
            balanceValue < totalRequired || balanceValue == 0)
        {
            TX_VALIDATOR_CHECKER_LOG(TRACE)
                << LOG_BADGE("ValidateTransactionWithState") << LOG_DESC("InsufficientFunds")
                << LOG_KV("sender", sender) << LOG_KV("balance", balanceValue)
                << LOG_KV("txValue", txValue) << LOG_KV("gasCost", gasCost)
                << LOG_KV("totalRequired", totalRequired);
            co_return TransactionStatus::InsufficientFunds;
        }
    }

    co_return TransactionStatus::None;
}

task::Task<protocol::TransactionStatus> TxValidator::validateChainId(
    const bcos::protocol::Transaction& _tx, std::shared_ptr<bcos::ledger::LedgerInterface> _ledger)
{
    if (_tx.type() != TransactionType::Web3Transaction)
    {
        co_return TransactionStatus::None;
    }
    // Same classifier as sendRawTransaction / the executor.
    namespace rlp_protocol = bcos::rlp::protocol;
    auto const classified = rlp_protocol::classifyWeb3EnvelopeChainId(_tx.extraTransactionBytes());
    // Same first-byte table as sendRaw / engine payload: Blob is parseable but never
    // admitted on L2; Unsupported (empty extra, 0x00, 0x05–0x7d, 0x7f–0xbf) is not a
    // Web3 envelope at all. P2P tars does not call decode(), so this is the gate.
    // BlobTxNotAllowed says WHY a blob was refused; Unsupported is Malformed, matching
    // verify()'s unknown-type rejection. Deposit/classifier-Malformed/mismatch stay
    // InvalidChainId.
    auto const kind = bcos::engine::dispatchRawTransaction(_tx.extraTransactionBytes());
    if (kind == bcos::engine::RawTransactionKind::Blob)
    {
        co_return TransactionStatus::BlobTxNotAllowed;
    }
    if (kind == bcos::engine::RawTransactionKind::Unsupported)
    {
        co_return TransactionStatus::Malformed;
    }
    // Deposits have no chainId; they enter via the rollup pipeline, not the pool.
    if (classified.kind == rlp_protocol::Web3EnvelopeChainIdKind::Deposit)
    {
        co_return TransactionStatus::InvalidChainId;
    }
    if (classified.kind == rlp_protocol::Web3EnvelopeChainIdKind::Malformed)
    {
        // Fail closed: an unreadable chainId/v must not ride the unprotected exemption.
        co_return TransactionStatus::InvalidChainId;
    }
    // Same fail-closed rules as EthEndpoint::sendRawTransaction: missing/unparsable
    // web3_chain_id must not fall through to None for anything that BINDS a chainId.
    // Only pre-EIP-155 unprotected legacy (classifier: Unprotected) is exempt.
    if (auto config = co_await ledger::getSystemConfig(*_ledger, ledger::SYSTEM_KEY_WEB3_CHAIN_ID))
    {
        auto [chainId, _] = config.value();
        // Same parseWeb3ChainId as sendRawTransaction (decimal or 0x).
        auto expected = ledger::parseWeb3ChainId(chainId);
        if (!expected.has_value())
        {
            co_return TransactionStatus::InvalidChainId;
        }
        if (classified.kind == rlp_protocol::Web3EnvelopeChainIdKind::Protected &&
            bcos::u256(classified.chainId) != *expected)
        {
            co_return TransactionStatus::InvalidChainId;
        }
        co_return TransactionStatus::None;
    }
    if (classified.kind == rlp_protocol::Web3EnvelopeChainIdKind::Protected)
    {
        co_return TransactionStatus::InvalidChainId;
    }
    co_return TransactionStatus::None;
}
