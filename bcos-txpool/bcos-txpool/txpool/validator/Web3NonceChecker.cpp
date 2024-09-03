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
#include <bcos-framework/storage2/Storage.h>
#include <utilities/Common.h>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;

task::Task<TransactionStatus> Web3NonceChecker::checkWeb3Nonce(
    Transaction::ConstPtr _tx, bool onlyCheckLedgerNonce)
{
    // sender is bytes view
    auto sender = std::string(_tx->sender());
    auto nonce = u256(_tx->nonce());

    // Note:
    // 在以太坊中，nonce是从0开始的，也代表着该地址发交易次数。例如：在存储中存储的是5，那么web3工具从rpc
    // api获取的transactionCount就是5，那么新的交易将从5开始发。
    // Note: In Ethereum, the nonce starts from 0, which also represents the number of transactions
    // sent by the address. For example: if 5 is stored in the storage, then the transactionCount
    // obtained from the rpc api by the web3 tool is 5, then the new transaction will be sent
    // from 5.
    if (!onlyCheckLedgerNonce)
    {
        if (auto const nonceInMem = co_await bcos::storage2::readOne(m_memoryNonces, sender))
        {
            if (auto nonceInMemValue = nonceInMem.value();
                nonce <= nonceInMemValue ||
                nonce > nonceInMemValue + DEFAULT_WEB3_NONCE_CHECK_LIMIT)
            {
                co_return TransactionStatus::NonceCheckFail;
            }
            else
            {
                // update memory if nonce bigger than memory's
                co_await storage2::writeOne(m_memoryNonces, sender, nonce);
                co_return TransactionStatus::None;
            }
        }
    }
    if (auto const nonceInLedger = co_await bcos::storage2::readOne(m_ledgerStateNonces, sender))
    {
        if (auto nonceInLedgerValue = nonceInLedger.value();
            nonce < nonceInLedgerValue ||
            nonce > nonceInLedgerValue + DEFAULT_WEB3_NONCE_CHECK_LIMIT)
        {
            co_return TransactionStatus::NonceCheckFail;
        }
        else
        {
            // update memory if nonce bigger than memory's
            co_await storage2::writeOne(m_memoryNonces, sender, nonce);
            co_return TransactionStatus::None;
        }
    }
    // not in ledger memory, check from storage
    // TODO)): block number not use nowadays
    auto const senderHex = toHex(sender);
    auto const storageState = co_await m_ledger->getStorageState(senderHex, 0);
    if (storageState.has_value())
    {
        auto const nonceInStorage = u256(storageState.value().nonce);
        // update memory first
        co_await storage2::writeOne(m_ledgerStateNonces, sender, nonceInStorage);
        if (nonce < nonceInStorage || nonce > nonceInStorage + DEFAULT_WEB3_NONCE_CHECK_LIMIT)
        {
            co_return TransactionStatus::NonceCheckFail;
        }
    }
    co_await storage2::writeOne(m_memoryNonces, sender, nonce);
    // TODO)): check balance？
    co_return TransactionStatus::None;
}

void Web3NonceChecker::batchRemoveMemoryNonce(std::unordered_map<std::string, u256> _nonceMap)
{
    for (auto const& [sender, nonce] : _nonceMap)
    {
        auto const nonceInMem = task::syncWait(storage2::readOne(m_memoryNonces, sender));
        if (nonceInMem.has_value())
        {
            // 假设交易池里有0xabcd的3笔交易，nonce分别是5，7，9。那么此时memory记录的nonce为9。
            // 1. 如果nonce为9的交易先被打包进区块，那么交易池中nonce为5和7的交易就会被移除。ledger
            // state nonce将会更新到9，此时按照ledger state nonce为准。
            // 2. 如果nonce为7的交易先被打包进区块，那么交易池中nonce为5的交易就会被移除。ledger
            // state nonce将会更新到7, 此时仍然以memory nonce为准。
            // Suppose there are 3 transactions with nonce 5, 7, 9 in the transaction pool with
            // 0xabcd. At this time, the nonce recorded in memory is 9.
            // 1. If the transaction with nonce 9 is packaged into a block first, the transactions
            // with nonce 5 and 7 in the transaction pool will be removed. The ledger state nonce
            // will be updated to 9, and the ledger state nonce will be followed at this time.
            // 2. If the transaction with nonce 7 is packaged into a block first, the transaction
            // with nonce 5 in the transaction pool will be removed. The ledger state nonce will be
            // updated to 7, and the memory nonce will still be followed at this time.
            if (u256(nonceInMem.value()) > nonce)
            {
                task::wait(storage2::removeOne(m_memoryNonces, sender));
            }
        }
    }
}


void Web3NonceChecker::batchInsert(
    RANGES::input_range auto&& senders, RANGES::input_range auto&& nonces)
{
    task::wait(storage2::writeSome(m_ledgerStateNonces, senders, nonces));
}

void Web3NonceChecker::insert(std::string sender, u256 nonce)
{
    task::wait(storage2::writeOne(m_ledgerStateNonces, sender, nonce));
}
