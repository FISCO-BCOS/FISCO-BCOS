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
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/storage2/MemoryStorage.h>

namespace bcos::txpool
{
/**
 * Implementation for web3 nonce-checker
 */
class Web3NonceChecker
{
public:
    using Ptr = std::shared_ptr<Web3NonceChecker>;
    explicit Web3NonceChecker(bcos::ledger::LedgerInterface::Ptr ledger)
      : m_ledgerStateNonces(256), m_ledger(std::move(ledger))
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
        bcos::protocol::Transaction::ConstPtr _tx, bool onlyCheckLedgerNonce = false);

    /**
     * batch insert sender and nonce into ledger state nonce
     * @param senders the senders to be inserted
     * @param nonces the nonces to be inserted
     */
    void batchInsert(RANGES::input_range auto&& senders, RANGES::input_range auto&& nonces);

    /**
     * batch remove sender and nonce from memory nonce
     * @param senders the sender to be removed
     * @param nonces the nonce to be removed, maybe not bigger than the nonce in memory
     */
    void batchRemoveMemoryNonce(std::unordered_map<std::string, u256>);

    // for test, inset nonce into ledgerStateNonces
    void insert(std::string sender, u256 nonce);

private:
    // sender address (bytes string) -> nonce
    using NonceMap = bcos::storage2::memory_storage::MemoryStorage<std::string, u256,
        storage2::memory_storage::LRU>;
    NonceMap m_ledgerStateNonces;
    NonceMap m_memoryNonces;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
};
}  // namespace bcos::txpool
