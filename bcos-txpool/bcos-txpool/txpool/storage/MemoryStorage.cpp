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
#include <tbb/parallel_invoke.h>
#include <memory>
#include <tuple>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::crypto;
using namespace bcos::protocol;

MemoryStorage::MemoryStorage(TxPoolConfig::Ptr _config, size_t _notifyWorkerNum) : m_config(_config)
{
    m_notifier = std::make_shared<ThreadPool>("txNotifier", _notifyWorkerNum);
    m_worker = std::make_shared<ThreadPool>("txpoolWorker", 1);
    m_blockNumberUpdatedTime = utcTime();
    TXPOOL_LOG(INFO) << LOG_DESC("init MemoryStorage of txpool")
                     << LOG_KV("txNotifierWorkerNum", _notifyWorkerNum);
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
}

TransactionStatus MemoryStorage::submitTransaction(
    bytesPointer _txData, TxSubmitCallback _txSubmitCallback)
{
    try
    {
        auto tx = m_config->txFactory()->createTransaction(ref(*_txData), false);
        auto result = submitTransaction(tx, _txSubmitCallback);
        if (result != TransactionStatus::None)
        {
            notifyInvalidReceipt(tx->hash(), result, _txSubmitCallback);
        }
        return result;
    }
    catch (std::exception const& e)
    {
        TXPOOL_LOG(WARNING) << LOG_DESC("Invalid transaction for decode exception")
                            << LOG_KV("error", boost::diagnostic_information(e));
        notifyInvalidReceipt(HashType(), TransactionStatus::Malform, _txSubmitCallback);
        return TransactionStatus::Malform;
    }
}

TransactionStatus MemoryStorage::txpoolStorageCheck(Transaction::ConstPtr _tx)
{
    auto txHash = _tx->hash();
    if (exist(txHash))
    {
        return TransactionStatus::AlreadyInTxPool;
    }
    return TransactionStatus::None;
}

// Note: the signature of the tx has already been verified
TransactionStatus MemoryStorage::enforceSubmitTransaction(Transaction::Ptr _tx)
{
    // the transaction has already onChain, reject it
    auto result = m_config->txValidator()->submittedToChain(_tx);
    if (result == TransactionStatus::NonceCheckFail)
    {
        return TransactionStatus::NonceCheckFail;
    }

    {
        auto txHash = _tx->hash();
        // use writeGuard here in case of the transaction status will be modified by other
        // interfaces
        UpgradableGuard l(x_txpoolMutex);
        if (m_txsTable.count(txHash) && m_txsTable.at(txHash))
        {
            auto tx = m_txsTable.at(txHash);
            if (!tx->sealed() || tx->batchHash() == HashType())
            {
                UpgradeGuard ul(l);
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
            // The transaction has already been sealed by another node
            return TransactionStatus::AlreadyInTxPool;
        }
    }

    // enforce import the transaction with duplicated nonce(for the consensus proposal)
    if (!_tx->sealed())
    {
        m_sealedTxsSize++;
        // avoid the sealed txs be sealed again
        _tx->setSealed(true);
    }
    insert(_tx);
    {
        WriteGuard l(x_missedTxs);
        m_missedTxs.unsafe_erase(_tx->hash());
    }
    // TODO: notifyUnsealedTxs()
    return TransactionStatus::None;
}

TransactionStatus MemoryStorage::submitTransaction(Transaction::Ptr _tx,
    TxSubmitCallback _txSubmitCallback, bool _enforceImport, bool _checkPoolLimit)
{
    if (!_enforceImport)
    {
        return verifyAndSubmitTransaction(_tx, _txSubmitCallback, _checkPoolLimit);
    }
    return enforceSubmitTransaction(_tx);
}

TransactionStatus MemoryStorage::verifyAndSubmitTransaction(
    Transaction::Ptr _tx, TxSubmitCallback _txSubmitCallback, bool _checkPoolLimit)
{
    // Note: In order to ensure that transactions can reach all nodes, transactions from P2P are not
    // restricted
    if (_checkPoolLimit && size() >= m_config->poolLimit())
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
        _tx->setImportTime(utcTime());
        if (_txSubmitCallback)
        {
            _tx->setSubmitCallback(_txSubmitCallback);
        }
        result = insert(_tx);
        {
            WriteGuard l(x_missedTxs);
            m_missedTxs.unsafe_erase(_tx->hash());
        }
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
    // check again to ensure the same transaction not be imported many times
    if (m_txsTable.count(_tx->hash()))
    {
        return TransactionStatus::AlreadyInTxPool;
    }
    m_txsTable[_tx->hash()] = _tx;
    m_onReady();
    m_txsQueue.push(_tx);
    preCommitTransaction(_tx);
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
    auto self = std::weak_ptr<MemoryStorage>(shared_from_this());
    m_worker->enqueue([self, _tx]() {
        try
        {
            auto txpoolStorage = self.lock();
            if (!txpoolStorage)
            {
                return;
            }
            auto encodedData = _tx->encode(false);
            auto txsToStore = std::make_shared<std::vector<bytesConstPtr>>();
            txsToStore->emplace_back(
                std::make_shared<bytes>(encodedData.begin(), encodedData.end()));
            auto txsHash = std::make_shared<HashList>();
            auto txHash = _tx->hash();
            txsHash->emplace_back(txHash);
            txpoolStorage->m_config->ledger()->asyncStoreTransactions(
                txsToStore, txsHash, [txHash](Error::Ptr _error) {
                    if (_error == nullptr)
                    {
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
    if (!m_txsTable.count(_txHash))
    {
        return nullptr;
    }
    auto tx = m_txsTable.at(_txHash);
    if (tx && tx->sealed())
    {
        m_sealedTxsSize--;
    }
    m_txsTable.unsafe_erase(_txHash);
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
    TransactionSubmitResult::Ptr _txSubmitResult)
{
    auto tx = removeWithoutLock(_txSubmitResult->txHash());
    if (!tx)
    {
        return nullptr;
    }
    notifyTxResult(tx, _txSubmitResult);
    return tx;
}

Transaction::ConstPtr MemoryStorage::removeSubmittedTx(TransactionSubmitResult::Ptr _txSubmitResult)
{
    auto tx = remove(_txSubmitResult->txHash());
    if (!tx)
    {
        return nullptr;
    }
    notifyTxResult(tx, _txSubmitResult);
    return tx;
}
void MemoryStorage::notifyTxResult(
    Transaction::ConstPtr _tx, TransactionSubmitResult::Ptr _txSubmitResult)
{
    if (!_txSubmitResult)
    {
        return;
    }
    if (!_tx->submitCallback())
    {
        return;
    }
    auto txSubmitCallback = _tx->submitCallback();
    // notify the transaction result to RPC
    auto txHash = _tx->hash();
    _txSubmitResult->setSender(std::string(_tx->sender()));
    _txSubmitResult->setTo(std::string(_tx->to()));
    // Note: Due to tx->setTransactionCallback(), _tx cannot be passed into lamba expression to
    // avoid shared_ptr circular reference
    m_notifier->enqueue([txHash, _txSubmitResult, txSubmitCallback]() {
        try
        {
            txSubmitCallback(nullptr, _txSubmitResult);
        }
        catch (std::exception const& e)
        {
            TXPOOL_LOG(WARNING) << LOG_DESC("notifyTxResult failed")
                                << LOG_KV("tx", txHash.abridged())
                                << LOG_KV("errorInfo", boost::diagnostic_information(e));
        }
    });
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
    if (unSealedTxsSize() > 0 || size() == 0)
    {
        return;
    }
    TXPOOL_LOG(DEBUG) << LOG_DESC("printPendingTxs for some txs unhandle")
                      << LOG_KV("pendingSize", size());
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
void MemoryStorage::batchRemove(BlockNumber _batchId, TransactionSubmitResults const& _txsResult)
{
    m_blockNumberUpdatedTime = utcTime();
    size_t succCount = 0;
    NonceListPtr nonceList = std::make_shared<NonceList>();
    {
        // batch remove
        WriteGuard l(x_txpoolMutex);
        for (auto txResult : _txsResult)
        {
            auto tx = removeSubmittedTxWithoutLock(txResult);

            if (!tx && txResult->nonce() != NonceType(-1))
            {
                nonceList->emplace_back(txResult->nonce());
            }
            else if (tx)
            {
                succCount++;
                nonceList->emplace_back(tx->nonce());
            }
        }
        // Note: must update the blockNumber after the txs removed
        if (_batchId > m_blockNumber)
        {
            m_blockNumber = _batchId;
        }
    }
    notifyUnsealedTxsSize();
    TXPOOL_LOG(INFO) << LOG_DESC("batchRemove txs success")
                     << LOG_KV("expectedSize", _txsResult.size()) << LOG_KV("succCount", succCount)
                     << LOG_KV("batchId", _batchId);
    // update the ledger nonce
    m_config->txValidator()->ledgerNonceChecker()->batchInsert(_batchId, nonceList);
    // update the txpool nonce
    m_config->txPoolNonceChecker()->batchRemove(*nonceList);
}

TransactionsPtr MemoryStorage::fetchTxs(HashList& _missedTxs, HashList const& _txs)
{
    ReadGuard l(x_txpoolMutex);
    auto fetchedTxs = std::make_shared<Transactions>();
    _missedTxs.clear();
    for (auto const& hash : _txs)
    {
        if (!m_txsTable.count(hash))
        {
            _missedTxs.emplace_back(hash);
            continue;
        }
        auto tx = m_txsTable[hash];
        fetchedTxs->emplace_back(std::const_pointer_cast<Transaction>(tx));
    }
    return fetchedTxs;
}

ConstTransactionsPtr MemoryStorage::fetchNewTxs(size_t _txsLimit)
{
    ReadGuard l(x_txpoolMutex);
    auto fetchedTxs = std::make_shared<ConstTransactions>();
    for (auto const& it : m_txsTable)
    {
        auto tx = it.second;
        // Note: When inserting data into tbb::concurrent_unordered_map while traversing, it.second
        // will occasionally be a null pointer.
        if (!tx)
        {
            continue;
        }
        if (tx->synced())
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
    Transaction::ConstPtr tx;
    while (m_txsQueue.size() != 0)
    {
        auto ret = m_txsQueue.try_pop(tx);
        if (!ret)
        {
            break;
        }
        // Note: When inserting data into tbb::concurrent_unordered_map while traversing,
        // it.second will occasionally be a null pointer.
        if (!tx)
        {
            continue;
        }
        auto txHash = tx->hash();
        if (m_invalidTxs.count(txHash))
        {
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
            m_invalidTxs.insert(tx->nonce());
            continue;
        }
        // blockLimit expired
        if (result == TransactionStatus::BlockLimitCheckFail && !tx->sealed())
        {
            m_invalidTxs.insert(txHash);
            m_invalidNonces.insert(tx->nonce());
            continue;
        }
        if (_avoidTxs && _avoidTxs->count(txHash))
        {
            continue;
        }
        // the transaction has already been sealed for newer proposal
        if (_avoidDuplicate && tx->sealed())
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
    TXPOOL_LOG(INFO) << LOG_DESC("batchFetchTxs success")
                     << LOG_KV("timecost", (utcTime() - recordT))
                     << LOG_KV("txsSize", _txsList->transactionsMetaDataSize())
                     << LOG_KV("sysTxsSize", _sysTxsList->transactionsMetaDataSize())
                     << LOG_KV("queueSize", m_txsQueue.size())
                     << LOG_KV("pendingTxs", m_txsTable.size()) << LOG_KV("limit", _txsLimit)
                     << LOG_KV("fetchTxsT", fetchTxsT) << LOG_KV("lockT", lockT);
}

void MemoryStorage::removeInvalidTxs()
{
    auto self = std::weak_ptr<MemoryStorage>(shared_from_this());
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

                        memoryStorage->removeSubmittedTxWithoutLock(txResult);
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
}

HashListPtr MemoryStorage::filterUnknownTxs(HashList const& _txsHashList, NodeIDPtr _peer)
{
    ReadGuard l(x_txpoolMutex);
    for (auto txHash : _txsHashList)
    {
        if (!m_txsTable.count(txHash))
        {
            continue;
        }
        auto tx = m_txsTable[txHash];
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
    ReadGuard l(x_txpoolMutex);
    ssize_t successCount = 0;
    for (auto txHash : _txsHashList)
    {
        if (!m_txsTable.count(txHash))
        {
            TXPOOL_LOG(TRACE) << LOG_DESC("batchMarkTxs: missing transaction")
                              << LOG_KV("tx", txHash.abridged()) << LOG_KV("sealFlag", _sealFlag);
            continue;
        }
        auto tx = m_txsTable[txHash];
        if (!tx)
        {
            continue;
        }
        // the tx has already been re-sealed, can not enforce unseal
        if (tx->batchHash() != HashType() && tx->batchHash() != _batchHash && !_sealFlag)
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
        if (!_sealFlag)
        {
            m_txsQueue.push(tx);
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
                      << LOG_KV("flag", _sealFlag) << LOG_KV("succ", successCount);
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
        if (!_sealFlag)
        {
            m_txsQueue.push(tx);
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

size_t MemoryStorage::size() const
{
    ReadGuard l(x_txpoolMutex);
    return m_txsTable.size();
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
    m_unsealedTxsNotifier(unsealedTxsSize, [_retryTime, this](Error::Ptr _error) {
        if (_error == nullptr)
        {
            return;
        }
        TXPOOL_LOG(WARNING) << LOG_DESC("notifyUnsealedTxsSize failed")
                            << LOG_KV("errorCode", _error->errorCode())
                            << LOG_KV("errorMsg", _error->errorMessage());
        if (_retryTime >= c_maxRetryTime)
        {
            return;
        }
        this->notifyUnsealedTxsSize((_retryTime + 1));
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
