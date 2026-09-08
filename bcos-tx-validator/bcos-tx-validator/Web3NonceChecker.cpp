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
#include "bcos-framework/txpool/TxPoolTypeDef.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/txpool/Constant.h>
#include <bcos-protocol/TransactionStatus.h>

using namespace bcos;
using namespace bcos::txvalidator;
using namespace bcos::protocol;

task::Task<bcos::protocol::TransactionStatus> Web3NonceChecker::checkWeb3Nonce(
    std::string_view sender, std::string_view nonce, bool onlyCheckLedgerNonce)
{
    // Reject oversized nonce strings before any u256 conversion to prevent LRU capacity bypass
    // (FIB-57): max decimal digits of u256 is 78
    constexpr static size_t MAX_NONCE_STRING_LENGTH = 78;
    if (nonce.length() > MAX_NONCE_STRING_LENGTH) [[unlikely]]
    {
        TXPOOL_LOG(WARNING) << LOG_DESC("Web3Nonce: reject oversized nonce string")
                            << LOG_KV("sender", toHex(sender))
                            << LOG_KV("nonceLen", nonce.length());
        co_return TransactionStatus::NonceCheckFail;
    }

    // Note:
    // 在以太坊中，nonce是从0开始的，也代表着该地址发交易次数。例如：在存储中存储的是5，那么web3工具从rpc
    // api获取的transactionCount就是5，那么新的交易将从5开始发。
    // Note: In Ethereum, the nonce starts from 0, which also represents the number of transactions
    // sent by the address. For example, if 5 is stored in the storage, then the transactionCount
    // obtained from the rpc api by the web3 tool is 5; then the new transaction will be sent
    // from 5.
    if (!onlyCheckLedgerNonce && co_await existsMemoryNonce(sender, nonce))
    {
        // memory nonce check nonce existence in memory first, if not exist, then check from storage
        TXPOOL_LOG(TRACE) << LOG_DESC("Web3Nonce: nonce mem check fail")
                          << LOG_KV("sender", toHex(sender)) << LOG_KV("nonce", nonce);
        co_return TransactionStatus::NonceCheckFail;
    }

    // The committed window: the account's nonce through the FIB-59 cache, then the one rule the
    // admission layer's Web3NonceWindow applies to the same read. An account with no on-chain
    // state is not judged -- see committedNonce().
    auto const committed = co_await committedNonce(sender);
    if (committed.has_value() && !withinCommittedWindow(u256(nonce), *committed))
    {
        TXPOOL_LOG(TRACE) << LOG_DESC("Web3Nonce: nonce ledger check fail")
                          << LOG_KV("sender", toHex(sender)) << LOG_KV("nonce", nonce)
                          << LOG_KV("nonceInLedger", *committed);
        co_return TransactionStatus::NonceCheckFail;
    }
    co_return TransactionStatus::None;
}

bool Web3NonceChecker::withinCommittedWindow(u256 const& txNonce, u256 const& committedNonce)
{
    return txNonce >= committedNonce &&
           txNonce <= committedNonce + bcos::protocol::DEFAULT_WEB3_NONCE_CHECK_LIMIT;
}

task::Task<TransactionStatus> Web3NonceChecker::checkWeb3Nonce(
    const bcos::protocol::Transaction& _tx, bool onlyCheckLedgerNonce)
{
    co_return co_await checkWeb3Nonce(_tx.sender(), _tx.nonce(), onlyCheckLedgerNonce);
}

task::Task<bool> Web3NonceChecker::existsMemoryNonce(
    std::string_view sender, std::string_view nonce)
{
    co_return co_await storage2::existsOne(m_memoryNonces, std::make_pair(sender, u256(nonce)));
}

task::Task<bool> Web3NonceChecker::insertMemoryNonce(std::string sender, std::string nonce)
{
    auto const uNonce = u256(nonce);
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        TXPOOL_LOG(TRACE) << LOG_DESC("Web3Nonce: write memory nonces")
                          << LOG_KV("sender", toHex(sender)) << LOG_KV("nonce", uNonce);
    }
    // Atomic check-and-reserve: insertIfAbsent returns false when the (sender, nonce)
    // pair already exists, eliminating the TOCTOU race between existsOne() and writeOne().
    if (const bool inserted = co_await storage2::insertIfAbsent(
            m_memoryNonces, std::make_pair(sender, uNonce), std::monostate{});
        !inserted)
    {
        co_return false;
    }
    auto const newMaxNonce = uNonce + 1;
    auto const written = co_await storage2::writeOneIf(m_maxNonces, sender, newMaxNonce,
        [&](u256 const& existing) { return newMaxNonce >= existing; });
    if (written && c_fileLogLevel == TRACE) [[unlikely]]
    {
        TXPOOL_LOG(TRACE) << LOG_DESC("Web3Nonce: update max nonce")
                          << LOG_KV("sender", toHex(sender)) << LOG_KV("newNonce", uNonce);
    }
    co_return true;
}

task::Task<std::optional<u256>> Web3NonceChecker::committedNonce(std::string_view sender)
{
    auto const senderView = std::string(sender);
    if (auto const cached = co_await bcos::storage2::readOne(m_ledgerStateNonces, senderView))
    {
        co_return cached;
    }
    auto const senderHex = toHex(sender);
    if (auto const storageState = co_await m_ledger->getStorageState(senderHex, 0);
        storageState.has_value())
    {
        auto const nonceInStorage = u256(storageState.value().nonce);
        // Monotonic: only ever raise the cached value (FIB-59).
        co_await storage2::writeOneIf(m_ledgerStateNonces, senderView, nonceInStorage,
            [&](u256 const& existing) { return nonceInStorage > existing; });
        co_return nonceInStorage;
    }
    // The account has no on-chain state yet. Reported as absent, not as nonce 0 -- see
    // txvalidator::AccountState::nonce.
    co_return std::nullopt;
}

task::Task<std::optional<u256>> Web3NonceChecker::getPendingNonce(std::string_view sender)
{
    const auto bytesSender = fromHex<std::string_view, std::string>(sender);
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