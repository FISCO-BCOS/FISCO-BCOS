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
 * @brief an implementation of using memory to store transactions
 * @file MemoryStorage.h
 * @author: yujiechen
 * @date 2021-05-07
 */
#pragma once
#include "bcos-txpool/TxPoolConfig.h"
#include <bcos-utilities/ThreadPool.h>
#include <bcos-utilities/Timer.h>
#include <tbb/concurrent_unordered_map.h>
#define TBB_PREVIEW_CONCURRENT_ORDERED_CONTAINERS 1
#include <tbb/concurrent_set.h>
namespace bcos
{
namespace txpool
{
class MemoryStorage : public TxPoolStorageInterface,
                      public std::enable_shared_from_this<MemoryStorage>
{
public:
    // the default txsExpirationTime is 10 minutes
    explicit MemoryStorage(TxPoolConfig::Ptr _config, size_t _notifyWorkerNum = 2,
        int64_t _txsExpirationTime = 10 * 60 * 1000);
    ~MemoryStorage() override {}

    bcos::protocol::TransactionStatus submitTransaction(bytesPointer _txData,
        bcos::protocol::TxSubmitCallback _txSubmitCallback = nullptr) override;
    bcos::protocol::TransactionStatus submitTransaction(bcos::protocol::Transaction::Ptr _tx,
        bcos::protocol::TxSubmitCallback _txSubmitCallback = nullptr, bool _enforceImport = false,
        bool _checkPoolLimit = true) override;

    bcos::protocol::TransactionStatus insert(bcos::protocol::Transaction::ConstPtr _tx) override;
    void batchInsert(bcos::protocol::Transactions const& _txs) override;

    bcos::protocol::Transaction::ConstPtr remove(bcos::crypto::HashType const& _txHash) override;
    void batchRemove(bcos::protocol::BlockNumber _batchId,
        bcos::protocol::TransactionSubmitResults const& _txsResult) override;
    bcos::protocol::Transaction::ConstPtr removeSubmittedTx(
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult) override;

    bcos::protocol::TransactionsPtr fetchTxs(
        bcos::crypto::HashList& _missedTxs, bcos::crypto::HashList const& _txsList) override;

    bcos::protocol::ConstTransactionsPtr fetchNewTxs(size_t _txsLimit) override;
    void batchFetchTxs(bcos::protocol::Block::Ptr _txsList, bcos::protocol::Block::Ptr _sysTxsList,
        size_t _txsLimit, TxsHashSetPtr _avoidTxs, bool _avoidDuplicate = true) override;

    bool exist(bcos::crypto::HashType const& _txHash) override
    {
        ReadGuard l(x_txpoolMutex);
        return m_txsTable.count(_txHash);
    }
    size_t size() const override;
    void clear() override;

    bcos::crypto::HashListPtr filterUnknownTxs(
        bcos::crypto::HashList const& _txsHashList, bcos::crypto::NodeIDPtr _peer) override;

    bcos::crypto::HashListPtr getAllTxsHash() override;
    void batchMarkTxs(bcos::crypto::HashList const& _txsHashList,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        bool _sealFlag) override;
    void batchMarkAllTxs(bool _sealFlag) override;

    size_t unSealedTxsSize() override;

    void stop() override;
    void start() override;
    void printPendingTxs() override;

    std::shared_ptr<bcos::crypto::HashList> batchVerifyProposal(
        bcos::protocol::Block::Ptr _block) override;

    bool batchVerifyProposal(std::shared_ptr<bcos::crypto::HashList> _txsHashList) override;

protected:
    bcos::protocol::TransactionStatus enforceSubmitTransaction(
        bcos::protocol::Transaction::Ptr _tx);
    bcos::protocol::TransactionStatus verifyAndSubmitTransaction(
        bcos::protocol::Transaction::Ptr _tx, bcos::protocol::TxSubmitCallback _txSubmitCallback,
        bool _checkPoolLimit);
    size_t unSealedTxsSizeWithoutLock();
    bcos::protocol::TransactionStatus txpoolStorageCheck(bcos::protocol::Transaction::ConstPtr _tx);

    virtual bcos::protocol::Transaction::ConstPtr removeWithoutLock(
        bcos::crypto::HashType const& _txHash);
    virtual bcos::protocol::Transaction::ConstPtr removeSubmittedTxWithoutLock(
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult);

    virtual void notifyInvalidReceipt(bcos::crypto::HashType const& _txHash,
        bcos::protocol::TransactionStatus _status,
        bcos::protocol::TxSubmitCallback _txSubmitCallback);

    virtual void notifyTxResult(bcos::protocol::Transaction::ConstPtr _tx,
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult);

    virtual void removeInvalidTxs();

    virtual void preCommitTransaction(bcos::protocol::Transaction::ConstPtr _tx);

    virtual void notifyUnsealedTxsSize(size_t _retryTime = 0);
    virtual void cleanUpExpiredTransactions();

private:
    TxPoolConfig::Ptr m_config;
    ThreadPool::Ptr m_notifier;
    ThreadPool::Ptr m_worker;

    tbb::concurrent_unordered_map<bcos::crypto::HashType, bcos::protocol::Transaction::ConstPtr,
        std::hash<bcos::crypto::HashType>>
        m_txsTable;

    mutable SharedMutex x_txpoolMutex;

    tbb::concurrent_set<bcos::crypto::HashType> m_invalidTxs;
    tbb::concurrent_set<bcos::protocol::NonceType> m_invalidNonces;

    tbb::concurrent_set<bcos::crypto::HashType> m_missedTxs;
    mutable SharedMutex x_missedTxs;
    std::atomic<size_t> m_sealedTxsSize = {0};

    size_t c_maxRetryTime = 3;

    std::atomic<bcos::protocol::BlockNumber> m_blockNumber = {0};
    std::atomic_bool m_printed = {false};
    int64_t m_blockNumberUpdatedTime;

    // the txs expiration time, default is 10 minutes
    int64_t m_txsExpirationTime = 10 * 60 * 1000;
    // timer to clear up the expired txs in-period
    std::shared_ptr<Timer> m_cleanUpTimer;
    // Maximum number of transactions traversed by m_cleanUpTimer,
    // The limit set here is to minimize the impact of the cleanup operation on txpool performance
    uint64_t c_maxTraverseTxsNum = 10000;
};
}  // namespace txpool
}  // namespace bcos