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
 * @brief Storage interface for TxPool
 * @file TxPoolStorageInterface.h
 * @author: yujiechen
 * @date 2021-05-07
 */
#pragma once
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/txpool/TxPoolTypeDef.h>
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-task/Task.h>
#include <utility>

namespace bcos::txpool
{
class TxPoolStorageInterface
{
public:
    using Ptr = std::shared_ptr<TxPoolStorageInterface>;
    TxPoolStorageInterface() = default;
    virtual ~TxPoolStorageInterface() = default;

    virtual task::Task<protocol::TransactionSubmitResult::Ptr> submitTransaction(
        protocol::Transaction::Ptr transaction, bool waitForReceipt) = 0;
    virtual std::vector<protocol::Transaction::ConstPtr> getTransactions(
        crypto::HashListView hashes) = 0;

    virtual void batchRemove(bcos::protocol::BlockNumber _batchId,
        bcos::protocol::TransactionSubmitResults const& _txsResult) = 0;

    virtual bool batchVerifyAndSubmitTransaction(
        bcos::protocol::BlockHeader::ConstPtr _header, bcos::protocol::TransactionsPtr _txs) = 0;
    virtual void batchImportTxs(bcos::protocol::TransactionsPtr _txs) = 0;

    /**
     * @brief Get newly inserted transactions from the txpool
     *
     * @param _txsLimit Maximum number of transactions that can be obtained at a time
     * @return List of new transactions
     */
    virtual bool batchFetchTxs(bcos::protocol::Block::Ptr _txsList,
        bcos::protocol::Block::Ptr _sysTxsList, size_t _txsLimit, TxsHashSetPtr _avoidTxs,
        bool _avoidDuplicate = true) = 0;

    virtual bool exist(bcos::crypto::HashType const& _txHash) = 0;
    virtual bool batchExists(crypto::HashListView _txsHashList) = 0;

    virtual bcos::crypto::HashList filterUnknownTxs(
        crypto::HashListView _txsHashList, bcos::crypto::NodeIDPtr _peer) = 0;

    virtual size_t size() const = 0;
    virtual void clear() = 0;

    // return true if all txs have been marked
    virtual bool batchMarkTxs(crypto::HashListView _txsHashList,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        bool _sealFlag) = 0;
    virtual void batchMarkAllTxs(bool _sealFlag) = 0;

    virtual void stop() = 0;
    virtual void start() = 0;
    virtual void printPendingTxs() {}

    virtual std::shared_ptr<bcos::crypto::HashList> batchVerifyProposal(
        bcos::protocol::Block::ConstPtr _block) = 0;

    virtual bcos::crypto::HashListPtr getTxsHash(int _limit) = 0;

    void registerTxsCleanUpSwitch(std::function<bool()> _txsCleanUpSwitch)
    {
        m_txsCleanUpSwitch = std::move(_txsCleanUpSwitch);
    }

    virtual void registerTxsNotifier(
        std::function<void(size_t, std::function<void(Error::Ptr)>)> _txsNotifier)
    {
        m_txsNotifier = std::move(_txsNotifier);
    }

protected:
    // Determine to periodically clean up expired transactions or not
    std::function<bool()> m_txsCleanUpSwitch;

    // notify the consensus the latest txs count, to determine stop/start the consensus
    // timer or not
    std::function<void(size_t, std::function<void(Error::Ptr)>)> m_txsNotifier;
};
}  // namespace bcos::txpool