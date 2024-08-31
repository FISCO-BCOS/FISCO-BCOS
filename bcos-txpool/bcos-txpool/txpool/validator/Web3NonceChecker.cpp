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

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;

task::Task<TransactionStatus> Web3NonceChecker::checkWeb3Nonce(Transaction::ConstPtr _tx)
{
    // sender is bytes view
    auto sender = std::string(_tx->sender());
    auto nonce = u256(_tx->nonce());

    if (auto const nonceInMem = co_await bcos::storage2::readOne(m_nonces, sender))
    {
        if (u256(nonceInMem.value()) >= nonce)
        {
            co_return TransactionStatus::NonceCheckFail;
        }
        // else
        // {
        //     // update memory if nonce bigger than memory's
        //     co_await storage2::writeOne(m_nonces, sender, nonce);
        //     co_return TransactionStatus::None;
        // }
    }
    // not in memory, check from ledger
    // TODO)): block number not use nowadays
    auto const senderHex = toHex(sender);
    auto const storageState = co_await m_ledger->getStorageState(senderHex, 0);
    if (storageState.has_value())
    {
        auto const nonceInLedger = boost::lexical_cast<u256>(storageState.value().nonce);
        // update memory first
        co_await storage2::writeOne(m_nonces, sender, storageState.value().nonce);
        if (nonceInLedger >= nonce)
        {
            co_return TransactionStatus::NonceCheckFail;
        }
    }
    // TODO)): check balance？
    // 在这里仍然不更新内存，因为这个nonce可能是未来的nonce。会在未来交易落盘时再更新。
    // Still not update memory here, because this nonce may be the nonce in the future. Will update
    // when the transaction is written to the disk in the future.
    co_return TransactionStatus::None;
}

void Web3NonceChecker::batchInsert(
    RANGES::input_range auto&& senders, RANGES::input_range auto&& nonces)
{
    task::wait(storage2::writeSome(m_nonces, senders, nonces));
}

void Web3NonceChecker::insert(std::string sender, std::string nonce)
{
    task::wait(storage2::writeOne(m_nonces, sender, nonce));
}
