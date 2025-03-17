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
 * @file Web3NonceChecker.h
 * @author: kyonGuo
 * @date 2024/8/26
 */

#pragma once
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/txpool/TxPoolTypeDef.h>
#include <concepts>

namespace bcos::txpool
{
constexpr uint16_t DefaultBucketSize = 256;

struct PairHash

{
    template <std::convertible_to<std::string_view> StringView>
    std::size_t operator()(const std::pair<StringView, StringView>& pair) const
    {
        return std::hash<StringView>()(pair.first);
    }
};
struct PairEqual
{
    template <std::convertible_to<std::string_view> Lhs, std::convertible_to<std::string_view> Rhs>
    bool operator()(const std::pair<Lhs, Lhs>& lhs, const std::pair<Rhs, Rhs>& rhs) const
    {
        return std::string_view{lhs.first} == std::string_view{rhs.first} &&
               std::string_view{lhs.second} == std::string_view{rhs.second};
    }
};
/**
 * Implementation for web3 nonce-checker
 */
class Web3NonceChecker
{
public:
    using Ptr = std::shared_ptr<Web3NonceChecker>;
    explicit Web3NonceChecker(bcos::ledger::LedgerInterface::Ptr ledger)
      : m_ledgerStateNonces(DefaultBucketSize),
        m_memoryNonces(DefaultBucketSize),
        m_maxNonces(DefaultBucketSize),
        m_ledger(std::move(ledger))
    {}
    ~Web3NonceChecker() = default;
    Web3NonceChecker(const Web3NonceChecker&) = delete;
    Web3NonceChecker& operator=(const Web3NonceChecker&) = delete;
    Web3NonceChecker(Web3NonceChecker&&) = default;
    Web3NonceChecker& operator=(Web3NonceChecker&&) = default;
    /**
     * check nonce for web3 transaction
     * @param _tx: the transaction to be checked
     * @param onlyCheckLedgerNonce: whether only check the nonce in ledger state; if tx verified
     * from rpc, then it will be false; if tx verified from txpool, then it will be true for skip
     * check tx nonce self failed.
     * @return TransactionStatus: the status of the transaction
     */
    task::Task<bcos::protocol::TransactionStatus> checkWeb3Nonce(
        const bcos::protocol::Transaction& _tx, bool onlyCheckLedgerNonce = false);

    task::Task<bcos::protocol::TransactionStatus> checkWeb3Nonce(
        std::string sender, std::string nonce, bool onlyCheckLedgerNonce = false);

    /**
     * batch insert sender and nonce into ledger state nonce and memory nonce, call when block is
     * committed
     * @param senders sender string list
     * @param noncesSet nonce u256 set
     */
    task::Task<void> updateNonceCache(
        RANGES::input_range auto&& senders, RANGES::input_range auto&& noncesSet)
    {
        if (RANGES::size(senders) != RANGES::size(noncesSet)) [[unlikely]]
        {
            TXPOOL_LOG(ERROR) << LOG_DESC("Web3Nonce: update nonce cache with different size")
                              << LOG_KV("senderSize", RANGES::size(senders))
                              << LOG_KV("nonceSize", RANGES::size(noncesSet));
        }
        for (auto&& [sender, nonceSet] : RANGES::views::zip(senders, noncesSet))
        {
            // Update ledger nonce cache and remove memory nonce cache here. When a new transaction
            // is coming with the same nonce, it should be refused by ledger nonce layer.
            if (!nonceSet.empty())
            {
                // because the nonce is sorted, so the last element is the max nonce
                // and the max nonce is in one certain tx, so here should update the max nonce,
                // expecting next nonce in next tx
                auto maxNonce = *nonceSet.rbegin() + 1;
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    TXPOOL_LOG(TRACE)
                        << LOG_DESC("Web3Nonce: update ledger nonce cache")
                        << LOG_KV("sender", toHex(sender)) << LOG_KV("nonce", maxNonce);
                }
                co_await storage2::writeOne(m_ledgerStateNonces, sender, maxNonce);
                if (auto maxMemNonce = co_await storage2::readOne(m_maxNonces, sender);
                    maxMemNonce.has_value() && maxNonce >= maxMemNonce.value())
                {
                    co_await storage2::removeOne(m_maxNonces, sender);
                }
            }
            for (auto&& nonce : nonceSet)
            {
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    TXPOOL_LOG(TRACE) << LOG_DESC("Web3Nonce: rm mem nonce cache")
                                      << LOG_KV("sender", toHex(sender)) << LOG_KV("nonce", nonce);
                }
                co_await storage2::removeOne(
                    m_memoryNonces, std::make_pair(sender, toQuantity(nonce)));
            }
        }
    }

    /**
     * batch remove sender and nonce from memory nonce, call when tx is invalid
     * @param senders sender string list
     * @param nonces nonce string list
     */
    task::Task<void> batchRemoveMemoryNonce(
        RANGES::input_range auto&& senders, RANGES::input_range auto&& nonces)
    {
        // 假设交易池里有0xabcd的3笔交易，nonce分别是5，7，9。如果nonce为7的交易先被打包进区块，ledger
        // state nonce将会更新到7，那么交易池中memory nonce中小等于ledger
        // nonce的5和7的交易就会被移除。
        // Suppose there are 3 transactions with nonce 5, 7,
        // 9 in the transaction pool sent by 0xabcd. If the transaction with nonce 7 is packaged
        // into a block first, the ledge state nonce will be updated to 7, then the transactions
        // with nonce 5 and 7 in the memory nonce of the transaction pool will be removed.
        std::stringstream ss;
        for (auto&& [sender, nonce] : RANGES::views::zip(senders, nonces))
        {
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                ss << toHex(sender) << ":" << nonce << ", ";
            }
            co_await storage2::removeOne(m_memoryNonces, std::make_pair(sender, nonce));
        }
        TXPOOL_LOG(DEBUG) << LOG_DESC("Web3Nonce: rm mem nonce cache for invalid txs.") << ss.str();
    }

    /**
     * insert sender nonce to memory layer, it will be called when transaction verifies, which is
     * from rpc or p2p
     * @param sender eoa address, bytes string
     * @param nonce transaction nonce, number string
     * @return coroutine void
     */
    task::Task<void> insertMemoryNonce(std::string sender, std::string nonce);

    task::Task<std::optional<u256>> getPendingNonce(std::string_view sender);

    // only for test, inset nonce into ledgerStateNonces
    void insert(std::string sender, u256 nonce);

private:
    // sender address(bytes string) -> nonce
    // ledger state nonce cache the nonce of the sender in storage, every tx send by the sender,
    // should bigger than the nonce in ledger state
    bcos::storage2::memory_storage::MemoryStorage<std::string, u256,
        storage2::memory_storage::LRU | storage2::memory_storage::CONCURRENT>
        m_ledgerStateNonces;

    // <sender address(bytes string), nonce>
    // memory nonce cache the nonce of the sender in memory
    // every tx send by the sender, should bigger than the nonce in ledger state and unique in
    // memory
    bcos::storage2::memory_storage::MemoryStorage<std::pair<std::string, std::string>,
        std::monostate, storage2::memory_storage::LRU | storage2::memory_storage::CONCURRENT,
        PairHash, PairEqual>
        m_memoryNonces;

    // <sender address(bytes string) => nonce>
    // Only store max nonce of memory nonce. This is used to response the pending nonce request
    bcos::storage2::memory_storage::MemoryStorage<std::string, u256,
        storage2::memory_storage::LRU | storage2::memory_storage::CONCURRENT>
        m_maxNonces;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
};
}  // namespace bcos::txpool