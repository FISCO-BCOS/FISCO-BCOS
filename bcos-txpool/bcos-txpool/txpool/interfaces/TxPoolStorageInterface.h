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
#include <bcos-framework/interfaces/protocol/Block.h>
#include <bcos-framework/interfaces/protocol/Transaction.h>
#include <bcos-framework/interfaces/txpool/TxPoolTypeDef.h>
#include <bcos-framework/libprotocol/TransactionStatus.h>
#include <bcos-framework/libutilities/CallbackCollectionHandler.h>

namespace bcos
{
namespace txpool
{
class TxPoolStorageInterface
{
public:
    using Ptr = std::shared_ptr<TxPoolStorageInterface>;
    TxPoolStorageInterface() = default;
    virtual ~TxPoolStorageInterface() {}

    virtual bcos::protocol::TransactionStatus submitTransaction(
        bytesPointer _txData, bcos::protocol::TxSubmitCallback _txSubmitCallback = nullptr) = 0;
    virtual bcos::protocol::TransactionStatus submitTransaction(
        bcos::protocol::Transaction::Ptr _tx,
        bcos::protocol::TxSubmitCallback _txSubmitCallback = nullptr,
        bool _enforceImport = false) = 0;

    virtual bcos::protocol::TransactionStatus insert(bcos::protocol::Transaction::ConstPtr _tx) = 0;
    virtual void batchInsert(bcos::protocol::Transactions const& _txs) = 0;

    virtual bcos::protocol::Transaction::ConstPtr remove(bcos::crypto::HashType const& _txHash) = 0;
    virtual bcos::protocol::Transaction::ConstPtr removeSubmittedTx(
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult) = 0;
    virtual void batchRemove(bcos::protocol::BlockNumber _batchId,
        bcos::protocol::TransactionSubmitResults const& _txsResult) = 0;

    // Note: the transactions may be missing from the transaction pool
    virtual bcos::protocol::TransactionsPtr fetchTxs(
        bcos::crypto::HashList& _missedTxs, bcos::crypto::HashList const& _txsList) = 0;

    /**
     * @brief Get newly inserted transactions from the txpool
     *
     * @param _txsLimit Maximum number of transactions that can be obtained at a time
     * @return List of new transactions
     */
    virtual bcos::protocol::ConstTransactionsPtr fetchNewTxs(size_t _txsLimit) = 0;
    virtual void batchFetchTxs(bcos::protocol::Block::Ptr _txsList,
        bcos::protocol::Block::Ptr _sysTxsList, size_t _txsLimit, TxsHashSetPtr _avoidTxs,
        bool _avoidDuplicate = true) = 0;

    virtual bool exist(bcos::crypto::HashType const& _txHash) = 0;

    virtual bcos::crypto::HashListPtr filterUnknownTxs(
        bcos::crypto::HashList const& _txsHashList, bcos::crypto::NodeIDPtr _peer) = 0;

    virtual size_t size() const = 0;
    virtual void clear() = 0;

    // Register a handler that will be called once there is a new transaction imported
    template <class T>
    bcos::Handler<> onReady(T const& _t)
    {
        return m_onReady.add(_t);
    }

    virtual void batchMarkTxs(bcos::crypto::HashList const& _txsHashList,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        bool _sealFlag) = 0;
    virtual void batchMarkAllTxs(bool _sealFlag) = 0;

    virtual size_t unSealedTxsSize() = 0;

    virtual void registerUnsealedTxsNotifier(
        std::function<void(size_t, std::function<void(Error::Ptr)>)> _unsealedTxsNotifier)
    {
        m_unsealedTxsNotifier = _unsealedTxsNotifier;
    }

    virtual void stop() = 0;
    virtual void printPendingTxs() {}

    virtual std::shared_ptr<bcos::crypto::HashList> batchVerifyProposal(
        bcos::protocol::Block::Ptr _block) = 0;

    virtual bool batchVerifyProposal(std::shared_ptr<bcos::crypto::HashList> _txsHashList) = 0;

protected:
    bcos::CallbackCollectionHandler<> m_onReady;
    // notify the sealer the latest unsealed txs
    std::function<void(size_t, std::function<void(Error::Ptr)>)> m_unsealedTxsNotifier;
};
}  // namespace txpool
}  // namespace bcos