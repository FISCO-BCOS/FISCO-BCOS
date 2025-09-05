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

#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-framework/protocol/TransactionMetaData.h"
#include "bcos-task/Task.h"
#include "bcos-txpool/TxPoolConfig.h"
#include "bcos-txpool/txpool/utilities/Common.h"
#include "txpool/interfaces/TxPoolStorageInterface.h"
#include <bcos-utilities/BucketMap.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-utilities/ThreadPool.h>
#include <bcos-utilities/Timer.h>

namespace bcos::txpool
{

class MemoryStorage : public TxPoolStorageInterface,
                      public std::enable_shared_from_this<MemoryStorage>
{
public:
    // the default txsExpirationTime is 10 minutes
    explicit MemoryStorage(TxPoolConfig::Ptr _config, size_t _notifyWorkerNum = 2,
        uint64_t _txsExpirationTime = TX_DEFAULT_EXPIRATION_TIME);
    ~MemoryStorage() override;

    task::Task<protocol::TransactionSubmitResult::Ptr> submitTransaction(
        protocol::Transaction::Ptr transaction, bool waitForReceipt) override;
    std::vector<protocol::Transaction::ConstPtr> getTransactions(
        crypto::HashListView hashes) override;

    // invoke when scheduler finished block executed and notify txpool new block result
    void batchRemoveSealedTxs(bcos::protocol::BlockNumber _batchId,
        bcos::protocol::TransactionSubmitResults const& _txsResult) override;

    bool batchSealTransactions(std::vector<protocol::TransactionMetaData::Ptr>& _txsList,
        std::vector<protocol::TransactionMetaData::Ptr>& _sysTxsList, size_t _txsLimit) override;

    bool exists(bcos::crypto::HashType const& _txHash) override;
    bool batchExists(crypto::HashListView _txsHashList) override;

    size_t size() const override;
    void clear() override;

    // FIXME: deprecated, after using txpool::broadcastTransaction
    bcos::crypto::HashList filterUnknownTxs(
        crypto::HashListView _txsHashList, bcos::crypto::NodeIDPtr _peer) override;

    bcos::crypto::HashListPtr getTxsHash(int _limit) override;
    void batchMarkAllTxs(bool _sealFlag) override;

    void stop() override;
    void start() override;

    std::shared_ptr<bcos::crypto::HashList> batchVerifyProposal(
        bcos::protocol::Block::ConstPtr _block) override;

    bool batchVerifyAndSubmitTransaction(
        const bcos::protocol::BlockHeader& _header, bcos::protocol::TransactionsPtr _txs) override;
    void batchImportTxs(bcos::protocol::TransactionsPtr _txs) override;

    // return true if all txs have been marked
    bool batchMarkTxs(crypto::HashListView _txsHashList, bcos::protocol::BlockNumber _batchId,
        bcos::crypto::HashType const& _batchHash, bool _sealFlag) override;

    bcos::protocol::TransactionStatus verifyAndSubmitTransaction(
        protocol::Transaction::Ptr transaction, protocol::TxSubmitCallback txSubmitCallback,
        bool checkPoolLimit, bool lock);

    // For testing
    bcos::protocol::TransactionStatus insert(bcos::protocol::Transaction::Ptr transaction);
    void remove(crypto::HashType const& _txHash);

protected:
    virtual void notifyTxsSize(size_t _retryTime = 0);

    bcos::protocol::TransactionStatus enforceSubmitTransaction(
        bcos::protocol::Transaction::Ptr _tx);
    bcos::protocol::TransactionStatus txpoolStorageCheck(
        const bcos::protocol::Transaction& transaction,
        protocol::TxSubmitCallback& txSubmitCallback);

    void onTxRemoved(const bcos::protocol::Transaction::Ptr& _tx, bool needNotifyUnsealedTxsSize);

    virtual void notifyTxResult(bcos::protocol::Transaction& transaction,
        bcos::protocol::TransactionSubmitResult::Ptr txSubmitResult);

    void removeInvalidTxs(std::span<bcos::protocol::Transaction::Ptr> txs);
    virtual void cleanUpExpiredTransactions();

    void printPendingTxs() override;

    TxPoolConfig::Ptr m_config;

    using TxsMap = BucketMap<bcos::crypto::HashType, bcos::protocol::Transaction::Ptr>;
    TxsMap m_unsealTransactions;
    TxsMap m_sealedTransactions;

    std::atomic<bcos::protocol::BlockNumber> m_blockNumber = {0};
    uint64_t m_blockNumberUpdatedTime;

    // the txs expiration time, default is 10 minutes
    uint64_t m_txsExpirationTime = TX_DEFAULT_EXPIRATION_TIME;
    // timer to clear up the expired txs in-period
    std::shared_ptr<Timer> m_cleanUpTimer;
    // timer to notify txs size
    std::shared_ptr<Timer> m_txsSizeNotifierTimer;
    bcos::crypto::HashType m_knownLatestSealedTxHash;
};
}  // namespace bcos::txpool