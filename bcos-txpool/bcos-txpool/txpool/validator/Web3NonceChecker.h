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

    /**
     * batch insert sender and nonce into ledger state nonce and memory nonce, call when block is
     * committed
     * @param senders sender string list
     * @param noncesSet nonce u256 set
     */
    task::Task<void> updateNonceCache(
        RANGES::input_range auto&& senders, RANGES::input_range auto&& noncesSet);

    /**
     * batch remove sender and nonce from memory nonce, call when tx is invalid
     * @param senders sender string list
     * @param nonces nonce string list
     */
    task::Task<void> batchRemoveMemoryNonce(
        RANGES::input_range auto&& senders, RANGES::input_range auto&& nonces);

    task::Task<void> insertMemoryNonce(std::string sender, std::string nonce);

    task::Task<std::optional<u256>> getPendingNonce(std::string_view sender);

    // for test, inset nonce into ledgerStateNonces
    void insert(std::string sender, u256 nonce);

private:
    // sender address (bytes string) -> nonce
    // ledger state nonce cache the nonce of the sender in storage, every tx send by the sender,
    // should bigger than the nonce in ledger state
    bcos::storage2::memory_storage::MemoryStorage<std::string, u256,
        storage2::memory_storage::LRU | storage2::memory_storage::CONCURRENT>
        m_ledgerStateNonces;

    // <sender address (bytes string), nonce>
    // memory nonce cache the nonce of the sender in memory
    // every tx send by the sender, should bigger than the nonce in ledger state and not in memory
    bcos::storage2::memory_storage::MemoryStorage<std::pair<std::string, std::string>,
        std::monostate, storage2::memory_storage::LRU | storage2::memory_storage::CONCURRENT,
        PairHash, PairEqual>
        m_memoryNonces;

    // sender address(bytes string), nonce
    // only store max nonce of memory nonce.
    bcos::storage2::memory_storage::MemoryStorage<std::string, u256,
        storage2::memory_storage::LRU | storage2::memory_storage::CONCURRENT>
        m_maxNonces;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
};
}  // namespace bcos::txpool