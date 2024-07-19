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
#include "bcos-utilities/Common.h"

#include <bcos-protocol/TransactionSubmitResultImpl.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_invoke.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <memory>
#include <thread>
#include <tuple>
#include <variant>

#define CPU_CORES std::thread::hardware_concurrency()
#define BUCKET_SIZE 4 * CPU_CORES

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::crypto;
using namespace bcos::protocol;

MemoryStorage::MemoryStorage(
    TxPoolConfig::Ptr _config, size_t _notifyWorkerNum, uint64_t _txsExpirationTime)
  : m_config(std::move(_config)),
    m_txsTable(BUCKET_SIZE),
    m_invalidTxs(BUCKET_SIZE),
    m_missedTxs(CPU_CORES),
    m_txsExpirationTime(_txsExpirationTime),
    m_inRateCollector("tx_pool_in", 1000),
    m_sealRateCollector("tx_pool_seal", 1000),
    m_removeRateCollector("tx_pool_rm", 1000)
{
    m_blockNumberUpdatedTime = utcTime();
    // Trigger a transaction cleanup operation every 3s
    m_cleanUpTimer = std::make_shared<Timer>(TXPOOL_CLEANUP_TIME, "txpoolTimer");
    m_cleanUpTimer->registerTimeoutHandler([this] { cleanUpExpiredTransactions(); });
    TXPOOL_LOG(INFO) << LOG_DESC("init MemoryStorage of txpool")
                     << LOG_KV("txNotifierWorkerNum", _notifyWorkerNum)
                     << LOG_KV("txsExpirationTime", m_txsExpirationTime)
                     << LOG_KV("poolLimit", m_config->poolLimit()) << LOG_KV("cpuCores", CPU_CORES);
}

void MemoryStorage::start()
{
    if (m_cleanUpTimer)
    {
        m_cleanUpTimer->start();
    }

    m_inRateCollector.start();
    m_sealRateCollector.start();
    m_removeRateCollector.start();
}

void MemoryStorage::stop()
{
    if (m_cleanUpTimer)
    {
        m_cleanUpTimer->stop();
        m_cleanUpTimer->destroy();
    }
    m_inRateCollector.stop();
    m_sealRateCollector.stop();
    m_removeRateCollector.stop();
}

task::Task<protocol::TransactionSubmitResult::Ptr> MemoryStorage::submitTransaction(
    protocol::Transaction::Ptr transaction)
{
    co_return co_await submitTransactionWithHook(transaction, nullptr);
}

task::Task<protocol::TransactionSubmitResult::Ptr> MemoryStorage::submitTransactionWithHook(
    protocol::Transaction::Ptr transaction, std::function<void()> onTxSubmitted)
{
    transaction->setImportTime(utcTime());
    struct Awaitable
    {
        [[maybe_unused]] constexpr bool await_ready() { return false; }
        [[maybe_unused]] void await_suspend(std::coroutine_handle<> handle)
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

                // already in txpool but not sealed in block now
                if (result == TransactionStatus::None && m_onTxSubmitted != nullptr)
                {
                    m_onTxSubmitted();
                }

                if (result != TransactionStatus::None)
                {
                    TXPOOL_LOG(DEBUG)
                        << "Submit transaction failed! "
                        << LOG_KV("TxHash", m_transaction ? m_transaction->hash().hex() : "")
                        << LOG_KV("result", result);
                    m_submitResult.emplace<Error::Ptr>(
                        BCOS_ERROR_PTR((int32_t)result, bcos::protocol::toString(result)));
                    handle.resume();
                }
            }
            catch (std::exception& e)
            {
                TXPOOL_LOG(WARNING) << "Unexpected exception: " << boost::diagnostic_information(e);
                m_submitResult.emplace<Error::Ptr>(
                    BCOS_ERROR_PTR((int32_t)TransactionStatus::Malformed, "Unknown exception"));
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
        std::function<void()> m_onTxSubmitted;
        std::shared_ptr<MemoryStorage> m_self;
        std::variant<std::monostate, bcos::protocol::TransactionSubmitResult::Ptr, Error::Ptr>
            m_submitResult;
    };

    Awaitable awaitable{.m_transaction = std::move(transaction),
        .m_onTxSubmitted = onTxSubmitted,
        .m_self = shared_from_this(),
        .m_submitResult = {}};
    co_return co_await awaitable;
}

task::Task<protocol::TransactionSubmitResult::Ptr> MemoryStorage::submitTransactionWithoutReceipt(
    protocol::Transaction::Ptr transaction)
{
    transaction->setImportTime(utcTime());
    struct Awaitable
    {
        [[maybe_unused]] constexpr bool await_ready() { return false; }
        [[maybe_unused]] void await_suspend(std::coroutine_handle<> handle)
        {
            try
            {
                auto result = m_self->verifyAndSubmitTransaction(
                    std::move(m_transaction), nullptr, true, true);

                if (result != TransactionStatus::None)
                {
                    TXPOOL_LOG(DEBUG)
                        << "Submit transaction failed! "
                        << LOG_KV("TxHash", m_transaction ? m_transaction->hash().hex() : "")
                        << LOG_KV("result", result);
                    m_submitResult.emplace<Error::Ptr>(
                        BCOS_ERROR_PTR((int32_t)result, bcos::protocol::toString(result)));
                }
                else
                {
                    auto res = std::make_shared<TransactionSubmitResultImpl>();
                    res->setStatus(static_cast<uint32_t>(result));
                    m_submitResult.emplace<TransactionSubmitResult::Ptr>(std::move(res));
                }
                handle.resume();
            }
            catch (std::exception& e)
            {
                TXPOOL_LOG(WARNING) << "Unexpected exception: " << boost::diagnostic_information(e);
                m_submitResult.emplace<Error::Ptr>(
                    BCOS_ERROR_PTR((int32_t)TransactionStatus::Malformed, "Unknown exception"));
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

    Awaitable awaitable{.m_transaction = std::move(transaction),
        .m_self = shared_from_this(),
        .m_submitResult = {}};
    co_return co_await awaitable;
}


std::vector<protocol::Transaction::ConstPtr> MemoryStorage::getTransactions(
    RANGES::any_view<bcos::h256, RANGES::category::mask | RANGES::category::sized> hashes)
{
    std::vector<protocol::Transaction::ConstPtr> transactions(RANGES::size(hashes));

    tbb::parallel_for(tbb::blocked_range(0LU, (size_t)RANGES::size(hashes)),
        [this, &hashes, &transactions](const auto& subrange) {
            for (auto i = subrange.begin(); i != subrange.end(); ++i)
            {
                auto const& hash = hashes[i];
                TxsMap::ReadAccessor::Ptr accessor;
                auto exists = m_txsTable.find<TxsMap::ReadAccessor>(accessor, hash);
                if (exists)
                {
                    transactions[i] = accessor->value();
                }
            }
        });

    return transactions;
}

TransactionStatus MemoryStorage::txpoolStorageCheck(
    const Transaction& transaction, protocol::TxSubmitCallback& txSubmitCallback)
{
    auto txHash = transaction.hash();
    TxsMap::ReadAccessor::Ptr accessor;
    auto has = m_txsTable.find<TxsMap::ReadAccessor>(accessor, txHash);
    if (has)
    {
        if (txSubmitCallback && !accessor->value()->submitCallback())
        {
            accessor->value()->setSubmitCallback(std::move(txSubmitCallback));
            return TransactionStatus::AlreadyInTxPoolAndAccept;
        }
        else
        {
            return TransactionStatus::AlreadyInTxPool;
        }
    }
    return TransactionStatus::None;
}

// Note: the signature of the tx has already been verified
TransactionStatus MemoryStorage::enforceSubmitTransaction(Transaction::Ptr _tx)
{
    auto txHash = _tx->hash();
    // the transaction has already onChain, reject it
    {
        // check txpool nonce
        // check ledger tx
        auto result = m_config->txValidator()->checkTxpoolNonce(_tx);
        if (result == TransactionStatus::None)
        {
            result = m_config->txValidator()->checkLedgerNonceAndBlockLimit(_tx);
        }
        Transaction::ConstPtr tx = nullptr;
        {
            TxsMap::ReadAccessor::Ptr accessor;
            auto has = m_txsTable.find<TxsMap::ReadAccessor>(accessor, txHash);
            if (has)
            {
                tx = accessor->value();
            }
        }
        if (result == TransactionStatus::NonceCheckFail) [[unlikely]]
        {
            if (!tx)
            {
                TXPOOL_LOG(WARNING)
                    << LOG_DESC("enforce to seal failed for nonce check failed: ")
                    // << tx->hash().hex() << LOG_KV("batchId", tx->batchId())
                    // << LOG_KV("batchHash", tx->batchHash().abridged())
                    << LOG_KV("importTxHash", txHash) << LOG_KV("importBatchId", _tx->batchId())
                    << LOG_KV("importBatchHash", _tx->batchHash().abridged());
                return TransactionStatus::NonceCheckFail;
            }
        }

        // tx already in txpool
        if (tx) [[likely]]
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
    }

    auto txHasSeal = _tx->sealed();
    _tx->setSealed(true);  // must set seal before insert
    auto status = insertWithoutLock(_tx);
    if (status != TransactionStatus::None)
    {
        _tx->setSealed(txHasSeal);
        Transaction::Ptr tx;
        {
            TxsMap::ReadAccessor::Ptr accessor;
            auto has = m_txsTable.find<TxsMap::ReadAccessor>(accessor, _tx->hash());
            assert(has);  // assume must has
            tx = accessor->value();
        }
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
        m_sealedTxsSize++;
    }
    return TransactionStatus::None;
}

TransactionStatus MemoryStorage::verifyAndSubmitTransaction(
    Transaction::Ptr transaction, TxSubmitCallback txSubmitCallback, bool checkPoolLimit, bool lock)
{
    size_t txsSize = m_txsTable.size();

    auto result = txpoolStorageCheck(*transaction, txSubmitCallback);
    if (result == TransactionStatus::AlreadyInTxPoolAndAccept) [[unlikely]]
    {
        // Note: if rpc is slower than p2p tx sync, we also need to accept this tx and record
        // callback
        return TransactionStatus::None;
    }

    if (result != TransactionStatus::None)
    {
        return result;
    }

    // start stat the tps when receive first new tx from the sdk
    if (m_tpsStatstartTime == 0 && txsSize == 0)
    {
        m_tpsStatstartTime = utcTime();
    }
    // Note: In order to ensure that transactions can reach all nodes, transactions from P2P are not
    // restricted
    if (checkPoolLimit && txsSize >= m_config->poolLimit())
    {
        return TransactionStatus::TxPoolIsFull;
    }

    // verify the transaction
    result = m_config->txValidator()->verify(transaction);
    m_inRateCollector.update(1, true);
    if (result == TransactionStatus::None)
    {
        auto const txImportTime = transaction->importTime();
        auto const txHash = transaction->hash().hex();
        if (txSubmitCallback)
        {
            transaction->setSubmitCallback(std::move(txSubmitCallback));
        }
        if (lock)
        {
            result = insert(std::move(transaction));
        }
        else
        {
            result = insertWithoutLock(std::move(transaction));
        }
        if (c_fileLogLevel == TRACE)
        {
            TXPOOL_LOG(TRACE) << LOG_DESC("submitTxTime") << LOG_KV("txHash", txHash)
                              << LOG_KV("insertTime", utcTime() - txImportTime);
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
    _txSubmitCallback(BCOS_ERROR_PTR((int32_t)_status, errorMsg.str()), txResult);
    TXPOOL_LOG(WARNING) << LOG_DESC("notifyReceipt: reject invalid tx")
                        << LOG_KV("tx", _txHash.abridged()) << LOG_KV("exception", _status);
}

TransactionStatus MemoryStorage::insert(Transaction::Ptr transaction)
{
    return insertWithoutLock(std::move(transaction));
}

TransactionStatus MemoryStorage::insertWithoutLock(Transaction::Ptr transaction)
{
    {
        TxsMap::WriteAccessor::Ptr accessor;
        auto inserted = m_txsTable.insert(accessor, {transaction->hash(), transaction});
        if (!inserted)
        {
            if (transaction->submitCallback() && !accessor->value()->submitCallback())
            {
                accessor->value()->setSubmitCallback(transaction->submitCallback());
                return TransactionStatus::None;
            }
            return TransactionStatus::AlreadyInTxPool;
        }
    }
    m_onReady();

    notifyUnsealedTxsSize();
    return TransactionStatus::None;
}

void MemoryStorage::batchInsert(Transactions const& _txs)
{
    for (const auto& tx : _txs)
    {
        insert(tx);
    }

    for (const auto& tx : _txs)
    {
        m_missedTxs.remove(tx->hash());
    }
}


void MemoryStorage::onTxRemoved(const Transaction::Ptr& _tx, bool needNotifyUnsealedTxsSize)
{
    if (_tx && _tx->sealed())
    {
        --m_sealedTxsSize;
    }
    if (needNotifyUnsealedTxsSize)
    {
        notifyUnsealedTxsSize();
    }
#if FISCO_DEBUG
    // TODO: remove this, now just for bug tracing
    TXPOOL_LOG(DEBUG) << LOG_DESC("remove tx: ") << tx->hash().abridged()
                      << LOG_KV("index", tx->batchId())
                      << LOG_KV("hash", tx->batchHash().abridged()) << LOG_KV("txPointer", tx);
#endif
}

Transaction::Ptr MemoryStorage::removeWithoutNotifyUnseal(HashType const& _txHash)
{
    auto tx = m_txsTable.remove(_txHash);
    if (!tx)
    {
        return nullptr;
    }

    onTxRemoved(tx, false);
    return tx;
}

Transaction::Ptr MemoryStorage::remove(HashType const& _txHash)
{
    auto tx = m_txsTable.remove(_txHash);
    if (!tx)
    {
        return nullptr;
    }

    onTxRemoved(tx, true);
    return tx;
}

Transaction::Ptr MemoryStorage::removeSubmittedTxWithoutLock(
    TransactionSubmitResult::Ptr txSubmitResult, bool _notify)
{
    auto tx = removeWithoutNotifyUnseal(txSubmitResult->txHash());
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

Transaction::Ptr MemoryStorage::removeSubmittedTx(TransactionSubmitResult::Ptr txSubmitResult)
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
    Transaction& transaction, TransactionSubmitResult::Ptr txSubmitResult)
{
    auto txSubmitCallback = transaction.takeSubmitCallback();
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
                            << LOG_KV("message", boost::diagnostic_information(e));
    }
}

void MemoryStorage::printPendingTxs()
{
    if (utcTime() - m_blockNumberUpdatedTime <= 1000 * 50)
    {
        return;
    }
    if (unSealedTxsSize() > 0 || m_txsTable.size() == 0)
    {
        return;
    }
    TXPOOL_LOG(DEBUG) << LOG_DESC("printPendingTxs for some txs unhandled")
                      << LOG_KV("pendingSize", m_txsTable.size());
    m_txsTable.forEach<TxsMap::ReadAccessor>([](const TxsMap::ReadAccessor::Ptr& accessor) {
        auto tx = accessor->value();
        if (!tx)
        {
            return true;
        }
        TXPOOL_LOG(DEBUG) << LOG_DESC("printPendingTxs") << LOG_KV("hash", tx->hash())
                          << LOG_KV("batchId", tx->batchId())
                          << LOG_KV("batchHash", tx->batchHash().abridged())
                          << LOG_KV("sealed", tx->sealed());
        return true;
    });
    TXPOOL_LOG(DEBUG) << LOG_DESC("printPendingTxs for some txs unhandled finish");
}
void MemoryStorage::batchRemove(BlockNumber batchId, TransactionSubmitResults const& txsResult)
{
    auto startT = utcTime();
    auto recordT = startT;
    uint64_t lockT = 0;
    m_blockNumberUpdatedTime = recordT;
    size_t succCount = 0;
    NonceList nonceList;

    auto range =
        txsResult | RANGES::views::transform([](TransactionSubmitResult::Ptr const& _txResult) {
            return std::make_pair(_txResult->txHash(), std::make_pair(nullptr, _txResult));
        });
    std::unordered_map<crypto::HashType, std::pair<Transaction::Ptr, TransactionSubmitResult::Ptr>>
        results(range.begin(), range.end());


    auto txHashes = range | RANGES::views::keys;
    m_txsTable.batchRemove(
        txHashes, [&](bool success, const crypto::HashType& key, Transaction::Ptr const& tx) {
            if (!success)
            {
                return;
            }
            onTxRemoved(tx, false);

            ++succCount;
            results[key].first = std::move(tx);
            m_removeRateCollector.update(1, true);
        });

    if (batchId > m_blockNumber)
    {
        m_blockNumber = batchId;
    }
    lockT = utcTime() - startT;

    m_onChainTxsCount += txsResult.size();
    // stop stat the tps when there has no pending txs
    if (m_tpsStatstartTime.load() > 0 && m_txsTable.empty())
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

    auto nonceListRange = results | RANGES::views::filter([](auto const& _result) {
        const auto& tx = _result.second.first;
        const auto& txResult = _result.second.second;
        return tx == nullptr ? !txResult->nonce().empty() : true;
    }) | RANGES::views::transform([](auto const& _result) {
        const auto& tx = _result.second.first;
        const auto& txResult = _result.second.second;
        return tx != nullptr ? tx->nonce() : txResult->nonce();
    });

    auto nonceListPtr = std::make_shared<NonceList>(nonceListRange.begin(), nonceListRange.end());
    m_config->txValidator()->ledgerNonceChecker()->batchInsert(batchId, nonceListPtr);
    auto updateLedgerNonceT = utcTime() - startT;

    startT = utcTime();
    // update the txpool nonce
    m_config->txPoolNonceChecker()->batchRemove(*nonceListPtr);
    auto updateTxPoolNonceT = utcTime() - startT;

    auto txs2Notify = results | RANGES::views::filter([](auto const& _result) {
        const auto& tx = _result.second.first;
        return tx != nullptr;
    }) | RANGES::views::values;

    tbb::parallel_for_each(txs2Notify.begin(), txs2Notify.end(),
        [&](auto& _result) { notifyTxResult(*_result.first, std::move(_result.second)); });
    // for (auto& [tx, txResult] : txs2Notify)
    // {
    //     notifyTxResult(*tx, std::move(txResult));
    // }

    TXPOOL_LOG(INFO) << METRIC << LOG_DESC("batchRemove txs success")
                     << LOG_KV("expectedSize", txsResult.size()) << LOG_KV("succCount", succCount)
                     << LOG_KV("batchId", batchId) << LOG_KV("timecost", (utcTime() - recordT))
                     << LOG_KV("lockT", lockT) << LOG_KV("removeT", removeT)
                     << LOG_KV("updateLedgerNonceT", updateLedgerNonceT)
                     << LOG_KV("updateTxPoolNonceT", updateTxPoolNonceT);
}

ConstTransactionsPtr MemoryStorage::fetchTxs(HashList& _missedTxs, HashList const& _txs)
{
    auto fetchedTxs = std::make_shared<ConstTransactions>();
    _missedTxs.clear();

    for (auto const& hash : _txs)
    {
        TxsMap::ReadAccessor::Ptr accessor;
        auto has = m_txsTable.find<TxsMap::ReadAccessor>(accessor, hash);
        if (!has)
        {
            _missedTxs.emplace_back(hash);
            continue;
        }
        auto& tx = accessor->value();
        fetchedTxs->emplace_back(tx);
    }
    if (c_fileLogLevel <= TRACE) [[unlikely]]
    {
        for (auto const& tx : _missedTxs)
        {
            TXPOOL_LOG(TRACE) << "miss: " << tx.abridged();
        }
    }
    return fetchedTxs;
}

#if 1
ConstTransactionsPtr MemoryStorage::fetchNewTxs(size_t _txsLimit)
{
    auto fetchedTxs = std::make_shared<ConstTransactions>();
    fetchedTxs->reserve(_txsLimit);


    m_txsTable.forEach<TxsMap::ReadAccessor>([&](TxsMap::ReadAccessor::Ptr accessor) {
        const auto& tx = accessor->value();
        // Note: When inserting data into tbb::concurrent_unordered_map while traversing, it.second
        // will occasionally be a null pointer.
        if (!tx || tx->synced())
        {
            return true;
        }
        tx->setSynced(true);
        fetchedTxs->emplace_back(tx);
        return fetchedTxs->size() < _txsLimit;
    });

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
    auto lockT = utcTime() - startT;
    startT = utcTime();
    auto currentTime = utcTime();
    size_t traverseCount = 0;
    size_t sealed = 0;

    auto handleTx = [&](Transaction::Ptr tx) {
        traverseCount++;
        // Note: When inserting data into tbb::concurrent_unordered_map while traversing,
        // it.second will occasionally be a null pointer.
        if (!tx)
        {
            return false;
        }

        auto txHash = tx->hash();
        // the transaction has already been sealed for newer proposal
        if (_avoidDuplicate && tx->sealed())
        {
            ++sealed;
            return false;
        }

        if (currentTime > (tx->importTime() + m_txsExpirationTime))
        {
            // add to m_invalidTxs to be deleted
            {
                TxsMap::WriteAccessor::Ptr accessor;
                m_invalidTxs.insert(accessor, {txHash, tx});
            }
            return false;
        }

        if (m_invalidTxs.contains(txHash))
        {
            return false;
        }
        /// check nonce again when obtain transactions
        // since the invalid nonce has already been checked before the txs import into the
        // txPool, the txs with duplicated nonce here are already-committed, but have not been
        // dropped
        // check txpool txs, no need to check txpool nonce
        auto result = m_config->txValidator()->checkLedgerNonceAndBlockLimit(tx);
        if (result == TransactionStatus::NonceCheckFail)
        {
            // in case of the same tx notified more than once
            auto transaction = std::const_pointer_cast<Transaction>(tx);
            transaction->takeSubmitCallback();
            // add to m_invalidTxs to be deleted
            TxsMap::WriteAccessor::Ptr accessor;
            m_invalidTxs.insert(accessor, {txHash, tx});
            return false;
        }
        // blockLimit expired
        if (result == TransactionStatus::BlockLimitCheckFail)
        {
            TxsMap::WriteAccessor::Ptr accessor;
            m_invalidTxs.insert(accessor, {txHash, tx});
            return false;
        }
        if (_avoidTxs && _avoidTxs->contains(txHash))
        {
            return false;
        }
        auto txMetaData = m_config->blockFactory()->createTransactionMetaData();

        txMetaData->setHash(tx->hash());
        txMetaData->setTo(std::string(tx->to()));
        txMetaData->setAttribute(tx->attribute());
        if (tx->systemTx())
        {
            _sysTxsList->appendTransactionMetaData(std::move(txMetaData));
        }
        else
        {
            _txsList->appendTransactionMetaData(std::move(txMetaData));
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
        m_knownLatestSealedTxHash = txHash;
        m_sealRateCollector.update(1, true);
        return true;
    };


    if (_avoidDuplicate)
    {
        m_txsTable.forEach<TxsMap::ReadAccessor>(
            m_knownLatestSealedTxHash, [&](TxsMap::ReadAccessor::Ptr accessor) {
                const auto& tx = accessor->value();
                handleTx(tx);
                return (_txsList->transactionsMetaDataSize() +
                           _sysTxsList->transactionsMetaDataSize()) < _txsLimit;
            });
    }
    else
    {
        m_txsTable.forEach<TxsMap::ReadAccessor>([&](TxsMap::ReadAccessor::Ptr accessor) {
            const auto& tx = accessor->value();
            handleTx(tx);
            return (_txsList->transactionsMetaDataSize() +
                       _sysTxsList->transactionsMetaDataSize()) < _txsLimit;
        });
    }
    auto invalidTxsSize = m_invalidTxs.size();
    notifyUnsealedTxsSize();
    removeInvalidTxs(true);

    auto fetchTxsT = utcTime() - startT;
    TXPOOL_LOG(INFO) << METRIC << LOG_DESC("batchFetchTxs success")
                     << LOG_KV("time", (utcTime() - recordT))
                     << LOG_KV("txsSize", _txsList->transactionsMetaDataSize())
                     << LOG_KV("sysTxsSize", _sysTxsList->transactionsMetaDataSize())
                     << LOG_KV("pendingTxs", m_txsTable.size()) << LOG_KV("limit", _txsLimit)
                     << LOG_KV("fetchTxsT", fetchTxsT) << LOG_KV("lockT", lockT)
                     << LOG_KV("invalidBefore", invalidTxsSize)
                     << LOG_KV("invalidNow", m_invalidTxs.size()) << LOG_KV("sealed", sealed)
                     << LOG_KV("traverseCount", traverseCount);
}
#endif

void MemoryStorage::removeInvalidTxs(bool lock)
{
    try
    {
        if (m_invalidTxs.empty())
        {
            return;
        }

        // remove invalid txs
        std::atomic<size_t> txCnt = 0;

        std::unordered_map<bcos::crypto::HashType, bcos::protocol::Transaction::Ptr> txs2Remove;

        m_invalidTxs.clear([&](bool success, const bcos::crypto::HashType& txHash,
                               const bcos::protocol::Transaction::Ptr& tx) {
            if (!success)
            {
                return;
            }

            txCnt++;
            txs2Remove.emplace(txHash, std::move(tx));
        });

        auto invalidNonceList =
            txs2Remove | RANGES::views::values |
            RANGES::views::transform([](auto const& tx2Remove) { return tx2Remove->nonce(); });
        m_config->txPoolNonceChecker()->batchRemove(invalidNonceList | RANGES::to_vector);

        /*
        m_txsTable.batchRemove(txs2Remove | RANGES::views::keys,
            [&](bool success, const crypto::HashType& key, Transaction::Ptr const& tx) {
                if (!success)
                {
                    txs2Remove[key] = nullptr;
                    return;
                }
            });
            */
        for (const auto& tx2Remove : txs2Remove | RANGES::views::keys)
        {
            auto tx = m_txsTable.remove(tx2Remove);
            if (!tx)
            {
                txs2Remove[tx2Remove] = nullptr;
            }
        }

        auto txs2Notify = txs2Remove | RANGES::views::filter([](auto const& tx2Remove) {
            return tx2Remove.second != nullptr;
        });

        for (const auto& [txHash, tx] : txs2Notify)
        {
            auto const& nonce = tx->nonce();
            auto txResult = m_config->txResultFactory()->createTxSubmitResult();
            txResult->setTxHash(txHash);
            txResult->setStatus(static_cast<uint32_t>(TransactionStatus::TransactionPoolTimeout));
            txResult->setNonce(nonce);
            notifyTxResult(*tx, std::move(txResult));
        }
        notifyUnsealedTxsSize();

        TXPOOL_LOG(DEBUG) << LOG_DESC("removeInvalidTxs") << LOG_KV("size", txCnt);
    }
    catch (std::exception const& e)
    {
        TXPOOL_LOG(WARNING) << LOG_DESC("removeInvalidTxs exception")
                            << LOG_KV("message", boost::diagnostic_information(e));
    }
}

void MemoryStorage::clear()
{
    m_txsTable.clear();
    m_invalidTxs.clear();
    m_missedTxs.clear();
    notifyUnsealedTxsSize();
}

HashListPtr MemoryStorage::filterUnknownTxs(HashList const& _txsHashList, NodeIDPtr _peer)
{
    auto missList = std::make_shared<HashList>();
    m_txsTable.batchFind<TxsMap::ReadAccessor>(
        _txsHashList, [&missList](auto const& txHash, TxsMap::ReadAccessor::Ptr accessor) {
            if (!accessor)
            {
                missList->push_back(txHash);
            }
            else
            {
                const auto& tx = accessor->value();
                if (!tx)
                {
                    return true;
                }
            }
            return true;
        });

    auto unknownTxsList = std::make_shared<HashList>();
    m_missedTxs.batchInsert(*missList, [&unknownTxsList](bool success, const HashType& hash,
                                           HashSet::WriteAccessor::Ptr accessor) {
        if (success)
        {
            unknownTxsList->push_back(hash);
        }
    });

    if (m_missedTxs.size() >= m_config->poolLimit())
    {
        m_missedTxs.clear();
    }
    return unknownTxsList;
}

bool MemoryStorage::batchMarkTxs(
    HashList const& _txsHashList, BlockNumber _batchId, HashType const& _batchHash, bool _sealFlag)
{
    return batchMarkTxsWithoutLock(_txsHashList, _batchId, _batchHash, _sealFlag);
}

bool MemoryStorage::batchMarkTxsWithoutLock(
    HashList const& _txsHashList, BlockNumber _batchId, HashType const& _batchHash, bool _sealFlag)
{
    auto recordT = utcTime();
    auto startT = utcTime();
    ssize_t successCount = 0;
    ssize_t notFound = 0;
    ssize_t reSealed = 0;
    for (auto const& txHash : _txsHashList)
    {
        Transaction::Ptr tx;
        {  // TODO: use batchFind
            TxsMap::ReadAccessor::Ptr accessor;
            auto has = m_txsTable.find<TxsMap::ReadAccessor>(accessor, txHash);
            if (!has)
            {
                ++notFound;
                TXPOOL_LOG(TRACE) << LOG_DESC("batchMarkTxs: missing transaction")
                                  << LOG_KV("tx", txHash.abridged())
                                  << LOG_KV("sealFlag", _sealFlag);
                continue;
            }
            tx = accessor->value();
        }
        if (!tx)
        {
            continue;
        }
        // the tx has already been re-sealed, can not enforce unseal
        // if tx batch id is -1 or batchHash is empty, it means node-self generate proposal verify
        // failed, so in this case should unsealed the txs.
        if ((tx->batchId() != _batchId || tx->batchHash() != _batchHash) && tx->sealed() &&
            !_sealFlag)
        {
            ++reSealed;
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
            m_knownLatestSealedTxHash = txHash;
        }
#if FISCO_DEBUG
        // TODO: remove this, now just for bug tracing
        TXPOOL_LOG(DEBUG) << LOG_DESC("mark ") << tx->hash().abridged() << ":" << _sealFlag
                          << LOG_KV("index", tx->batchId())
                          << LOG_KV("hash", tx->batchHash().abridged()) << LOG_KV("txPointer", tx);
#endif
    }
    TXPOOL_LOG(INFO) << LOG_DESC("batchMarkTxs") << LOG_KV("txsSize", _txsHashList.size())
                     << LOG_KV("batchId", _batchId) << LOG_KV("hash", _batchHash.abridged())
                     << LOG_KV("sealFlag", _sealFlag) << LOG_KV("notFound", notFound)
                     << LOG_KV("reSealed", reSealed) << LOG_KV("succ", successCount)
                     << LOG_KV("time", utcTime() - recordT)
                     << LOG_KV("markT", (utcTime() - startT));
    notifyUnsealedTxsSize();
    return notFound == 0;  // return true if all txs have been marked
}

void MemoryStorage::batchMarkAllTxs(bool _sealFlag)
{
    m_txsTable.forEach<TxsMap::ReadAccessor>([&](TxsMap::ReadAccessor::Ptr accessor) {
        auto tx = accessor->value();
        if (!tx)
        {
            return true;
        }
        tx->setSealed(_sealFlag);
        if (!_sealFlag)
        {
            tx->setBatchId(-1);
            tx->setBatchHash(HashType());
        }
        return true;
    });

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
    return unSealedTxsSizeWithoutLock();
}

size_t MemoryStorage::unSealedTxsSizeWithoutLock()
{
    auto txsSize = m_txsTable.size();
    TXPOOL_LOG(TRACE) << LOG_DESC("unSealedTxsSize") << LOG_KV("txsSize", txsSize)
                      << LOG_KV("sealedTxsSize", m_sealedTxsSize);

    if (txsSize < m_sealedTxsSize)
    {
        m_sealedTxsSize = txsSize;
        return 0;
    }
    return (txsSize - m_sealedTxsSize);
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
                            << LOG_KV("code", _error->errorCode())
                            << LOG_KV("msg", _error->errorMessage());
        auto memoryStorage = self.lock();
        if (!memoryStorage)
        {
            return;
        }
        if (_retryTime >= MAX_RETRY_NOTIFY_TIME)
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
    auto lockT = utcTime() - startT;
    startT = utcTime();

    auto txHashes =
        RANGES::iota_view<size_t, size_t>{0, txsSize} |
        RANGES::views::transform([&_block](size_t i) { return _block->transactionHash(i); });
    bool findErrorTxInBlock = false;

    m_txsTable.batchFind<TxsMap::ReadAccessor>(
        txHashes, [&missedTxs, &findErrorTxInBlock, _block](
                      const auto& txHash, TxsMap::ReadAccessor::Ptr accessor) {
            if (!accessor)
            {
                missedTxs->emplace_back(txHash);
            }
            else if (accessor->value()->sealed())
            {
                auto header = _block->blockHeader();
                if ((accessor->value()->batchId() != header->number() &&
                        accessor->value()->batchId() != -1))
                {
                    TXPOOL_LOG(INFO)
                        << LOG_DESC("batchVerifyProposal unexpected wrong tx")
                        << LOG_KV("blkNum", header->number())
                        << LOG_KV("blkHash", header->hash().abridged())
                        << LOG_KV("txHash", accessor->value()->hash().hexPrefixed())
                        << LOG_KV("txBatchId", accessor->value()->batchId())
                        << LOG_KV("txBatchHash", accessor->value()->batchHash().abridged());
                    // NOTE: In certain scenarios, a bug may occur here: The leader generates the
                    // (N)th proposal, which includes transaction A. The local node puts this
                    // proposal into the cache and sets the batchId of transaction A to (N) and the
                    // batchHash to the hash of the (N)th proposal.
                    //
                    // However, at this point, a view change happens, and the next leader completes
                    // the resetTx operation for the (N)th proposal and includes transaction A in
                    // the new block of the (N)th proposal.
                    //
                    // Meanwhile, the local node, due to the lengthy resetTx operation caused by the
                    // view change, has not completed it yet, and it receives the (N+1)th proposal
                    // sent by the new leader. During the verification process, transaction A has a
                    // consistent batchId, but the batchHash doesn't match the one in the (N+1)th
                    // proposal, leading to false positives.
                    //
                    // Therefore, we do not validate the consistency of the batchHash for now.
                    findErrorTxInBlock = true;
                    return false;
                }
            }
            return true;
        });

    TXPOOL_LOG(INFO) << LOG_DESC("batchVerifyProposal") << LOG_KV("consNum", batchId)
                     << LOG_KV("hash", batchHash.abridged()) << LOG_KV("txsSize", txsSize)
                     << LOG_KV("lockT", lockT) << LOG_KV("verifyT", (utcTime() - startT))
                     << LOG_KV("missedTxs", missedTxs->size());
    return findErrorTxInBlock ? nullptr : missedTxs;
}

bool MemoryStorage::batchVerifyProposal(std::shared_ptr<HashList> _txsHashList)
{
    bool has = false;
    m_txsTable.batchFind<TxsMap::ReadAccessor>(
        *_txsHashList, [&has](auto const& txHash, TxsMap::ReadAccessor::Ptr accessor) {
            has = (accessor != nullptr);
            return has;  // break if has is false
        });

    return has;
}

HashListPtr MemoryStorage::getTxsHash(int _limit)
{
    auto txsHash = std::make_shared<HashList>();

    m_txsTable.forEach<TxsMap::ReadAccessor>(
        [this, &txsHash, &_limit](TxsMap::ReadAccessor::Ptr accessor) {
            auto tx = accessor->value();
            if (!tx)
            {
                return true;
            }
            // check txpool txs, no need to check txpool nonce
            auto result = m_config->txValidator()->checkLedgerNonceAndBlockLimit(tx);
            if (result != TransactionStatus::None)
            {
                TxsMap::WriteAccessor::Ptr writeAccessor;
                m_invalidTxs.insert(writeAccessor, {tx->hash(), tx});
                return true;
            }
            if ((int)txsHash->size() >= _limit)
            {
                return false;
            }
            txsHash->emplace_back(accessor->key());
            return true;
        });
    removeInvalidTxs(true);
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

    if (m_txsTable.empty())
    {
        return;
    }
    // printPendingTxs();
    size_t traversedTxsNum = 0;
    size_t erasedTxs = 0;
    size_t sealedTxs = 0;
    uint64_t currentTime = utcTime();

    m_txsTable.forEach<TxsMap::ReadAccessor>([&traversedTxsNum, &sealedTxs, this, &currentTime,
                                                 &erasedTxs](TxsMap::ReadAccessor::Ptr accessor) {
        traversedTxsNum++;
        if (traversedTxsNum > MAX_TRAVERSE_TXS_COUNT)
        {
            return false;
        }

        auto tx = accessor->value();
        if (tx->sealed() &&
            (tx->batchId() >= m_blockNumber || tx->batchId() == -1))  // -1 means seal by my self

        {
            sealedTxs++;
            return true;
        }

        // the txs expired or not
        if (currentTime > (tx->importTime() + m_txsExpirationTime))
        {
            TxsMap::WriteAccessor::Ptr accessor1;
            if (m_invalidTxs.insert(accessor1, {tx->hash(), tx}))
            {
                erasedTxs++;
            }
            else
            {
                // already exist
                return true;
            }
        }
        else
        {
            if (m_invalidTxs.contains(tx->hash()))
            {
                // already exist
                return true;
            }
        }
        // check txpool txs, no need to check txpool nonce
        auto result = m_config->txValidator()->checkLedgerNonceAndBlockLimit(tx);
        // blockLimit expired
        if (result != TransactionStatus::None)
        {
            TxsMap::WriteAccessor::Ptr writeAccessor;
            if (m_invalidTxs.insert(writeAccessor, {tx->hash(), tx}))
            {
                erasedTxs++;
            }
            else
            {  // already exist
                return true;
            }
        }

        if (traversedTxsNum > MAX_TRAVERSE_TXS_COUNT)
        {
            return false;
        }
        return true;
    });

    removeInvalidTxs(true);

    TXPOOL_LOG(INFO) << LOG_DESC("cleanUpExpiredTransactions")
                     << LOG_KV("pendingTxs", m_txsTable.size()) << LOG_KV("erasedTxs", erasedTxs)
                     << LOG_KV("sealedTxs", sealedTxs)
                     << LOG_KV("traversedTxsNum", traversedTxsNum);
}

void MemoryStorage::batchImportTxs(TransactionsPtr _txs)
{
    auto recordT = utcTime();
    size_t successCount = 0;
    for (auto const& tx : *_txs)
    {
        if (!tx || tx->invalid())
        {
            continue;
        }
        // will check ledger nonce, txpool nonce and blockLimit when import txs
        auto ret = verifyAndSubmitTransaction(tx, nullptr, false, false);
        if (ret != TransactionStatus::None)
        {
            TXPOOL_LOG(TRACE) << LOG_DESC("batchImportTxs failed") << LOG_KV("tx", tx->hash().hex())
                              << LOG_KV("msg", ret);
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
                                << LOG_KV("consIndex", _header ? _header->number() : -1)
                                << LOG_KV("propHash", _header ? _header->hash().abridged() : "");
            return false;
        }
    }
    notifyUnsealedTxsSize();
    TXPOOL_LOG(DEBUG) << LOG_DESC("batchVerifyAndSubmitTransaction success")
                      << LOG_KV("totalTxs", _txs->size()) << LOG_KV("lockT", lockT)
                      << LOG_KV("submitT", (utcTime() - recordT));
    return true;
}
