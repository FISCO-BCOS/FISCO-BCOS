/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file Web3NonceChecker.cpp
 * @author: kyonGuo
 * @date 2024/8/26
 */

#include "Web3NonceChecker.h"
#include "../utilities/Common.h"
#include "bcos-framework/txpool/TxPoolTypeDef.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-protocol/TransactionStatus.h>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;

task::Task<bcos::protocol::TransactionStatus> Web3NonceChecker::checkWeb3Nonce(
    std::string sender, std::string nonce, bool onlyCheckLedgerNonce)
{
    // sender is bytes view
    auto const senderHex = toHex(sender);
    auto nonceU256 = u256(nonce);

    // Note:
    // 在以太坊中，nonce是从0开始的，也代表着该地址发交易次数。例如：在存储中存储的是5，那么web3工具从rpc
    // api获取的transactionCount就是5，那么新的交易将从5开始发。
    // Note: In Ethereum, the nonce starts from 0, which also represents the number of transactions
    // sent by the address. For example, if 5 is stored in the storage, then the transactionCount
    // obtained from the rpc api by the web3 tool is 5; then the new transaction will be sent
    // from 5.
    if (!onlyCheckLedgerNonce)
    {
        // memory nonce check nonce existence in memory first, if not exist, then check from storage
        if (co_await bcos::storage2::existsOne(m_memoryNonces, std::make_pair(sender, nonce)))
        {
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                TXPOOL_LOG(TRACE) << LOG_DESC("Web3Nonce: nonce mem check fail")
                                  << LOG_KV("sender", senderHex) << LOG_KV("nonce", nonce);
            }
            co_return TransactionStatus::NonceCheckFail;
        }
    }
    if (auto const nonceInLedger = co_await bcos::storage2::readOne(m_ledgerStateNonces, sender))
    {
        if (auto nonceInLedgerValue = nonceInLedger.value();
            nonceU256 < nonceInLedgerValue ||
            nonceU256 > nonceInLedgerValue + DEFAULT_WEB3_NONCE_CHECK_LIMIT)
        {
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                TXPOOL_LOG(TRACE) << LOG_DESC("Web3Nonce: nonce ledger check fail")
                                  << LOG_KV("sender", senderHex) << LOG_KV("nonce", nonce)
                                  << LOG_KV("nonceInLedger", nonceInLedgerValue);
            }
            co_return TransactionStatus::NonceCheckFail;
        }
    }
    // not in ledger memory, check from storage
    // TODO)): block number not use nowadays
    auto const storageState = co_await m_ledger->getStorageState(senderHex, 0);
    if (storageState.has_value())
    {
        // nonce in storage is uint string
        auto const nonceInStorage = u256(storageState.value().nonce);
        // update memory first
        co_await storage2::writeOne(m_ledgerStateNonces, sender, nonceInStorage);
        if (nonceU256 < nonceInStorage ||
            nonceU256 > nonceInStorage + DEFAULT_WEB3_NONCE_CHECK_LIMIT)
        {
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                TXPOOL_LOG(TRACE) << LOG_DESC("Web3Nonce: nonce storage check fail")
                                  << LOG_KV("sender", senderHex) << LOG_KV("nonce", nonce)
                                  << LOG_KV("nonceInStorage", nonceInStorage);
            }
            co_return TransactionStatus::NonceCheckFail;
        }
    }
    // TODO)): check balance？
    co_return TransactionStatus::None;
}

task::Task<TransactionStatus> Web3NonceChecker::checkWeb3Nonce(
    const bcos::protocol::Transaction& _tx, bool onlyCheckLedgerNonce)
{
    // sender is bytes view
    auto sender = std::string(_tx.sender());
    auto nonce = std::string(_tx.nonce());
    co_return co_await checkWeb3Nonce(std::move(sender), std::move(nonce), onlyCheckLedgerNonce);
}

task::Task<void> Web3NonceChecker::insertMemoryNonce(std::string sender, std::string nonce)
{
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        TXPOOL_LOG(TRACE) << LOG_DESC("write memory nonces") << LOG_KV("sender", toHex(sender))
                          << LOG_KV("nonce", nonce);
    }
    co_await storage2::writeOne(m_memoryNonces, std::make_pair(sender, nonce), std::monostate{});
    const auto maxMemNonce = co_await storage2::readOne(m_maxNonces, sender);
    if (auto const uNonce = u256(nonce); uNonce > maxMemNonce.value_or(0))
    {
        co_await storage2::writeOne(m_maxNonces, sender, uNonce);
    }
    co_return;
}

task::Task<std::optional<u256>> Web3NonceChecker::getPendingNonce(std::string_view sender)
{
    auto bytesSender =
        fromHex<std::string_view, std::string>(sender, sender.starts_with("0x") ? "0x" : "");
    if (auto nonce = co_await storage2::readOne(m_maxNonces, bytesSender))
    {
        co_return nonce;
    }

    if (auto ledgerNonce = co_await storage2::readOne(m_ledgerStateNonces, bytesSender))
    {
        co_return ledgerNonce;
    }

    co_return std::nullopt;
}

// only for test, inset nonce into ledgerStateNonces
void Web3NonceChecker::insert(std::string sender, u256 nonce)
{
    task::syncWait(storage2::writeOne(m_ledgerStateNonces, std::move(sender), nonce));
}