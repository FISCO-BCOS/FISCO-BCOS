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
#include "bcos-task/Wait.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/ITTAPI.h"
#include <bcos-protocol/TransactionSubmitResultImpl.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_invoke.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <memory>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/transform.hpp>
#include <variant>

const static auto CPU_CORES = std::thread::hardware_concurrency();
const static auto BUCKET_SIZE = 4 * CPU_CORES;

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
    m_blockNumberUpdatedTime(utcTime()),
    m_txsExpirationTime(_txsExpirationTime),
    m_cleanUpTimer(std::make_shared<Timer>(TXPOOL_CLEANUP_TIME, "txpoolTimer")),
    m_txsSizeNotifierTimer(std::make_shared<Timer>(TXS_SIZE_NOTIFY_TIME, "txsNotifier"))
{
    // Trigger a transaction cleanup operation every 3s
    m_cleanUpTimer->registerTimeoutHandler([this] { cleanUpExpiredTransactions(); });
    m_txsSizeNotifierTimer->registerTimeoutHandler([this] { notifyTxsSize(); });
    TXPOOL_LOG(INFO) << LOG_DESC("init MemoryStorage of txpool")
                     << LOG_KV("txNotifierWorkerNum", _notifyWorkerNum)
                     << LOG_KV("txsExpirationTime", m_txsExpirationTime)
                     << LOG_KV("poolLimit", m_config->poolLimit()) << LOG_KV("cpuCores", CPU_CORES);
}

void MemoryStorage::start()
{
    m_cleanUpTimer->start();
    m_txsSizeNotifierTimer->start();
}

void MemoryStorage::stop()
{
    m_cleanUpTimer->stop();
    m_cleanUpTimer->destroy();
    m_txsSizeNotifierTimer->stop();
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
        [[maybe_unused]] static constexpr bool await_ready() { return false; }
        [[maybe_unused]] void await_suspend(std::coroutine_handle<> handle)
        {
            try
            {
                auto result = m_self->verifyAndSubmitTransaction(
                    m_transaction,
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
        .m_onTxSubmitted = std::move(onTxSubmitted),
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
        [[maybe_unused]] static constexpr bool await_ready() { return false; }
        [[maybe_unused]] void await_suspend(std::coroutine_handle<> handle)
        {
            try
            {
                auto const txHash = m_transaction->hash();
                auto const from = std::string(m_transaction->sender());
                auto const to = std::string(m_transaction->to());
                auto result = m_self->verifyAndSubmitTransaction(
                    std::move(m_transaction), nullptr, true, true);

                if (result != TransactionStatus::None)
                {
                    TXPOOL_LOG(DEBUG) << "Submit transaction failed! "
                                      << LOG_KV("TxHash", txHash.hex()) << LOG_KV("result", result);
                    m_submitResult.emplace<Error::Ptr>(
                        BCOS_ERROR_PTR((int32_t)result, bcos::protocol::toString(result)));
                }
                else
                {
                    auto res = std::make_shared<TransactionSubmitResultImpl>();
                    res->setStatus(static_cast<uint32_t>(result));
                    res->setTxHash(txHash);
                    res->setSender(from);
                    res->setTo(std::string(to));
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
    ::ranges::any_view<bcos::h256, ::ranges::category::mask | ::ranges::category::sized> hashes)
{
    auto hashesVector = ::ranges::to<std::vector>(hashes);
    auto values = m_txsTable.batchFind<decltype(m_txsTable)::ReadAccessor>(hashesVector);
    return values |
           ::ranges::views::transform([](auto const& value) -> protocol::Transaction::ConstPtr {
               if (value)
               {
                   return protocol::Transaction::ConstPtr(std::move(*value));
               }
               return {};
           }) |
           ::ranges::to<std::vector>();
}

TransactionStatus MemoryStorage::txpoolStorageCheck(
    const Transaction& transaction, protocol::TxSubmitCallback& txSubmitCallback)
{
    auto txHash = transaction.hash();
    if (TxsMap::ReadAccessor accessor; m_txsTable.find<TxsMap::ReadAccessor>(accessor, txHash))
    {
        if (txSubmitCallback && !accessor.value()->submitCallback())
        {
            accessor.value()->setSubmitCallback(std::move(txSubmitCallback));
            return TransactionStatus::AlreadyInTxPoolAndAccept;
        }

        return TransactionStatus::AlreadyInTxPool;
    }
    return TransactionStatus::None;
}

// Note: the signature of the tx has already been verified
TransactionStatus MemoryStorage::enforceSubmitTransaction(Transaction::Ptr _tx)
{
    auto txHash = _tx->hash();
    // the transaction has already onChain, reject it
    // check ledger tx
    // check web3 tx
    if (auto result = m_config->txValidator()->checkTransaction(*_tx, true);
        result == TransactionStatus::NonceCheckFail)
    {
        TXPOOL_LOG(WARNING) << LOG_DESC("enforce to seal failed for nonce check failed: ")
                            << LOG_KV("importTxHash", txHash)
                            << LOG_KV("importBatchId", _tx->batchId())
                            << LOG_KV("importBatchHash", _tx->batchHash().abridged());
        return TransactionStatus::NonceCheckFail;
    }

    Transaction::ConstPtr tx = nullptr;
    if (TxsMap::ReadAccessor accessor; m_txsTable.find<TxsMap::ReadAccessor>(accessor, txHash))
    {
        tx = accessor.value();
    }
    // tx already in txpool
    if (tx)
    {
        if (!tx->sealed() || tx->batchHash() == HashType())
        {
            if (!tx->sealed())
            {
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

    auto txHasSeal = _tx->sealed();
    _tx->setSealed(true);  // must set seal before insert
    if (auto status = insertWithoutLock(_tx); status != TransactionStatus::None)
    {
        _tx->setSealed(txHasSeal);
        Transaction::Ptr tx;
        {
            TxsMap::ReadAccessor accessor;
            auto has = m_txsTable.find<TxsMap::ReadAccessor>(accessor, _tx->hash());
            assert(has);  // assume must has
            tx = accessor.value();
        }
        TXPOOL_LOG(WARNING) << LOG_DESC("insertWithoutLock failed for already has the tx")
                            << LOG_KV("hash", tx->hash().abridged())
                            << LOG_KV("status", tx->sealed());
        if (!tx->sealed())
        {
            tx->setSealed(true);
        }
    }
    return TransactionStatus::None;
}

TransactionStatus MemoryStorage::verifyAndSubmitTransaction(
    Transaction::Ptr transaction, TxSubmitCallback txSubmitCallback, bool checkPoolLimit, bool lock)
{
    ittapi::Report report(
        ittapi::ITT_DOMAINS::instance().TXPOOL, ittapi::ITT_DOMAINS::instance().SUBMIT_TX);
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
    if (m_tpsStatstartTime == 0)
    {
        m_tpsStatstartTime = utcTime();
    }
    // Note: In order to ensure that transactions can reach all nodes, transactions from P2P are not
    // restricted
    // if (checkPoolLimit && txsSize >= m_config->poolLimit())
    // {
    //     return TransactionStatus::TxPoolIsFull;
    // }

    // verify the transaction
    if (m_config->checkTransactionSignature())
    {
        result = m_config->txValidator()->verify(*transaction);
    }

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
                              << LOG_KV("result", result)
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
    TxsMap::WriteAccessor accessor;
    auto inserted = m_txsTable.insert(accessor, {transaction->hash(), transaction});
    if (!inserted)
    {
        if (transaction->submitCallback() && !accessor.value()->submitCallback())
        {
            accessor.value()->setSubmitCallback(transaction->submitCallback());
            return TransactionStatus::None;
        }
        return TransactionStatus::AlreadyInTxPool;
    }
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
        if (decltype(m_missedTxs)::WriteAccessor accessor; m_missedTxs.find(accessor, tx->hash()))
        {
            m_missedTxs.remove(accessor);
        }
    }
}


Transaction::Ptr MemoryStorage::removeWithoutNotifyUnseal(HashType const& _txHash)
{
    if (decltype(m_txsTable)::WriteAccessor accessor; m_txsTable.find(accessor, _txHash))
    {
        auto tx = std::move(accessor.value());
        m_txsTable.remove(accessor);
        return tx;
    }
    return {};
}

Transaction::Ptr MemoryStorage::remove(HashType const& _txHash)
{
    return removeWithoutNotifyUnseal(_txHash);
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
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        TXPOOL_LOG(TRACE) << LOG_DESC("notifyTxResult")
                          << LOG_KV("txSubmitResult", *txSubmitResult);
    }
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
    TXPOOL_LOG(DEBUG) << LOG_DESC("printPendingTxs for some txs unhandled")
                      << LOG_KV("pendingSize", m_txsTable.size());
    for (auto& accessor : m_txsTable.range<TxsMap::ReadAccessor>())
    {
        auto tx = accessor.value();
        if (!tx)
        {
            continue;
        }
        TXPOOL_LOG(DEBUG) << LOG_DESC("printPendingTxs") << LOG_KV("hash", tx->hash())
                          << LOG_KV("batchId", tx->batchId())
                          << LOG_KV("batchHash", tx->batchHash().abridged())
                          << LOG_KV("sealed", tx->sealed());
    }
    TXPOOL_LOG(DEBUG) << LOG_DESC("printPendingTxs for some txs unhandled finish");
}

void MemoryStorage::batchRemove(BlockNumber batchId, TransactionSubmitResults const& txsResult)
{
    ittapi::Report report(
        ittapi::ITT_DOMAINS::instance().TXPOOL, ittapi::ITT_DOMAINS::instance().BATCH_REMOVE_TXS);

    auto startT = utcTime();
    auto recordT = startT;
    uint64_t lockT = 0;
    m_blockNumberUpdatedTime = recordT;
    size_t succCount = 0;

    auto txHashes =
        ::ranges::views::transform(txsResult,
            [](TransactionSubmitResult::Ptr const& _txResult) { return _txResult->txHash(); }) |
        ::ranges::to<std::vector>;
    auto removedTxs = m_txsTable.batchRemove<decltype(txHashes), true>(txHashes);

    auto results = ::ranges::views::transform(txsResult,
                       [](TransactionSubmitResult::Ptr const& _txResult) {
                           return std::make_pair(Transaction::Ptr{}, std::addressof(_txResult));
                       }) |
                   ::ranges::to<std::vector>;
    for (auto&& [i, txHash, tx] :
        ::ranges::views::zip(::ranges::views::iota(0), txHashes, removedTxs))
    {
        if (!tx)
        {
            continue;
        }

        ++succCount;
        results[i].first = std::move(*tx);
    }

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
    // update the ledger nonce

    auto nonceListPtr = std::make_shared<NonceList>();
    std::unordered_map<std::string, std::set<u256>> web3NonceMap{};
    for (auto&& [tx, txResult] : results)
    {
        if (tx)
        {
            if (tx->type() == TransactionType::Web3Transaction) [[unlikely]]
            {
                auto sender = std::string(tx->sender());
                auto nonce = hex2u(tx->nonce());
                if (auto it = web3NonceMap.find(sender); it != web3NonceMap.end())
                {
                    it->second.insert(nonce);
                }
                else
                {
                    web3NonceMap.insert({sender, {nonce}});
                }
            }
            else
            {
                nonceListPtr->emplace_back(tx->nonce());
            }
        }
        else if (!(*txResult)->nonce().empty())
        {
            nonceListPtr->emplace_back((*txResult)->nonce());
        }
    }
    m_config->txValidator()->ledgerNonceChecker()->batchInsert(batchId, nonceListPtr);
    auto updateLedgerNonceT = utcTime() - startT;

    startT = utcTime();
    task::syncWait(m_config->txValidator()->web3NonceChecker()->updateNonceCache(
        ::ranges::views::all(web3NonceMap)));
    auto updateWeb3NonceT = utcTime() - startT;

    startT = utcTime();
    // update the txpool nonce
    m_config->txPoolNonceChecker()->batchRemove(*nonceListPtr);
    auto updateTxPoolNonceT = utcTime() - startT;

    tbb::parallel_for(tbb::blocked_range(0UL, results.size()), [&](const auto& range) {
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            auto& [tx, txResult] = results[i];
            if (tx)
            {
                notifyTxResult(*tx, std::move(*txResult));
            }
        }
    });

    TXPOOL_LOG(INFO) << METRIC << LOG_DESC("batchRemove txs success")
                     << LOG_KV("expectedSize", txsResult.size()) << LOG_KV("succCount", succCount)
                     << LOG_KV("batchId", batchId) << LOG_KV("timecost", (utcTime() - recordT))
                     << LOG_KV("lockT", lockT) << LOG_KV("removeT", removeT)
                     << LOG_KV("updateLedgerNonceT", updateLedgerNonceT)
                     << LOG_KV("updateWeb3NonceT", updateWeb3NonceT)
                     << LOG_KV("updateTxPoolNonceT", updateTxPoolNonceT);
}

ConstTransactionsPtr MemoryStorage::fetchTxs(HashList& _missedTxs, HashList const& _txs)
{
    ittapi::Report report(
        ittapi::ITT_DOMAINS::instance().TXPOOL, ittapi::ITT_DOMAINS::instance().FETCH_TXS);

    auto fetchedTxs = std::make_shared<ConstTransactions>();
    _missedTxs.clear();

    for (auto const& hash : _txs)
    {
        TxsMap::ReadAccessor accessor;
        auto has = m_txsTable.find<TxsMap::ReadAccessor>(accessor, hash);
        if (!has)
        {
            _missedTxs.emplace_back(hash);
            continue;
        }
        const auto& tx = accessor.value();
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

    for (auto& accessor : m_txsTable.range<TxsMap::ReadAccessor>())
    {
        const auto& tx = accessor.value();
        // Note: When inserting data into tbb::concurrent_unordered_map while traversing, it.second
        // will occasionally be a null pointer.
        if (!tx || tx->synced())
        {
            continue;
        }
        tx->setSynced(true);
        fetchedTxs->emplace_back(tx);
        if (!(fetchedTxs->size() < _txsLimit))
        {
            break;
        }
    }

    return fetchedTxs;
}

bool MemoryStorage::batchFetchTxs(Block::Ptr _txsList, Block::Ptr _sysTxsList, size_t _txsLimit,
    TxsHashSetPtr _avoidTxs, bool _avoidDuplicate)
{
    auto txsSize = m_txsTable.size();
    if (txsSize == 0)
    {
        return false;
    }

    ittapi::Report report(
        ittapi::ITT_DOMAINS::instance().TXPOOL, ittapi::ITT_DOMAINS::instance().BATCH_FETCH_TXS);
    TXPOOL_LOG(INFO) << LOG_DESC("begin batchFetchTxs") << LOG_KV("pendingTxs", txsSize)
                     << LOG_KV("limit", _txsLimit);
    auto blockFactory = m_config->blockFactory();
    auto recordT = utcTime();
    auto startT = utcTime();
    auto lockT = utcTime() - startT;
    startT = utcTime();
    auto currentTime = utcTime();
    size_t traverseCount = 0;
    size_t sealed = 0;

    auto handleTx = [&](const Transaction::Ptr& tx) {
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
            TxsMap::WriteAccessor accessor;
            m_invalidTxs.insert(accessor, {txHash, tx});
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
        auto result = m_config->txValidator()->checkTransaction(*tx, true);
        if (result == TransactionStatus::NonceCheckFail)
        {
            // in case of the same tx notified more than once
            auto transaction = std::const_pointer_cast<Transaction>(tx);
            transaction->takeSubmitCallback();
            // add to m_invalidTxs to be deleted
            TxsMap::WriteAccessor accessor;
            m_invalidTxs.insert(accessor, {txHash, tx});
            return false;
        }
        // blockLimit expired
        if (result == TransactionStatus::BlockLimitCheckFail)
        {
            TxsMap::WriteAccessor accessor;
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
        return true;
    };

    if (_avoidDuplicate)
    {
        for (auto& accessor :
            m_txsTable.rangeByKey<TxsMap::ReadAccessor>(m_knownLatestSealedTxHash))
        {
            const auto& tx = accessor.value();
            handleTx(tx);
            if (!((_txsList->transactionsMetaDataSize() + _sysTxsList->transactionsMetaDataSize()) <
                    _txsLimit))
            {
                break;
            }
        }
    }
    else
    {
        for (auto& accessor : m_txsTable.range<TxsMap::ReadAccessor>())
        {
            const auto& tx = accessor.value();
            handleTx(tx);
            if (!((_txsList->transactionsMetaDataSize() + _sysTxsList->transactionsMetaDataSize()) <
                    _txsLimit))
            {
                break;
            }
        }
    }
    auto invalidTxsSize = m_invalidTxs.size();
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
    return true;
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
        size_t txCnt = 0;
        std::unordered_map<bcos::crypto::HashType, bcos::protocol::Transaction::Ptr> txs2Remove;

        for (auto& accessor : m_invalidTxs.range<decltype(m_invalidTxs)::ReadAccessor>())
        {
            ++txCnt;
            txs2Remove.emplace(accessor.key(), accessor.value());
        }
        m_invalidTxs.clear();

        bcos::protocol::NonceList invalidNonceList;
        for (auto const& [_, tx] : txs2Remove)
        {
            if (tx->type() == TransactionType::BCOSTransaction) [[likely]]
            {
                invalidNonceList.emplace_back(tx->nonce());
            }
        }
        auto web3Txs =
            ::ranges::views::values(txs2Remove) | ::ranges::views::filter([](auto const& _tx) {
                return _tx->type() == TransactionType::Web3Transaction;
            });
        m_config->txPoolNonceChecker()->batchRemove(invalidNonceList);
        task::syncWait(m_config->txValidator()->web3NonceChecker()->batchRemoveMemoryNonce(
            web3Txs | ::ranges::views::transform([](auto const& _tx) { return _tx->sender(); }),
            web3Txs | ::ranges::views::transform([](auto const& _tx) { return _tx->nonce(); })));

        for (const auto& tx2Remove : txs2Remove | ::ranges::views::keys)
        {
            if (decltype(m_txsTable)::WriteAccessor accessor; m_txsTable.find(accessor, tx2Remove))
            {
                m_txsTable.remove(accessor);
            }
            else
            {
                txs2Remove[tx2Remove] = nullptr;
            }
        }

        auto txs2Notify = txs2Remove | ::ranges::views::filter([](auto const& tx2Remove) {
            return tx2Remove.second != nullptr;
        });

        for (const auto& [txHash, tx] : txs2Notify)
        {
            auto nonce = tx->nonce();
            auto txResult = m_config->txResultFactory()->createTxSubmitResult();
            txResult->setTxHash(txHash);
            txResult->setStatus(static_cast<uint32_t>(TransactionStatus::TransactionPoolTimeout));
            txResult->setNonce(std::string(nonce));
            notifyTxResult(*tx, std::move(txResult));
        }

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
}

HashListPtr MemoryStorage::filterUnknownTxs(HashList const& _txsHashList, NodeIDPtr _peer)
{
    auto values = m_txsTable.batchFind<TxsMap::ReadAccessor>(_txsHashList);
    HashList missList = ::ranges::views::filter(values, [](const auto& transaction) {
        return transaction.has_value();
    }) | ::ranges::views::transform([](const auto& transaction) {
        return (*transaction)->hash();
    }) | ::ranges::to<std::vector>();

    auto unknownTxsList = std::make_shared<HashList>();
    auto results = m_missedTxs.batchInsert<true>(missList);
    for (auto [index, result] : ::ranges::views::enumerate(results))
    {
        if (result != 0)
        {
            unknownTxsList->push_back(missList[index]);
        }
    }

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
    std::atomic_size_t successCount = 0;
    std::atomic_size_t notFound = 0;
    std::atomic_size_t reSealed = 0;
    std::atomic_int64_t knownLatestSealedTxIndex = -1;

    m_txsTable.traverse<TxsMap::ReadAccessor, true>(
        _txsHashList, [&](TxsMap::ReadAccessor& accessor, const auto& range, auto& bucket) {
            size_t localNotFound = 0;
            size_t localReSealed = 0;
            size_t localSuccess = 0;
            int64_t localKnownLatestSealedTxIndex = -1;
            for (auto index : range)
            {
                if (!bucket.find(accessor, _txsHashList[index]))
                {
                    ++localNotFound;
                    continue;
                }

                const auto& transaction = accessor.value();
                if ((transaction->batchId() != _batchId ||
                        transaction->batchHash() != _batchHash) &&
                    transaction->sealed() && !_sealFlag)
                {
                    ++localReSealed;
                    continue;
                }
                transaction->setSealed(_sealFlag);
                ++localSuccess;
                // set the block information for the transaction
                if (_sealFlag)
                {
                    transaction->setBatchId(_batchId);
                    transaction->setBatchHash(_batchHash);
                    if (static_cast<int64_t>(index) > localKnownLatestSealedTxIndex)
                    {
                        localKnownLatestSealedTxIndex = index;
                    }
                }
            }

            successCount += localSuccess;
            notFound += localNotFound;
            reSealed += localReSealed;
            if (localKnownLatestSealedTxIndex > 0)
            {
                auto current = knownLatestSealedTxIndex.load();
                while (localKnownLatestSealedTxIndex > current &&
                       !knownLatestSealedTxIndex.compare_exchange_strong(
                           current, localKnownLatestSealedTxIndex))
                {
                }
            }
        });
    if (knownLatestSealedTxIndex > 0)
    {
        m_knownLatestSealedTxHash = _txsHashList[knownLatestSealedTxIndex];
    }

    TXPOOL_LOG(INFO) << LOG_DESC("batchMarkTxs") << LOG_KV("txsSize", _txsHashList.size())
                     << LOG_KV("batchId", _batchId) << LOG_KV("hash", _batchHash.abridged())
                     << LOG_KV("sealFlag", _sealFlag) << LOG_KV("notFound", notFound)
                     << LOG_KV("reSealed", reSealed) << LOG_KV("succ", successCount)
                     << LOG_KV("time", utcTime() - recordT)
                     << LOG_KV("markT", (utcTime() - startT));
    return notFound == 0;  // return true if all txs have been marked
}

void MemoryStorage::batchMarkAllTxs(bool _sealFlag)
{
    for (auto& accessor : m_txsTable.range<TxsMap::ReadAccessor>())
    {
        auto tx = accessor.value();
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

    auto txHashes = ::ranges::iota_view<size_t, size_t>{0, txsSize} |
                    ::ranges::views::transform(
                        [&_block](size_t index) { return _block->transactionHash(index); }) |
                    ::ranges::to<std::vector>();
    bool findErrorTxInBlock = false;

    auto values = m_txsTable.batchFind<TxsMap::ReadAccessor>(txHashes);
    for (auto&& [index, value] : ::ranges::views::enumerate(values))
    {
        if (!value)
        {
            missedTxs->emplace_back(txHashes[index]);
        }
        else if ((*value)->sealed())
        {
            auto header = _block->blockHeader();
            if ((*value)->batchId() != header->number() && (*value)->batchId() != -1)
            {
                TXPOOL_LOG(INFO) << LOG_DESC("batchVerifyProposal unexpected wrong tx")
                                 << LOG_KV("blkNum", header->number())
                                 << LOG_KV("blkHash", header->hash().abridged())
                                 << LOG_KV("txHash", (*value)->hash().hexPrefixed())
                                 << LOG_KV("txBatchId", (*value)->batchId())
                                 << LOG_KV("txBatchHash", (*value)->batchHash().abridged());
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
                break;
            }
        }
    }

    TXPOOL_LOG(INFO) << LOG_DESC("batchVerifyProposal") << LOG_KV("consNum", batchId)
                     << LOG_KV("hash", batchHash.abridged()) << LOG_KV("txsSize", txsSize)
                     << LOG_KV("lockT", lockT) << LOG_KV("verifyT", (utcTime() - startT))
                     << LOG_KV("missedTxs", missedTxs->size());
    return findErrorTxInBlock ? nullptr : missedTxs;
}

bool MemoryStorage::batchVerifyProposal(std::shared_ptr<HashList> _txsHashList)
{
    bool has = false;
    auto values = m_txsTable.batchFind<TxsMap::ReadAccessor>(*_txsHashList);
    return ::ranges::all_of(values, [](const auto& value) { return value.has_value(); });
}

HashListPtr MemoryStorage::getTxsHash(int _limit)
{
    auto txsHash = std::make_shared<HashList>();

    for (auto& accessor : m_txsTable.range<TxsMap::ReadAccessor>())
    {
        auto tx = accessor.value();
        if (!tx)
        {
            continue;
        }
        // check txpool txs, no need to check txpool nonce
        auto result = m_config->txValidator()->checkTransaction(*tx, true);
        if (result != TransactionStatus::None)
        {
            TxsMap::WriteAccessor writeAccessor;
            m_invalidTxs.insert(writeAccessor, {tx->hash(), tx});
            continue;
        }
        if ((int)txsHash->size() >= _limit)
        {
            break;
        }
        txsHash->emplace_back(accessor.key());
    };
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

    for (auto& accessor : m_txsTable.range<TxsMap::ReadAccessor>())
    {
        traversedTxsNum++;
        if (traversedTxsNum > MAX_TRAVERSE_TXS_COUNT)
        {
            break;
        }

        auto tx = accessor.value();
        if (tx->sealed() &&
            (tx->batchId() >= m_blockNumber || tx->batchId() == -1))  // -1 means seal by my self

        {
            sealedTxs++;
            continue;
        }

        // the txs expired or not
        if (currentTime > (tx->importTime() + m_txsExpirationTime))
        {
            TxsMap::WriteAccessor accessor1;
            if (m_invalidTxs.insert(accessor1, {tx->hash(), tx}))
            {
                erasedTxs++;
            }
            else
            {
                // already exist
                continue;
            }
        }
        else
        {
            if (m_invalidTxs.contains(tx->hash()))
            {
                // already exist
                continue;
            }
        }
        // check txpool txs, no need to check txpool nonce
        auto validator = m_config->txValidator();
        auto result = validator->checkTransaction(*tx, true);
        // blockLimit expired
        if (result != TransactionStatus::None)
        {
            TxsMap::WriteAccessor writeAccessor;
            if (m_invalidTxs.insert(writeAccessor, {tx->hash(), tx}))
            {
                erasedTxs++;
            }
            else
            {  // already exist
                continue;
            }
        }

        if (traversedTxsNum > MAX_TRAVERSE_TXS_COUNT)
        {
            break;
        }
    }
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
        if (auto result = enforceSubmitTransaction(tx); result != TransactionStatus::None)
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
    TXPOOL_LOG(DEBUG) << LOG_DESC("batchVerifyAndSubmitTransaction success")
                      << LOG_KV("totalTxs", _txs->size()) << LOG_KV("lockT", lockT)
                      << LOG_KV("submitT", (utcTime() - recordT));
    return true;
}

void MemoryStorage::notifyTxsSize(size_t _retryTime)
{
    m_txsSizeNotifierTimer->restart();
    // Note: must set the notifier
    if (!m_txsNotifier)
    {
        return;
    }
    auto txsSize = size();
    auto self = weak_from_this();
    m_txsNotifier(txsSize, [_retryTime, self](Error::Ptr _error) {
        if (_error == nullptr)
        {
            return;
        }
        TXPOOL_LOG(WARNING) << LOG_DESC("notifyTxsSize failed")
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
        memoryStorage->notifyTxsSize((_retryTime + 1));
    });
}