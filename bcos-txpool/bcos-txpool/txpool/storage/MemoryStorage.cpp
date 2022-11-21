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
 * @file MemoryStorage.cpp
 * @author: yujiechen
 * @date 2021-05-07
 */
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include <tbb/parallel_for.h>
#include <tbb/parallel_invoke.h>
#include <tbb/pipeline.h>
#include <boost/throw_exception.hpp>
#include <memory>
#include <tuple>
#include <variant>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::crypto;
using namespace bcos::protocol;

MemoryStorage::MemoryStorage(TxPoolConfig::Ptr _config, size_t _notifyWorkerNum,
    int64_t _txsExpirationTime, bool _preStoreTxs)
  : m_config(_config), m_txsExpirationTime(_txsExpirationTime), m_preStoreTxs(_preStoreTxs)
{
    m_notifier = std::make_shared<ThreadPool>("txNotifier", _notifyWorkerNum);
    m_worker = std::make_shared<ThreadPool>("txpoolWorker", 1);
    m_blockNumberUpdatedTime = utcTime();
    // Trigger a transaction cleanup operation every 3s
    m_cleanUpTimer = std::make_shared<Timer>(3000, "txpoolTimer");
    m_cleanUpTimer->registerTimeoutHandler(
        boost::bind(&MemoryStorage::cleanUpExpiredTransactions, this));
    TXPOOL_LOG(INFO) << LOG_DESC("init MemoryStorage of txpool")
                     << LOG_KV("txNotifierWorkerNum", _notifyWorkerNum)
                     << LOG_KV("txsExpriationTime", m_txsExpirationTime)
                     << LOG_KV("preStoreTxs", m_preStoreTxs);
}

void MemoryStorage::start()
{
    if (m_cleanUpTimer)
    {
        m_cleanUpTimer->start();
    }
}

void MemoryStorage::stop()
{
    if (m_notifier)
    {
        m_notifier->stop();
    }
    if (m_worker)
    {
        m_worker->stop();
    }
    if (m_cleanUpTimer)
    {
        m_cleanUpTimer->stop();
    }
}

task::Task<protocol::TransactionSubmitResult::Ptr> MemoryStorage::submitTransaction(
    protocol::Transaction::Ptr transaction)
{
    transaction->setImportTime(utcTime());
    struct Awaitable
    {
        constexpr bool await_ready() { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            try
            {
                auto result = m_self->verifyAndSubmitTransaction(
                    std::move(m_transaction),
                    [this, m_handle = handle](Error::Ptr error,
                        bcos::protocol::TransactionSubmitResult::Ptr result) mutable {
                        if (error)
                        {
                            m_submitResult.emplace<Error::Ptr>(std::move(error));
                        }
                        else
                        {
                            m_submitResult.emplace<bcos::protocol::TransactionSubmitResult::Ptr>(
                                std::move(result));
                        }
                        if (m_handle)
                        {
                            m_handle.resume();
                        }
                    },
                    true, true);

                if (result != TransactionStatus::None)
                {
                    TXPOOL_LOG(ERROR) << "Submit transaction error! " << result;
                    m_submitResult.emplace<Error::Ptr>(
                        BCOS_ERROR_PTR((int32_t)result, "Invalid transaction"));
                    handle.resume();
                }
            }
            catch (std::exception& e)
            {
                m_submitResult.emplace<Error::Ptr>(
                    BCOS_ERROR_PTR((int32_t)TransactionStatus::Malform, "Invalid transaction"));
                handle.resume();
            }
        }
        bcos::protocol::TransactionSubmitResult::Ptr await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_submitResult))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_submitResult));
            }

            return std::move(
                std::get<bcos::protocol::TransactionSubmitResult::Ptr>(m_submitResult));
        }

        protocol::Transaction::Ptr m_transaction;
        std::shared_ptr<MemoryStorage> m_self;
        std::variant<std::monostate, bcos::protocol::TransactionSubmitResult::Ptr, Error::Ptr>
            m_submitResult;
    };

    Awaitable awaitable{
        .m_transaction = transaction, .m_self = shared_from_this(), .m_submitResult = {}};
    co_return co_await awaitable;
}

TransactionStatus MemoryStorage::txpoolStorageCheck(Transaction::ConstPtr _tx)
{
    auto txHash = _tx->hash();
    if (m_txsTable.count(txHash))
    {
        return TransactionStatus::AlreadyInTxPool;
    }
    return TransactionStatus::None;
}

// Note: the signature of the tx has already been verified
TransactionStatus MemoryStorage::enforceSubmitTransaction(Transaction::Ptr _tx)
{
    auto txHash = _tx->hash();
    // the transaction has already onChain, reject it
    auto result = m_config->txValidator()->submittedToChain(_tx);
    auto it = m_txsTable.find(txHash);
    Transaction::ConstPtr tx = nullptr;
    if (it != m_txsTable.end())
    {
        tx = it->second;
    }
    if (result == TransactionStatus::NonceCheckFail)
    {
        if (tx)
        {
            TXPOOL_LOG(WARNING) << LOG_DESC("enforce to seal failed for nonce check failed: ")
                                << tx->hash().abridged() << LOG_KV("batchId", tx->batchId())
                                << LOG_KV("batchHash", tx->batchHash().abridged())
                                << LOG_KV("importBatchId", _tx->batchId())
                                << LOG_KV("importBatchHash", _tx->batchHash().abridged());
        }
        return TransactionStatus::NonceCheckFail;
    }
    if (tx)
    {
        if (!tx->sealed() || tx->batchHash() == HashType())
        {
            if (!tx->sealed())
            {
                m_sealedTxsSize++;
                tx->setSealed(true);
            }
            tx->setBatchId(_tx->batchId());
            tx->setBatchHash(_tx->batchHash());
            TXPOOL_LOG(TRACE) << LOG_DESC("enforce to seal:") << tx->hash().abridged()
                              << LOG_KV("num", tx->batchId())
                              << LOG_KV("hash", tx->batchHash().abridged());
            return TransactionStatus::None;
        }
        // sealed for the same proposal
        if (tx->batchId() == _tx->batchId() && tx->batchHash() == _tx->batchHash())
        {
            return TransactionStatus::None;
        }
        TXPOOL_LOG(WARNING) << LOG_DESC("enforce to seal failed: ") << tx->hash().abridged()
                            << LOG_KV("batchId", tx->batchId())
                            << LOG_KV("batchHash", tx->batchHash().abridged())
                            << LOG_KV("importBatchId", _tx->batchId())
                            << LOG_KV("importBatchHash", _tx->batchHash().abridged());
        // The transaction has already been sealed by another node
        return TransactionStatus::AlreadyInTxPool;
    }
    auto status = insertWithoutLock(_tx);
    if (status != TransactionStatus::None)
    {
        auto tx = m_txsTable.at(_tx->hash());
        TXPOOL_LOG(WARNING) << LOG_DESC("insertWithoutLock failed for already has the tx")
                            << LOG_KV("hash", tx->hash().abridged())
                            << LOG_KV("status", tx->sealed());
        if (!tx->sealed())
        {
            tx->setSealed(true);
            m_sealedTxsSize++;
        }
    }
    else
    {
        // avoid the sealed txs be sealed again
        _tx->setSealed(true);
        m_sealedTxsSize++;
    }
    return TransactionStatus::None;
}

TransactionStatus MemoryStorage::verifyAndSubmitTransaction(
    Transaction::Ptr _tx, TxSubmitCallback _txSubmitCallback, bool _checkPoolLimit, bool _lock)
{
    // start stat the tps when receive first new tx from the sdk
    if (m_tpsStatstartTime.load() == 0 && m_txsTable.size() == 0)
    {
        m_tpsStatstartTime = utcTime();
    }
    // Note: In order to ensure that transactions can reach all nodes, transactions from P2P are not
    // restricted
    if (_checkPoolLimit && m_txsTable.size() >= m_config->poolLimit())
    {
        return TransactionStatus::TxPoolIsFull;
    }
    auto result = txpoolStorageCheck(_tx);
    if (result != TransactionStatus::None)
    {
        return result;
    }
    // verify the transaction
    result = m_config->txValidator()->verify(_tx);
    if (result == TransactionStatus::None)
    {
        if (_txSubmitCallback)
        {
            _tx->setSubmitCallback(_txSubmitCallback);
        }
        if (_lock)
        {
            result = insert(_tx);
        }
        else
        {
            result = insertWithoutLock(_tx);
        }
    }
    else
    {
        return result;
    }
    return result;
}

void MemoryStorage::notifyInvalidReceipt(
    HashType const& _txHash, TransactionStatus _status, TxSubmitCallback _txSubmitCallback)
{
    if (!_txSubmitCallback)
    {
        return;
    }
    // notify txResult
    auto txResult = m_config->txResultFactory()->createTxSubmitResult();
    txResult->setTxHash(_txHash);
    txResult->setStatus((uint32_t)_status);
    std::stringstream errorMsg;
    errorMsg << _status;
    _txSubmitCallback(std::make_shared<Error>((int32_t)_status, errorMsg.str()), txResult);
    TXPOOL_LOG(WARNING) << LOG_DESC("notifyReceipt: reject invalid tx")
                        << LOG_KV("tx", _txHash.abridged()) << LOG_KV("exception", _status);
}

TransactionStatus MemoryStorage::insert(Transaction::ConstPtr _tx)
{
    ReadGuard l(x_txpoolMutex);
    return insertWithoutLock(_tx);
}

TransactionStatus MemoryStorage::insertWithoutLock(Transaction::ConstPtr _tx)
{
    // check again to ensure the same transaction not be imported many times
    if (m_txsTable.count(_tx->hash()))
    {
        return TransactionStatus::AlreadyInTxPool;
    }
    auto result = m_txsTable.insert(std::make_pair(_tx->hash(), _tx));
    if (!result.second)
    {
        return TransactionStatus::AlreadyInTxPool;
    }
    m_onReady();
    if (m_preStoreTxs)
    {
        preCommitTransaction(_tx);
    }
    notifyUnsealedTxsSize();
#if FISCO_DEBUG
    // TODO: remove this, now just for bug tracing
    TXPOOL_LOG(DEBUG) << LOG_DESC("submit tx:") << _tx->hash().abridged()
                      << LOG_KV("txPointer", _tx);
#endif
    return TransactionStatus::None;
}

void MemoryStorage::preCommitTransaction(Transaction::ConstPtr _tx)
{
    auto self = weak_from_this();
    m_worker->enqueue([self, _tx]() {
        try
        {
            auto txpoolStorage = self.lock();
            if (!txpoolStorage)
            {
                return;
            }
            // the transaction has already been stored to backend
            if (_tx->storeToBackend())
            {
                return;
            }
            bcos::bytes encodeData;
            _tx->encode(encodeData);
            auto txsToStore = std::make_shared<std::vector<bytesConstPtr>>();
            txsToStore->emplace_back(std::make_shared<bytes>(std::move(encodeData)));
            auto txsHash = std::make_shared<HashList>();
            auto txHash = _tx->hash();
            txsHash->emplace_back(txHash);
            txpoolStorage->m_config->ledger()->asyncStoreTransactions(
                txsToStore, txsHash, [_tx, txHash](Error::Ptr _error) {
                    if (_error == nullptr)
                    {
                        _tx->setStoreToBackend(true);
                        return;
                    }
                    TXPOOL_LOG(WARNING) << LOG_DESC("asyncPreStoreTransaction failed")
                                        << LOG_KV("errorCode", _error->errorCode())
                                        << LOG_KV("errorMsg", _error->errorMessage())
                                        << LOG_KV("tx", txHash.abridged());
                });
        }
        catch (std::exception const& e)
        {
            TXPOOL_LOG(WARNING) << LOG_DESC("preCommitTransaction exception")
                                << LOG_KV("error", boost::diagnostic_information(e))
                                << LOG_KV("tx", _tx->hash().abridged());
        }
    });
}

void MemoryStorage::batchInsert(Transactions const& _txs)
{
    for (auto tx : _txs)
    {
        insert(tx);
    }
    WriteGuard l(x_missedTxs);
    for (auto tx : _txs)
    {
        m_missedTxs.unsafe_erase(tx->hash());
    }
}

Transaction::ConstPtr MemoryStorage::removeWithoutLock(HashType const& _txHash)
{
    auto it = m_txsTable.find(_txHash);
    if (it == m_txsTable.end())
    {
        return nullptr;
    }
    auto tx = std::move(it->second);
    if (tx && tx->sealed())
    {
        --m_sealedTxsSize;
    }
    m_txsTable.unsafe_erase(it);
#if FISCO_DEBUG
    // TODO: remove this, now just for bug tracing
    TXPOOL_LOG(DEBUG) << LOG_DESC("remove tx: ") << tx->hash().abridged()
                      << LOG_KV("index", tx->batchId())
                      << LOG_KV("hash", tx->batchHash().abridged()) << LOG_KV("txPointer", tx);
#endif
    return tx;
}

Transaction::ConstPtr MemoryStorage::remove(HashType const& _txHash)
{
    WriteGuard l(x_txpoolMutex);
    auto tx = removeWithoutLock(_txHash);
    notifyUnsealedTxsSize();
    return tx;
}

Transaction::ConstPtr MemoryStorage::removeSubmittedTxWithoutLock(
    TransactionSubmitResult::Ptr txSubmitResult, bool _notify)
{
    auto tx = removeWithoutLock(txSubmitResult->txHash());
    if (!tx)
    {
        return nullptr;
    }
    if (_notify)
    {
        notifyTxResult(*tx, std::move(txSubmitResult));
    }
    return tx;
}

Transaction::ConstPtr MemoryStorage::removeSubmittedTx(TransactionSubmitResult::Ptr txSubmitResult)
{
    auto tx = remove(txSubmitResult->txHash());
    if (!tx)
    {
        return nullptr;
    }
    notifyTxResult(*tx, std::move(txSubmitResult));
    return tx;
}
void MemoryStorage::notifyTxResult(
    Transaction const& transaction, TransactionSubmitResult::Ptr txSubmitResult)
{
    const auto& txSubmitCallback = transaction.submitCallback();
    if (!txSubmitCallback)
    {
        return;
    }

    auto txHash = transaction.hash();
    txSubmitResult->setSender(std::string(transaction.sender()));
    txSubmitResult->setTo(std::string(transaction.to()));
    try
    {
        txSubmitCallback(nullptr, std::move(txSubmitResult));
    }
    catch (std::exception const& e)
    {
        TXPOOL_LOG(WARNING) << LOG_DESC("notifyTxResult failed") << LOG_KV("tx", txHash.abridged())
                            << LOG_KV("errorInfo", boost::diagnostic_information(e));
    }
}

// TODO: remove this, now just for bug tracing
void MemoryStorage::printPendingTxs()
{
    if (m_printed)
    {
        return;
    }
    if (utcTime() - m_blockNumberUpdatedTime <= 1000 * 50)
    {
        return;
    }
    if (unSealedTxsSize() > 0 || m_txsTable.size() == 0)
    {
        return;
    }
    TXPOOL_LOG(DEBUG) << LOG_DESC("printPendingTxs for some txs unhandle")
                      << LOG_KV("pendingSize", m_txsTable.size());
    for (auto item : m_txsTable)
    {
        auto tx = item.second;
        if (!tx)
        {
            continue;
        }
        TXPOOL_LOG(DEBUG) << LOG_KV("hash", tx->hash().abridged()) << LOG_KV("id", tx->batchId())
                          << LOG_KV("hash", tx->batchHash().abridged())
                          << LOG_KV("seal", tx->sealed());
    }
    TXPOOL_LOG(DEBUG) << LOG_DESC("printPendingTxs for some txs unhandle finish");
    m_printed = true;
}
void MemoryStorage::batchRemove(BlockNumber batchId, TransactionSubmitResults const& txsResult)
{
    auto startT = utcTime();
    auto recordT = startT;
    int64_t lockT = 0;
    m_blockNumberUpdatedTime = recordT;
    size_t succCount = 0;
    NonceListPtr nonceList = std::make_shared<NonceList>();

    std::vector<std::tuple<Transaction::ConstPtr, TransactionSubmitResult::Ptr>> results;
    results.reserve(txsResult.size());
    {
        WriteGuard lock(x_txpoolMutex);
        for (const auto& it : txsResult)
        {
            auto const& txResult = it;
            auto tx = removeWithoutLock(txResult->txHash());
            if (!tx && txResult->nonce() != NonceType(-1))
            {
                nonceList->emplace_back(txResult->nonce());
            }
            else if (tx)
            {
                ++succCount;
                nonceList->emplace_back(tx->nonce());
            }
            results.emplace_back(std::tuple{std::move(tx), txResult});
        }

        if (batchId > m_blockNumber)
        {
            m_blockNumber = batchId;
        }
        lockT = utcTime() - startT;
    }

    m_onChainTxsCount += txsResult.size();
    // stop stat the tps when there has no pending txs
    if (m_tpsStatstartTime.load() > 0 && m_txsTable.size() == 0)
    {
        auto totalTime = (utcTime() - m_tpsStatstartTime);
        if (totalTime > 0)
        {
            auto tps = (m_onChainTxsCount * 1000) / totalTime;
            TXPOOL_LOG(INFO) << METRIC << LOG_DESC("StatTPS") << LOG_KV("tps", tps)
                             << LOG_KV("totalTime", totalTime);
        }
        m_tpsStatstartTime.store(0);
        m_onChainTxsCount.store(0);
    }

    auto removeT = utcTime() - startT;
    startT = utcTime();
    notifyUnsealedTxsSize();
    // update the ledger nonce
    m_config->txValidator()->ledgerNonceChecker()->batchInsert(batchId, nonceList);
    auto updateLedgerNonceT = utcTime() - startT;
    startT = utcTime();
    // update the txpool nonce
    m_config->txPoolNonceChecker()->batchRemove(*nonceList);
    auto updateTxPoolNonceT = utcTime() - startT;

    for (auto& [tx, txResult] : results)
    {
        if (tx)
        {
            notifyTxResult(*tx, std::move(txResult));
        }
    }

    TXPOOL_LOG(INFO) << METRIC << LOG_DESC("batchRemove txs success")
                     << LOG_KV("expectedSize", txsResult.size()) << LOG_KV("succCount", succCount)
                     << LOG_KV("batchId", batchId) << LOG_KV("timecost", (utcTime() - recordT))
                     << LOG_KV("lockT", lockT) << LOG_KV("removeT", removeT)
                     << LOG_KV("updateLedgerNonceT", updateLedgerNonceT)
                     << LOG_KV("updateTxPoolNonceT", updateTxPoolNonceT);
}

TransactionsPtr MemoryStorage::fetchTxs(HashList& _missedTxs, HashList const& _txs)
{
    ReadGuard l(x_txpoolMutex);
    auto fetchedTxs = std::make_shared<Transactions>();
    _missedTxs.clear();
    for (auto const& hash : _txs)
    {
        auto it = m_txsTable.find(hash);
        if (it == m_txsTable.end())
        {
            _missedTxs.emplace_back(hash);
            continue;
        }
        auto tx = it->second;
        fetchedTxs->emplace_back(std::const_pointer_cast<Transaction>(tx));
    }
    if (c_fileLogLevel <= TRACE)
        [[unlikely]]
        {
            for (auto const& tx : _missedTxs)
            {
                TXPOOL_LOG(TRACE) << "miss: " << tx.abridged();
            }
        }
    return fetchedTxs;
}

ConstTransactionsPtr MemoryStorage::fetchNewTxs(size_t _txsLimit)
{
    ReadGuard l(x_txpoolMutex);
    auto fetchedTxs = std::make_shared<ConstTransactions>();
    fetchedTxs->reserve(_txsLimit);

    for (auto const& it : m_txsTable)
    {
        auto& tx = it.second;
        // Note: When inserting data into tbb::concurrent_unordered_map while traversing, it.second
        // will occasionally be a null pointer.
        if (!tx || tx->synced())
        {
            continue;
        }
        tx->setSynced(true);
        fetchedTxs->emplace_back(tx);
        if (fetchedTxs->size() >= _txsLimit)
        {
            break;
        }
    }
    return fetchedTxs;
}

void MemoryStorage::batchFetchTxs(Block::Ptr _txsList, Block::Ptr _sysTxsList, size_t _txsLimit,
    TxsHashSetPtr _avoidTxs, bool _avoidDuplicate)
{
    TXPOOL_LOG(INFO) << LOG_DESC("begin batchFetchTxs") << LOG_KV("pendingTxs", m_txsTable.size())
                     << LOG_KV("limit", _txsLimit);
    auto blockFactory = m_config->blockFactory();
    auto recordT = utcTime();
    auto startT = utcTime();
    ReadGuard l(x_txpoolMutex);
    auto lockT = utcTime() - startT;
    startT = utcTime();
    int64_t currentTime = (int64_t)utcTime();
    size_t traverseCount = 0;
    for (auto const& it : m_txsTable)
    {
        traverseCount++;
        auto tx = it.second;
        // Note: When inserting data into tbb::concurrent_unordered_map while traversing,
        // it.second will occasionally be a null pointer.
        if (!tx)
        {
            continue;
        }
        // only seal the txs have been stored to the backend
        if (m_preStoreTxs && !tx->storeToBackend())
        {
            continue;
        }
        auto txHash = tx->hash();
        if (m_invalidTxs.count(txHash))
        {
            continue;
        }
        // the transaction has already been sealed for newer proposal
        if (_avoidDuplicate && tx->sealed())
        {
            continue;
        }
        if (currentTime > (tx->importTime() + m_txsExpirationTime))
        {
            // add to m_invalidTxs to be deleted
            m_invalidTxs.insert(txHash);
            m_invalidNonces.insert(tx->nonce());
            continue;
        }
        /// check nonce again when obtain transactions
        // since the invalid nonce has already been checked before the txs import into the
        // txPool the txs with duplicated nonce here are already-committed, but have not been
        // dropped
        auto result = m_config->txValidator()->submittedToChain(tx);
        if (result == TransactionStatus::NonceCheckFail)
        {
            // in case of the same tx notified more than once
            auto transaction = std::const_pointer_cast<Transaction>(tx);
            transaction->takeSubmitCallback();
            // add to m_invalidTxs to be deleted
            m_invalidTxs.insert(txHash);
            m_invalidNonces.insert(tx->nonce());
            continue;
        }
        // blockLimit expired
        if (result == TransactionStatus::BlockLimitCheckFail)
        {
            m_invalidTxs.insert(txHash);
            m_invalidNonces.insert(tx->nonce());
            continue;
        }
        if (_avoidTxs && _avoidTxs->count(txHash))
        {
            continue;
        }
        auto txMetaData = m_config->blockFactory()->createTransactionMetaData();

        txMetaData->setHash(tx->hash());
        txMetaData->setTo(std::string(tx->to()));
        txMetaData->setAttribute(tx->attribute());
        if (tx->systemTx())
        {
            _sysTxsList->appendTransactionMetaData(txMetaData);
        }
        else
        {
            _txsList->appendTransactionMetaData(txMetaData);
        }
        if (!tx->sealed())
        {
            m_sealedTxsSize++;
        }
#if FISCO_DEBUG
        // TODO: remove this, now just for bug tracing
        TXPOOL_LOG(INFO) << LOG_DESC("fetch ") << tx->hash().abridged()
                         << LOG_KV("sealed", tx->sealed()) << LOG_KV("batchId", tx->batchId())
                         << LOG_KV("batchHash", tx->batchHash().abridged())
                         << LOG_KV("txPointer", tx);
#endif
        tx->setSealed(true);
        tx->setBatchId(-1);
        tx->setBatchHash(HashType());
        if ((_txsList->transactionsMetaDataSize() + _sysTxsList->transactionsMetaDataSize()) >=
            _txsLimit)
        {
            break;
        }
    }
    auto fetchTxsT = utcTime() - startT;
    notifyUnsealedTxsSize();
    removeInvalidTxs();
    TXPOOL_LOG(INFO) << METRIC << LOG_DESC("batchFetchTxs success")
                     << LOG_KV("timecost", (utcTime() - recordT))
                     << LOG_KV("txsSize", _txsList->transactionsMetaDataSize())
                     << LOG_KV("sysTxsSize", _sysTxsList->transactionsMetaDataSize())
                     << LOG_KV("pendingTxs", m_txsTable.size()) << LOG_KV("limit", _txsLimit)
                     << LOG_KV("fetchTxsT", fetchTxsT) << LOG_KV("lockT", lockT)
                     << LOG_KV("traverseCount", traverseCount);
}

void MemoryStorage::removeInvalidTxs()
{
    auto self = weak_from_this();
    m_notifier->enqueue([self]() {
        try
        {
            auto memoryStorage = self.lock();
            if (!memoryStorage)
            {
                return;
            }
            if (memoryStorage->m_invalidTxs.size() == 0)
            {
                return;
            }
            WriteGuard l(memoryStorage->x_txpoolMutex);
            tbb::parallel_invoke(
                [memoryStorage]() {
                    // remove invalid txs
                    for (auto const& txHash : memoryStorage->m_invalidTxs)
                    {
                        auto txResult =
                            memoryStorage->m_config->txResultFactory()->createTxSubmitResult();
                        txResult->setTxHash(txHash);
                        txResult->setStatus((uint32_t)TransactionStatus::BlockLimitCheckFail);
                        // not notify receipt to the sdk when the txs has been removed by
                        // removeInvalidTxs in the cases the txs-expired
                        memoryStorage->removeSubmittedTxWithoutLock(txResult, false);
                    }
                    memoryStorage->notifyUnsealedTxsSize();
                },
                [memoryStorage]() {
                    // remove invalid nonce
                    memoryStorage->m_config->txPoolNonceChecker()->batchRemove(
                        memoryStorage->m_invalidNonces);
                });
            TXPOOL_LOG(DEBUG) << LOG_DESC("removeInvalidTxs")
                              << LOG_KV("size", memoryStorage->m_invalidTxs.size());
            memoryStorage->m_invalidTxs.clear();
            memoryStorage->m_invalidNonces.clear();
        }
        catch (std::exception const& e)
        {
            TXPOOL_LOG(WARNING) << LOG_DESC("removeInvalidTxs exception")
                                << LOG_KV("errorInfo", boost::diagnostic_information(e));
        }
    });
}

void MemoryStorage::clear()
{
    WriteGuard l(x_txpoolMutex);
    m_txsTable.clear();
    m_invalidTxs.clear();
    m_invalidNonces.clear();
    m_missedTxs.clear();
    notifyUnsealedTxsSize();
}

HashListPtr MemoryStorage::filterUnknownTxs(HashList const& _txsHashList, NodeIDPtr _peer)
{
    ReadGuard l(x_txpoolMutex);
    for (auto txHash : _txsHashList)
    {
        auto it = m_txsTable.find(txHash);
        if (it == m_txsTable.end())
        {
            continue;
        }
        auto tx = it->second;
        if (!tx)
        {
            continue;
        }
        tx->appendKnownNode(_peer);
    }
    auto unknownTxsList = std::make_shared<HashList>();
    UpgradableGuard missedTxsLock(x_missedTxs);
    for (auto const& txHash : _txsHashList)
    {
        if (m_txsTable.count(txHash))
        {
            continue;
        }
        if (m_missedTxs.count(txHash))
        {
            continue;
        }
        unknownTxsList->push_back(txHash);
        m_missedTxs.insert(txHash);
    }
    if (m_missedTxs.size() >= m_config->poolLimit())
    {
        UpgradeGuard ul(missedTxsLock);
        m_missedTxs.clear();
    }
    return unknownTxsList;
}

void MemoryStorage::batchMarkTxs(
    HashList const& _txsHashList, BlockNumber _batchId, HashType const& _batchHash, bool _sealFlag)
{
    if (_sealFlag)
    {
        ReadGuard l(x_txpoolMutex);
        batchMarkTxsWithoutLock(_txsHashList, _batchId, _batchHash, _sealFlag);
        return;
    }
    // Note: setting flag to false is pessimistic, use writeLock here in case of the same txs has
    // been sealed twice
    WriteGuard l(x_txpoolMutex);
    batchMarkTxsWithoutLock(_txsHashList, _batchId, _batchHash, _sealFlag);
    return;
}

void MemoryStorage::batchMarkTxsWithoutLock(
    HashList const& _txsHashList, BlockNumber _batchId, HashType const& _batchHash, bool _sealFlag)
{
    auto recordT = utcTime();
    auto startT = utcTime();
    ssize_t successCount = 0;
    for (auto txHash : _txsHashList)
    {
        auto it = m_txsTable.find(txHash);
        if (it == m_txsTable.end())
        {
            TXPOOL_LOG(TRACE) << LOG_DESC("batchMarkTxs: missing transaction")
                              << LOG_KV("tx", txHash.abridged()) << LOG_KV("sealFlag", _sealFlag);
            continue;
        }
        auto tx = it->second;
        if (!tx)
        {
            continue;
        }
        // the tx has already been re-sealed, can not enforce unseal
        if ((tx->batchId() != _batchId || tx->batchHash() != _batchHash) && tx->sealed() &&
            !_sealFlag)
        {
            continue;
        }
        if (_sealFlag && !tx->sealed())
        {
            m_sealedTxsSize++;
        }
        if (!_sealFlag && tx->sealed())
        {
            m_sealedTxsSize--;
        }
        tx->setSealed(_sealFlag);
        successCount += 1;
        // set the block information for the transaction
        if (_sealFlag)
        {
            tx->setBatchId(_batchId);
            tx->setBatchHash(_batchHash);
        }
#if FISCO_DEBUG
        // TODO: remove this, now just for bug tracing
        TXPOOL_LOG(DEBUG) << LOG_DESC("mark ") << tx->hash().abridged() << ":" << _sealFlag
                          << LOG_KV("index", tx->batchId())
                          << LOG_KV("hash", tx->batchHash().abridged()) << LOG_KV("txPointer", tx);
#endif
    }
    TXPOOL_LOG(DEBUG) << LOG_DESC("batchMarkTxs ") << LOG_KV("txsSize", _txsHashList.size())
                      << LOG_KV("batchId", _batchId) << LOG_KV("hash", _batchHash.abridged())
                      << LOG_KV("flag", _sealFlag) << LOG_KV("succ", successCount)
                      << LOG_KV("timecost", utcTime() - recordT)
                      << LOG_KV("markT", (utcTime() - startT));
    notifyUnsealedTxsSize();
}

void MemoryStorage::batchMarkAllTxs(bool _sealFlag)
{
    ReadGuard l(x_txpoolMutex);
    for (auto item : m_txsTable)
    {
        auto tx = item.second;
        if (!tx)
        {
            continue;
        }
        tx->setSealed(_sealFlag);
        if (!_sealFlag)
        {
            tx->setBatchId(-1);
            tx->setBatchHash(HashType());
        }
    }
    if (_sealFlag)
    {
        m_sealedTxsSize = m_txsTable.size();
    }
    else
    {
        m_sealedTxsSize = 0;
    }
    notifyUnsealedTxsSize();
}

size_t MemoryStorage::unSealedTxsSize()
{
    ReadGuard l(x_txpoolMutex);
    return unSealedTxsSizeWithoutLock();
}

size_t MemoryStorage::unSealedTxsSizeWithoutLock()
{
    if (m_txsTable.size() < m_sealedTxsSize)
    {
        m_sealedTxsSize = m_txsTable.size();
        return 0;
    }
    return (m_txsTable.size() - m_sealedTxsSize);
}

void MemoryStorage::notifyUnsealedTxsSize(size_t _retryTime)
{
    // Note: must set the notifier
    if (!m_unsealedTxsNotifier)
    {
        return;
    }

    auto unsealedTxsSize = unSealedTxsSizeWithoutLock();
    auto self = weak_from_this();
    m_unsealedTxsNotifier(unsealedTxsSize, [_retryTime, self](Error::Ptr _error) {
        if (_error == nullptr)
        {
            return;
        }
        TXPOOL_LOG(WARNING) << LOG_DESC("notifyUnsealedTxsSize failed")
                            << LOG_KV("errorCode", _error->errorCode())
                            << LOG_KV("errorMsg", _error->errorMessage());
        auto memoryStorage = self.lock();
        if (!memoryStorage)
        {
            return;
        }
        if (_retryTime >= memoryStorage->c_maxRetryTime)
        {
            return;
        }
        memoryStorage->notifyUnsealedTxsSize((_retryTime + 1));
    });
}

std::shared_ptr<HashList> MemoryStorage::batchVerifyProposal(Block::Ptr _block)
{
    auto missedTxs = std::make_shared<HashList>();
    auto txsSize = _block->transactionsHashSize();
    if (txsSize == 0)
    {
        return missedTxs;
    }
    auto batchId = (_block && _block->blockHeader()) ? _block->blockHeader()->number() : -1;
    auto batchHash = (_block && _block->blockHeader()) ? _block->blockHeader()->hash() :
                                                         bcos::crypto::HashType();
    auto startT = utcTime();
    ReadGuard l(x_txpoolMutex);
    auto lockT = utcTime() - startT;
    startT = utcTime();
    for (size_t i = 0; i < txsSize; i++)
    {
        auto txHash = _block->transactionHash(i);
        if (!(m_txsTable.count(txHash)))
        {
            missedTxs->emplace_back(txHash);
        }
    }
    TXPOOL_LOG(INFO) << LOG_DESC("batchVerifyProposal") << LOG_KV("consNum", batchId)
                     << LOG_KV("hash", batchHash.abridged()) << LOG_KV("txsSize", txsSize)
                     << LOG_KV("lockT", lockT) << LOG_KV("verifyT", (utcTime() - startT));
    return missedTxs;
}

bool MemoryStorage::batchVerifyProposal(std::shared_ptr<HashList> _txsHashList)
{
    ReadGuard l(x_txpoolMutex);
    for (auto const& txHash : *_txsHashList)
    {
        if (!(m_txsTable.count(txHash)))
        {
            return false;
        }
    }
    return true;
}

HashListPtr MemoryStorage::getAllTxsHash()
{
    auto txsHash = std::make_shared<HashList>();
    ReadGuard l(x_txpoolMutex);
    for (auto const& it : m_txsTable)
    {
        auto tx = it.second;
        if (!tx)
        {
            continue;
        }
        txsHash->emplace_back(it.first);
    }
    return txsHash;
}

void MemoryStorage::cleanUpExpiredTransactions()
{
    m_cleanUpTimer->restart();

    // Note: In order to minimize the impact of cleanUp on performance,
    // the normal consensus node does not clear expired txs in m_clearUpTimer, but clears
    // expired txs in the process of sealing txs
    if (m_txsCleanUpSwitch && !m_txsCleanUpSwitch())
    {
        return;
    }
    ReadGuard l(x_txpoolMutex);
    if (m_txsTable.size() == 0)
    {
        return;
    }
    size_t traversedTxsNum = 0;
    size_t erasedTxs = 0;
    int64_t currentTime = utcTime();
    for (auto it = m_txsTable.begin();
         traversedTxsNum <= c_maxTraverseTxsNum && it != m_txsTable.end(); it++)
    {
        auto tx = it->second;
        if (m_invalidTxs.count(tx->hash()))
        {
            continue;
        }
        if (tx->sealed() && tx->batchId() >= m_blockNumber)
        {
            continue;
        }
        // the txs expired or not
        if (currentTime > (tx->importTime() + m_txsExpirationTime))
        {
            m_invalidTxs.insert(tx->hash());
            m_invalidNonces.insert(tx->nonce());
            erasedTxs++;
        }
        traversedTxsNum++;
    }
    TXPOOL_LOG(INFO) << LOG_DESC("cleanUpExpiredTransactions")
                     << LOG_KV("pendingTxs", m_txsTable.size()) << LOG_KV("erasedTxs", erasedTxs);
    removeInvalidTxs();
}


void MemoryStorage::batchImportTxs(TransactionsPtr _txs)
{
    auto recordT = utcTime();
    ReadGuard l(x_txpoolMutex);
    size_t successCount = 0;
    for (auto const& tx : *_txs)
    {
        if (!tx || tx->invalid())
        {
            continue;
        }
        // not checkLimit when receive txs from p2p
        auto ret = verifyAndSubmitTransaction(tx, nullptr, false, false);
        if (ret != TransactionStatus::None)
        {
            TXPOOL_LOG(TRACE) << LOG_DESC("batchImportTxs failed")
                              << LOG_KV("tx", tx->hash().abridged()) << LOG_KV("error", ret);
            continue;
        }
        successCount++;
    }
    notifyUnsealedTxsSize();
    TXPOOL_LOG(DEBUG) << LOG_DESC("batchImportTxs success") << LOG_KV("importTxs", successCount)
                      << LOG_KV("totalTxs", _txs->size()) << LOG_KV("pendingTxs", m_txsTable.size())
                      << LOG_KV("timecost", (utcTime() - recordT));
}

bool MemoryStorage::batchVerifyAndSubmitTransaction(
    bcos::protocol::BlockHeader::Ptr _header, TransactionsPtr _txs)
{
    // use writeGuard here in case of the transaction status will be modified by other
    // interfaces
    auto recordT = utcTime();
    // use writeGuard here in case of the transaction status will be modified by other
    // interfaces
    WriteGuard l(x_txpoolMutex);
    auto lockT = utcTime() - recordT;
    recordT = utcTime();
    for (auto const& tx : *_txs)
    {
        if (!tx || tx->invalid())
        {
            continue;
        }
        auto result = enforceSubmitTransaction(tx);
        if (result != TransactionStatus::None)
        {
            TXPOOL_LOG(WARNING) << LOG_BADGE("batchSubmitTransaction: verify proposal failed")
                                << LOG_KV("tx", tx->hash().abridged()) << LOG_KV("result", result)
                                << LOG_KV("txBatchID", tx->batchId())
                                << LOG_KV("txBatchHash", tx->batchHash().abridged())
                                << LOG_KV("consIndex", _header->number())
                                << LOG_KV("propHash", _header->hash().abridged());
            return false;
        }
    }
    notifyUnsealedTxsSize();
    TXPOOL_LOG(DEBUG) << LOG_DESC("batchVerifyAndSubmitTransaction success")
                      << LOG_KV("totalTxs", _txs->size()) << LOG_KV("lockT", lockT)
                      << LOG_KV("submitT", (utcTime() - recordT));
    return true;
}