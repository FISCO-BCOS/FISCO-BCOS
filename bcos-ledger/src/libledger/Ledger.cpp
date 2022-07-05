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
 * @file Ledger.cpp
 * @author: kyonRay
 * @date 2021-04-13
 * @file Ledger.cpp
 * @author: ancelmo
 * @date 2021-09-06
 */

#include "Ledger.h"
#include "bcos-tool/VersionConverter.h"
#include "utilities/Common.h"
#include <bcos-codec/scale/Scale.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/interfaces/consensus/ConsensusNode.h>
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
#include <bcos-framework/interfaces/ledger/LedgerTypeDef.h>
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/interfaces/protocol/GlobalConfig.h>
#include <bcos-framework/interfaces/protocol/ProtocolTypeDef.h>
#include <bcos-framework/interfaces/storage/Table.h>
#include <bcos-protocol/ParallelMerkleProof.h>
#include <bcos-tool/ConsensusNode.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <tbb/parallel_for.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <future>
#include <memory>
#include <utility>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::crypto;


void Ledger::asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
    bcos::protocol::TransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
    std::function<void(Error::Ptr&&)> callback)
{
    if (!block)
    {
        callback(
            BCOS_ERROR_PTR(LedgerError::ErrorArgument, "asyncPrewriteBlock failed, empty block"));
        return;
    }

    if (isSysContractDeploy(block->blockHeaderConst()->number()) && block->transactionsSize() > 0)
    {
        // sys contract deploy
        /// NOTE: write block number for 2pc storage
        Entry numberEntry;
        numberEntry.importFields({"0"});
        storage->asyncSetRow(SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER, std::move(numberEntry),
            [callback](auto&& error) {
                LEDGER_LOG(ERROR) << "System contract write ledger storage error!";
                callback(std::forward<decltype(error)>(error));
            });
        return;
    }
    auto header = block->blockHeaderConst();

    auto blockNumberStr = boost::lexical_cast<std::string>(header->number());

    // 8 storage callbacks and write hash=>receipt and
    size_t TOTAL_CALLBACK = 8 + block->receiptsSize();
    auto txsToSaveResult = needStoreUnsavedTxs(_blockTxs, block);
    bool shouldStoreTxs = std::get<0>(txsToSaveResult);
    if (shouldStoreTxs)
    {
        TOTAL_CALLBACK = 9 + block->receiptsSize();
    }
    auto setRowCallback = [total = std::make_shared<std::atomic<size_t>>(TOTAL_CALLBACK),
                              failed = std::make_shared<bool>(false),
                              callback = std::move(callback)](
                              Error::UniquePtr&& error, size_t count = 1) {
        *total -= count;
        if (error)
        {
            LEDGER_LOG(ERROR) << "Prewrite block error!" << boost::diagnostic_information(*error);
            *failed = true;
        }

        if (*total == 0)
        {
            // all finished
            if (*failed)
            {
                LEDGER_LOG(ERROR) << "PrewriteBlock error";
                callback(
                    BCOS_ERROR_PTR(LedgerError::CollectAsyncCallbackError, "PrewriteBlock error"));
                return;
            }

            LEDGER_LOG(INFO) << "PrewriteBlock success";
            callback(nullptr);
        }
    };

    // number 2 entry
    Entry numberEntry;
    numberEntry.importFields({blockNumberStr});
    storage->asyncSetRow(SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER, std::move(numberEntry),
        [setRowCallback](auto&& error) { setRowCallback(std::move(error)); });

    // number 2 hash
    Entry hashEntry;
    hashEntry.importFields({header->hash().hex()});
    storage->asyncSetRow(SYS_NUMBER_2_HASH, blockNumberStr, std::move(hashEntry),
        [setRowCallback](auto&& error) { setRowCallback(std::move(error)); });

    // hash 2 number
    Entry hash2NumberEntry;
    hash2NumberEntry.importFields({blockNumberStr});
    storage->asyncSetRow(SYS_HASH_2_NUMBER, header->hash().hex(), std::move(hash2NumberEntry),
        [setRowCallback](auto&& error) { setRowCallback(std::move(error)); });

    // number 2 header
    bytes headerBuffer;
    header->encode(headerBuffer);

    Entry number2HeaderEntry;
    number2HeaderEntry.importFields({std::move(headerBuffer)});
    storage->asyncSetRow(SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr, std::move(number2HeaderEntry),
        [setRowCallback](auto&& error) { setRowCallback(std::move(error)); });

    // number 2 nonce
    auto nonceBlock = m_blockFactory->createBlock();
    nonceBlock->setNonceList(block->nonceList());
    bytes nonceBuffer;
    nonceBlock->encode(nonceBuffer);

    Entry number2NonceEntry;
    number2NonceEntry.importFields({std::move(nonceBuffer)});
    storage->asyncSetRow(SYS_BLOCK_NUMBER_2_NONCES, blockNumberStr, std::move(number2NonceEntry),
        [setRowCallback](auto&& error) { setRowCallback(std::move(error)); });

    // number 2 transactions
    auto transactionsBlock = m_blockFactory->createBlock();
    if (block->transactionsMetaDataSize() > 0)
    {
        for (size_t i = 0; i < block->transactionsMetaDataSize(); ++i)
        {
            auto originTransactionMetaData = block->transactionMetaData(i);
            auto transactionMetaData = m_blockFactory->createTransactionMetaData(
                originTransactionMetaData->hash(), std::string(originTransactionMetaData->to()));
            transactionsBlock->appendTransactionMetaData(std::move(transactionMetaData));
        }
    }
    else if (block->transactionsSize() > 0)
    {
        for (size_t i = 0; i < block->transactionsSize(); ++i)
        {
            auto transaction = block->transaction(i);
            auto transactionMetaData = m_blockFactory->createTransactionMetaData(
                transaction->hash(), std::string(transaction->to()));
            transactionsBlock->appendTransactionMetaData(std::move(transactionMetaData));
        }
    }
    else
    {
        LEDGER_LOG(WARNING) << "Empty transactions and metadata, empty block?";
    }
    bytes transactionsBuffer;
    transactionsBlock->encode(transactionsBuffer);

    Entry number2TransactionHashesEntry;
    number2TransactionHashesEntry.importFields({std::move(transactionsBuffer)});
    storage->asyncSetRow(SYS_NUMBER_2_TXS, blockNumberStr, std::move(number2TransactionHashesEntry),
        [setRowCallback](auto&& error) { setRowCallback(std::move(error)); });

    // hash 2 receipts
    std::atomic_int64_t totalCount = 0;
    std::atomic_int64_t failedCount = 0;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, block->receiptsSize()),
        [&storage, &transactionsBlock, &block, &failedCount, &totalCount, &setRowCallback](
            const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i < range.end(); ++i)
            {
                auto hash = transactionsBlock->transactionHash(i);

                auto receipt = block->receipt(i);
                if (receipt->status() != 0)
                {
                    failedCount++;
                }
                totalCount++;

                bytes receiptBuffer;
                receipt->encode(receiptBuffer);

                Entry receiptEntry;
                receiptEntry.importFields({std::move(receiptBuffer)});
                storage->asyncSetRow(SYS_HASH_2_RECEIPT, hash.hex(), std::move(receiptEntry),
                    [setRowCallback](auto&& error) { setRowCallback(std::move(error)); });
            }
        });

    LEDGER_LOG(DEBUG) << LOG_DESC("Calculate tx counts in block")
                      << LOG_KV("number", blockNumberStr) << LOG_KV("totalCount", totalCount)
                      << LOG_KV("failedCount", failedCount);

    // total transaction count
    asyncGetTotalTransactionCount(
        [storage, block, &setRowCallback, &totalCount, &failedCount](
            Error::Ptr error, int64_t total, int64_t failed, bcos::protocol::BlockNumber) {
            if (error)
            {
                LEDGER_LOG(INFO) << "No total transaction count entry, add new one";
                setRowCallback(std::make_unique<Error>(*error), 2);
                return;
            }
            auto totalTxsCount = total + totalCount;
            Entry totalEntry;
            totalEntry.importFields({boost::lexical_cast<std::string>(totalTxsCount)});
            storage->asyncSetRow(SYS_CURRENT_STATE, SYS_KEY_TOTAL_TRANSACTION_COUNT,
                std::move(totalEntry),
                [setRowCallback](auto&& error) { setRowCallback(std::move(error)); });
            auto failedTxs = failed + failedCount;
            if (failedCount != 0)
            {
                Entry failedEntry;
                failedEntry.importFields({boost::lexical_cast<std::string>(failedTxs)});
                storage->asyncSetRow(SYS_CURRENT_STATE, SYS_KEY_TOTAL_FAILED_TRANSACTION,
                    std::move(failedEntry),
                    [setRowCallback](auto&& error) { setRowCallback(std::move(error)); });
            }
            else
            {
                setRowCallback({}, true);
            }
            LEDGER_LOG(INFO) << METRIC << LOG_DESC("asyncPrewriteBlock")
                             << LOG_KV("number", block->blockHeaderConst()->number())
                             << LOG_KV("totalTxs", totalTxsCount) << LOG_KV("failedTxs", failedTxs)
                             << LOG_KV("incTxs", totalCount) << LOG_KV("incFailedTxs", failedCount);
        });

    if (!shouldStoreTxs)
    {
        return;
    }
    auto startT = utcTime();
    auto blockTxsSize = _blockTxs->size();
    auto unstoredTxsHash = std::get<1>(txsToSaveResult);
    asyncStoreTransactions(std::get<2>(txsToSaveResult), unstoredTxsHash,
        [startT, setRowCallback, unstoredTxsHash, blockTxsSize, blockNumberStr](Error::Ptr _error) {
            LEDGER_LOG(INFO) << LOG_DESC("asyncPrewriteBlock: supplement unstored txs")
                             << LOG_KV("number", blockNumberStr)
                             << LOG_KV("blockTxsSize", blockTxsSize)
                             << LOG_KV("unstoredTxs", unstoredTxsHash->size())
                             << LOG_KV("msg", _error ? _error->errorMessage() : "success")
                             << LOG_KV("code", _error ? _error->errorCode() : 0)
                             << LOG_KV("timecost", (utcTime() - startT));
            // TODO: retry when asyncStoreTransactions failed
            if (!_error)
            {
                setRowCallback(nullptr);
                return;
            }
            setRowCallback(std::make_unique<Error>(*_error));
        });
}

std::tuple<bool, bcos::crypto::HashListPtr, std::shared_ptr<std::vector<bytesConstPtr>>>
Ledger::needStoreUnsavedTxs(
    bcos::protocol::TransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr _block)
{
    // Note: in the case of block-sync, no-need to save transactions when prewriteBlock
    if (!_blockTxs || _blockTxs->size() == 0)
    {
        LEDGER_LOG(INFO) << LOG_DESC("needStoreUnsavedTxs: empty txs")
                         << LOG_KV("number", _block->blockHeaderConst()->number());
        return std::make_tuple(false, nullptr, nullptr);
    }
    // supplement the unsaved hash_2_txs
    auto txsToStore = std::make_shared<std::vector<bytesConstPtr>>();
    size_t unstoredTxs = 0;
    auto txsHash = std::make_shared<HashList>();
    for (auto const& tx : (*_blockTxs))
    {
        if (tx->storeToBackend())
        {
            continue;
        }
        auto encodedData = tx->encode();
        // in case of the tx been re-committed
        tx->setStoreToBackend(true);
        unstoredTxs++;
        txsHash->emplace_back(tx->hash());
        txsToStore->emplace_back(std::make_shared<bytes>(encodedData.begin(), encodedData.end()));
    }
    LEDGER_LOG(INFO) << LOG_DESC("needStoreUnsavedTxs: supplement unstored txs")
                     << LOG_KV("txsSize", _blockTxs->size()) << LOG_KV("unstoredTxs", unstoredTxs)
                     << LOG_KV("number", _block->blockHeaderConst()->number());
    if (txsToStore->size() == 0)
    {
        return std::make_tuple(false, nullptr, nullptr);
    }
    return std::make_tuple(true, txsHash, txsToStore);
}

void Ledger::asyncStoreTransactions(std::shared_ptr<std::vector<bytesConstPtr>> _txToStore,
    crypto::HashListPtr _txHashList, std::function<void(Error::Ptr)> _onTxStored)
{
    if (!_txToStore || !_txHashList || _txToStore->size() != _txHashList->size())
    {
        LEDGER_LOG(ERROR) << "StoreTransactions error";
        _onTxStored(
            BCOS_ERROR_PTR(LedgerError::ErrorArgument, "asyncStoreTransactions argument error!"));
        return;
    }

    LEDGER_LOG(TRACE) << "StoreTransactions request" << LOG_KV("tx count", _txToStore->size())
                      << LOG_KV("hash count", _txHashList->size());

#if 0
    m_storage->asyncOpenTable(SYS_HASH_2_TX,
        [this, storage = m_storage, hashList = std::move(_txHashList),
            txList = std::move(_txToStore),
            callback = std::move(_onTxStored)](auto&& error, std::optional<Table>&& table) {
            auto validError = checkTableValid(std::move(error), table, SYS_HASH_2_TX);
            if (validError)
            {
                LEDGER_LOG(ERROR) << "StoreTransactions error"
                                  << boost::diagnostic_information(validError);
                callback(std::move(validError));
                return;
            }

            auto total = txList->size();
            auto count =
                std::make_shared<std::tuple<std::atomic<size_t>, std::atomic<size_t>>>(0, 0);
            Error::Ptr storeError = nullptr;
            for (size_t i = 0; i < txList->size(); ++i)
            {
                auto entry = table->newEntry();
                entry.setField(0, *((*txList)[i]));  // copy the bytes entry

                LEDGER_LOG(TRACE) << "Write transaction" << LOG_KV("hash", (*hashList)[i].hex());
                table->asyncSetRow((*hashList)[i].hex(), std::move(entry),
                    [total, &storeError, count, callback](auto&& error) {
                        if (error)
                        {
                            ++std::get<1>(*count);
                            storeError = std::make_shared<Error>(*error);
                            LEDGER_LOG(ERROR) << "Set row failed!" << error->what();
                        }
                        else
                        {
                            ++std::get<0>(*count);
                        }

                        if (std::get<0>(*count) + std::get<1>(*count) == total)
                        {
                            // All finished
                            // if contains failed store
                            if (std::get<1>(*count) > 0)
                            {
                                LEDGER_LOG(ERROR) << LOG_DESC("asyncStoreTransactions with error")
                                                  << LOG_KV("e", storeError->what());
                                callback(storeError);
                                return;
                            }
                            LEDGER_LOG(TRACE) << "StoreTransactions success";
                            callback(nullptr);
                        }
                    });
            }
        });
#endif
    auto total = _txToStore->size();
    std::vector<std::string> keys(total);
    std::vector<std::string> values(total);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, _txHashList->size()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i < range.end(); ++i)
            {
                keys[i] = _txHashList->at(i).hex();
                values[i] =
                    std::string((char*)(_txToStore->at(i)->data()), _txToStore->at(i)->size());
            }
        });
    auto error = m_storage->setRows(SYS_HASH_2_TX, std::move(keys), std::move(values));
    _onTxStored(error);
}

void Ledger::asyncGetBlockDataByNumber(bcos::protocol::BlockNumber _blockNumber, int32_t _blockFlag,
    std::function<void(Error::Ptr, bcos::protocol::Block::Ptr)> _onGetBlock)
{
    LEDGER_LOG(INFO) << "GetBlockDataByNumber request" << LOG_KV("blockNumber", _blockNumber)
                     << LOG_KV("blockFlag", _blockFlag);
    if (_blockNumber < 0 || _blockFlag < 0)
    {
        LEDGER_LOG(INFO) << "GetBlockDataByNumber error, wrong argument";
        _onGetBlock(BCOS_ERROR_PTR(LedgerError::ErrorArgument, "Wrong argument"), nullptr);
        return;
    }

    std::list<std::function<void()>> fetchers;
    auto block = m_blockFactory->createBlock();
    auto total = std::make_shared<size_t>(0);
    auto result = std::make_shared<std::tuple<std::atomic<size_t>, std::atomic<size_t>>>(0, 0);

    auto finally = [_blockNumber, total, result, block, _onGetBlock](Error::Ptr&& error) {
        if (error)
            ++std::get<1>(*result);
        else
            ++std::get<0>(*result);

        if (std::get<0>(*result) + std::get<1>(*result) == *total)
        {
            // All finished
            if (std::get<0>(*result) != *total)
            {
                LEDGER_LOG(ERROR) << "GetBlockDataByNumber request error, with errors!"
                                  << LOG_KV("number", _blockNumber);
                _onGetBlock(BCOS_ERROR_PTR(LedgerError::CollectAsyncCallbackError,
                                "Get block failed with errors!"),
                    nullptr);
                return;
            }

            LEDGER_LOG(INFO) << "GetBlockDataByNumber success" << LOG_KV("number", _blockNumber);
            _onGetBlock(nullptr, std::move(block));
        }
    };

    if (_blockFlag & HEADER)
    {
        ++(*total);

        fetchers.push_back([this, _blockNumber, block, finally]() {
            asyncGetBlockHeader(
                block, _blockNumber, [finally](Error::Ptr&& error) { finally(std::move(error)); });
        });
    }
    if (_blockFlag & TRANSACTIONS)
        ++(*total);
    if (_blockFlag & RECEIPTS)
        ++(*total);

    if ((_blockFlag & TRANSACTIONS) || (_blockFlag & RECEIPTS))
    {
        fetchers.push_back([this, block, _blockNumber, finally, _blockFlag]() {
            asyncGetBlockTransactionHashes(_blockNumber, [this, _blockFlag, block, finally](
                                                             Error::Ptr&& error,
                                                             std::vector<std::string>&& hashes) {
                if (error)
                {
                    if (_blockFlag & TRANSACTIONS)
                        finally(std::move(error));
                    if (_blockFlag & RECEIPTS)
                        finally(std::move(error));
                    return;
                }

                LEDGER_LOG(TRACE) << "Get transactions hash list success, size:" << hashes.size();

                auto hashesPtr = std::make_shared<std::vector<std::string>>(std::move(hashes));
                if (_blockFlag & TRANSACTIONS)
                {
                    asyncBatchGetTransactions(
                        hashesPtr, [block, finally](Error::Ptr&& error,
                                       std::vector<protocol::Transaction::Ptr>&& transactions) {
                            if (error)
                            {
                                LEDGER_LOG(ERROR) << LOG_DESC("asyncGetBlockDataByNumber error")
                                                  << LOG_KV("code", error->errorCode())
                                                  << LOG_KV("msg", error->errorMessage());
                            }
                            for (auto& it : transactions)
                            {
                                block->appendTransaction(it);
                            }
                            finally(std::move(error));
                        });
                }
                if (_blockFlag & RECEIPTS)
                {
                    asyncBatchGetReceipts(
                        hashesPtr, [block, finally](Error::Ptr&& error,
                                       std::vector<protocol::TransactionReceipt::Ptr>&& receipts) {
                            for (auto& it : receipts)
                            {
                                block->appendReceipt(it);
                            }
                            finally(std::move(error));
                        });
                }
            });
        });
    }

    for (auto& it : fetchers)
    {
        it();
    }
}

void Ledger::asyncGetBlockNumber(
    std::function<void(Error::Ptr, bcos::protocol::BlockNumber)> _onGetBlock)
{
    LEDGER_LOG(INFO) << "GetBlockNumber request";
    asyncGetSystemTableEntry(SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER,
        [callback = std::move(_onGetBlock)](
            Error::Ptr&& error, std::optional<bcos::storage::Entry>&& entry) {
            if (error)
            {
                LEDGER_LOG(ERROR) << "GetBlockNumber error" << boost::diagnostic_information(error);
                callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                             "Get block number storage error", *error),
                    -1);
                return;
            }

            bcos::protocol::BlockNumber blockNumber = -1;
            try
            {
                blockNumber = boost::lexical_cast<bcos::protocol::BlockNumber>(entry->getField(0));
            }
            catch (boost::bad_lexical_cast& e)
            {
                // Ignore the exception
                LEDGER_LOG(INFO) << "Cast blocknumber failed, may be empty, set to default value -1"
                                 << LOG_KV("blocknumber str", entry->getField(0));
            }

            LEDGER_LOG(INFO) << "GetBlockNumber success" << LOG_KV("number", blockNumber);
            callback(nullptr, blockNumber);
        });
}

void Ledger::asyncGetBlockHashByNumber(bcos::protocol::BlockNumber _blockNumber,
    std::function<void(Error::Ptr, bcos::crypto::HashType)> _onGetBlock)
{
    LEDGER_LOG(INFO) << "GetBlockHashByNumber request" << LOG_KV("blockNumber", _blockNumber);
    if (_blockNumber < 0)
    {
        _onGetBlock(BCOS_ERROR_PTR(
                        LedgerError::ErrorArgument, "GetBlockHashByNumber error, error argument"),
            bcos::crypto::HashType());
        return;
    }

    auto key = boost::lexical_cast<std::string>(_blockNumber);
    asyncGetSystemTableEntry(SYS_NUMBER_2_HASH, key,
        [callback = std::move(_onGetBlock)](
            Error::Ptr&& error, std::optional<bcos::storage::Entry>&& entry) {
            try
            {
                if (error)
                {
                    LEDGER_LOG(ERROR)
                        << "GetBlockHashByNumber error" << boost::diagnostic_information(error);
                    callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                 "GetBlockHashByNumber error", *error),
                        bcos::crypto::HashType());
                    return;
                }

                auto hashStr = entry->getField(0);
                bcos::crypto::HashType hash(std::string(hashStr), bcos::crypto::HashType::FromHex);

                LEDGER_LOG(INFO) << "GetBlockHashByNumber success" << LOG_KV("hash", hashStr);
                callback(nullptr, std::move(hash));
            }
            catch (std::exception& e)
            {
                callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::UnknownError, "Unknown error", e),
                    bcos::crypto::HashType());
                return;
            }
        });
}

void Ledger::asyncGetBlockNumberByHash(const crypto::HashType& _blockHash,
    std::function<void(Error::Ptr, bcos::protocol::BlockNumber)> _onGetBlock)
{
    auto key = _blockHash.hex();
    LEDGER_LOG(INFO) << "GetBlockNumberByHash request" << LOG_KV("hash", key);

    asyncGetSystemTableEntry(SYS_HASH_2_NUMBER, key,
        [callback = std::move(_onGetBlock)](
            Error::Ptr&& error, std::optional<bcos::storage::Entry>&& entry) {
            try
            {
                if (error)
                {
                    LEDGER_LOG(ERROR)
                        << "GetBlockNumberByHash error " << boost::diagnostic_information(*error);
                    callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                 "GetBlockNumberByHash error ", *error),
                        -1);
                    return;
                }

                bcos::protocol::BlockNumber blockNumber = -1;
                try
                {
                    blockNumber =
                        boost::lexical_cast<bcos::protocol::BlockNumber>(entry->getField(0));
                }
                catch (boost::bad_lexical_cast& e)
                {
                    // Ignore the exception
                    LEDGER_LOG(INFO)
                        << "Cast blocknumber failed, may be empty, set to default value -1"
                        << LOG_KV("blocknumber str", entry->getField(0));
                }
                LEDGER_LOG(INFO) << "GetBlockNumberByHash success" << LOG_KV("number", blockNumber);
                callback(nullptr, blockNumber);
            }
            catch (std::exception& e)
            {
                LEDGER_LOG(INFO) << "GetBlockNumberByHash error"
                                 << boost::diagnostic_information(e);
                callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                             "GetBlockNumberByHash error", std::move(e)),
                    -1);
            }
        });
}

void Ledger::asyncGetBatchTxsByHashList(crypto::HashListPtr _txHashList, bool _withProof,
    std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr,
        std::shared_ptr<std::map<std::string, MerkleProofPtr>>)>
        _onGetTx)
{
    if (!_txHashList)
    {
        LEDGER_LOG(ERROR) << "GetBatchTxsByHashList error, wrong argument";
        _onGetTx(BCOS_ERROR_PTR(LedgerError::ErrorArgument, "Wrong argument"), nullptr, nullptr);
        return;
    }

    LEDGER_LOG(TRACE) << "GetBatchTxsByHashList request" << LOG_KV("hashes", _txHashList->size())
                      << LOG_KV("withProof", _withProof);

    auto hexList = std::make_shared<std::vector<std::string>>();
    hexList->reserve(_txHashList->size());

    for (auto& it : *_txHashList)
    {
        hexList->push_back(it.hex());
    }

    asyncBatchGetTransactions(
        hexList, [this, callback = std::move(_onGetTx), _txHashList, _withProof](
                     Error::Ptr&& error, std::vector<protocol::Transaction::Ptr>&& transactions) {
            if (error)
            {
                LEDGER_LOG(TRACE) << "GetBatchTxsByHashList error: "
                                  << boost::diagnostic_information(error);
                callback(BCOS_ERROR_WITH_PREV_PTR(
                             LedgerError::GetStorageError, "GetBatchTxsByHashList error", *error),
                    nullptr, nullptr);
                return;
            }

            bcos::protocol::TransactionsPtr results =
                std::make_shared<bcos::protocol::Transactions>(std::move(transactions));

            if (_withProof)
            {
                auto con_proofMap =
                    std::make_shared<tbb::concurrent_unordered_map<std::string, MerkleProofPtr>>();
                auto count = std::make_shared<std::atomic_uint64_t>(0);
                auto counter = [_txList = results, _txHashList, count, con_proofMap,
                                   callback = callback]() {
                    count->fetch_add(1);
                    if (count->load() == _txHashList->size())
                    {
                        auto proofMap = std::make_shared<std::map<std::string, MerkleProofPtr>>(
                            con_proofMap->begin(), con_proofMap->end());
                        LEDGER_LOG(INFO) << LOG_BADGE("GetBatchTxsByHashList success")
                                         << LOG_KV("txHashListSize", _txHashList->size())
                                         << LOG_KV("proofMapSize", proofMap->size());
                        callback(nullptr, _txList, proofMap);
                    }
                };

                tbb::parallel_for(tbb::blocked_range<size_t>(0, _txHashList->size()),
                    [this, _txHashList, counter, con_proofMap](
                        const tbb::blocked_range<size_t>& range) {
                        for (size_t i = range.begin(); i < range.end(); ++i)
                        {
                            auto txHash = _txHashList->at(i);
                            getTxProof(txHash, [con_proofMap, txHash, counter](
                                                   Error::Ptr _error, MerkleProofPtr _proof) {
                                if (!_error && _proof)
                                {
                                    con_proofMap->insert(std::make_pair(txHash.hex(), _proof));
                                }
                                counter();
                            });
                        }
                    });
            }
            else
            {
                LEDGER_LOG(TRACE) << LOG_BADGE("GetBatchTxsByHashList success")
                                  << LOG_KV("txHashListSize", _txHashList->size())
                                  << LOG_KV("withProof", _withProof);
                callback(nullptr, results, nullptr);
            }
        });
}

void Ledger::asyncGetTransactionReceiptByHash(bcos::crypto::HashType const& _txHash,
    bool _withProof,
    std::function<void(Error::Ptr, bcos::protocol::TransactionReceipt::ConstPtr, MerkleProofPtr)>
        _onGetTx)
{
    auto key = _txHash.hex();

    LEDGER_LOG(TRACE) << "GetTransactionReceiptByHash" << LOG_KV("hash", key);

    asyncGetSystemTableEntry(SYS_HASH_2_RECEIPT, key,
        [this, callback = std::move(_onGetTx), key, _withProof](
            Error::Ptr&& error, std::optional<bcos::storage::Entry>&& entry) {
            if (error)
            {
                LEDGER_LOG(ERROR) << "GetTransactionReceiptByHash error"
                                  << boost::diagnostic_information(error);
                callback(BCOS_ERROR_WITH_PREV_PTR(
                             LedgerError::GetStorageError, "GetTransactionReceiptByHash", *error),
                    nullptr, nullptr);

                return;
            }

            auto value = entry->getField(0);
            auto receipt = m_blockFactory->receiptFactory()->createReceipt(
                bcos::bytesConstRef((bcos::byte*)value.data(), value.size()));

            if (_withProof)
            {
                getReceiptProof(receipt,
                    [receipt, _onGetTx = callback](Error::Ptr _error, MerkleProofPtr _proof) {
                        if (_error)
                        {
                            LEDGER_LOG(ERROR) << "GetTransactionReceiptByHash error"
                                              << LOG_KV("errorCode", _error->errorCode())
                                              << LOG_KV("errorMsg", _error->errorMessage())
                                              << boost::diagnostic_information(_error);
                            _onGetTx(std::move(_error), receipt, nullptr);
                            return;
                        }

                        _onGetTx(nullptr, receipt, std::move(_proof));
                    });
            }
            else
            {
                LEDGER_LOG(TRACE) << "GetTransactionReceiptByHash success" << LOG_KV("hash", key);
                callback(nullptr, receipt, nullptr);
            }
        });
}

void Ledger::asyncGetTotalTransactionCount(
    std::function<void(Error::Ptr, int64_t, int64_t, bcos::protocol::BlockNumber)> _callback)
{
    static std::string_view keys[] = {
        SYS_KEY_TOTAL_TRANSACTION_COUNT, SYS_KEY_TOTAL_FAILED_TRANSACTION, SYS_KEY_CURRENT_NUMBER};

    LEDGER_LOG(INFO) << "GetTotalTransactionCount request";
    m_storage->asyncOpenTable(SYS_CURRENT_STATE, [this, callback = std::move(_callback)](
                                                     auto&& error, std::optional<Table>&& table) {
        auto tableError = checkTableValid(std::move(error), table, SYS_CURRENT_STATE);
        if (tableError)
        {
            LEDGER_LOG(ERROR) << "GetTotalTransactionCount error"
                              << boost::diagnostic_information(*tableError);
            callback(std::move(tableError), -1, -1, -1);
            return;
        }

        table->asyncGetRows(keys, [callback = std::move(callback)](
                                      auto&& error, std::vector<std::optional<Entry>>&& entries) {
            if (error)
            {
                LEDGER_LOG(ERROR) << "GetTotalTransactionCount error"
                                  << boost::diagnostic_information(*error);
                callback(
                    BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError, "Get row error", *error),
                    -1, -1, -1);
                return;
            }

            int64_t totalCount = 0, failedCount = 0, blockNumber = 0;
            size_t i = 0;
            for (auto& entry : entries)
            {
                int64_t value = 0;
                if (!entry)
                {
                    LEDGER_LOG(WARNING)
                        << "GetTotalTransactionCount error" << LOG_KV("index", i) << " empty";
                }
                else
                {
                    try
                    {
                        value = boost::lexical_cast<int64_t>(entry->getField(0));
                    }
                    catch (boost::bad_lexical_cast& e)
                    {
                        LEDGER_LOG(ERROR) << "Lexical cast transaction count failed, entry value: "
                                          << entry->get();
                        BOOST_THROW_EXCEPTION(e);
                    }
                }
                switch (i++)
                {
                case 0:
                    totalCount = value;
                    break;
                case 1:
                    failedCount = value;
                    break;
                case 2:
                    blockNumber = value;
                    break;
                }
            }

            LEDGER_LOG(INFO) << "GetTotalTransactionCount success"
                             << LOG_KV("totalCount", totalCount)
                             << LOG_KV("failedCount", failedCount)
                             << LOG_KV("blockNumber", blockNumber);
            callback(nullptr, totalCount, failedCount, blockNumber);
        });
    });
}

void Ledger::asyncGetSystemConfigByKey(const std::string& _key,
    std::function<void(Error::Ptr, std::string, bcos::protocol::BlockNumber)> _onGetConfig)
{
    LEDGER_LOG(INFO) << "GetSystemConfigByKey request" << LOG_KV("key", _key);

    asyncGetBlockNumber([this, callback = std::move(_onGetConfig), _key](
                            Error::Ptr error, bcos::protocol::BlockNumber blockNumber) {
        if (error)
        {
            LEDGER_LOG(ERROR) << "GetSystemConfigByKey error, "
                              << boost::diagnostic_information(*error);
            callback(std::move(error), "", -1);
            return;
        }

        asyncGetSystemTableEntry(SYS_CONFIG, _key,
            [blockNumber, _key, callback = std::move(callback)](
                Error::Ptr&& error, std::optional<bcos::storage::Entry>&& entry) {
                try
                {
                    // Note: should considerate the case that the compatibility_version is not
                    // setted
                    if (error)
                    {
                        LEDGER_LOG(ERROR) << "GetSystemConfigByKey error, "
                                          << boost::diagnostic_information(*error);
                        callback(std::move(error), "", -1);
                        return;
                    }

                    if (!entry)
                    {
                        LEDGER_LOG(WARNING) << "Entry doesn't exists";
                    }

                    LEDGER_LOG(TRACE) << "Entry value: " << toHex(entry->get());

                    auto [value, number] = entry->getObject<SystemConfigEntry>();

                    // The param was reset at height getLatestBlockNumber(), and takes effect in
                    // next block. So we query the status of getLatestBlockNumber() + 1.
                    auto effectNumber = blockNumber + 1;
                    if (number > effectNumber)
                    {
                        LEDGER_LOG(ERROR) << "GetSystemConfigByKey error, config not available"
                                          << LOG_KV("currentBlockNumber", effectNumber)
                                          << LOG_KV("available number", number);
                        callback(BCOS_ERROR_PTR(LedgerError::ErrorArgument, "Config not available"),
                            "", -1);
                        return;
                    }

                    LEDGER_LOG(INFO) << "GetSystemConfigByKey success" << LOG_KV("value", value)
                                     << LOG_KV("number", number);
                    callback(nullptr, std::move(value), number);
                }
                catch (std::exception& e)
                {
                    LEDGER_LOG(ERROR)
                        << "GetSystemConfigByKey error, " << boost::diagnostic_information(e);
                    callback(
                        BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError, "error", e), "", -1);
                }
            });
    });
}

void Ledger::asyncGetNonceList(bcos::protocol::BlockNumber _startNumber, int64_t _offset,
    std::function<void(
        Error::Ptr, std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>)>
        _onGetList)
{
    LEDGER_LOG(INFO) << "GetNonceList request" << LOG_KV("startNumber", _startNumber)
                     << LOG_KV("offset", _offset);

    if (_startNumber < 0 || _offset < 0)
    {
        LEDGER_LOG(ERROR) << "GetNonceList error";
        _onGetList(BCOS_ERROR_PTR(LedgerError::ErrorArgument, "Wrong argument"), nullptr);
        return;
    }

    m_storage->asyncOpenTable(SYS_BLOCK_NUMBER_2_NONCES, [this, callback = std::move(_onGetList),
                                                             _startNumber, _offset](auto&& error,
                                                             std::optional<Table>&& table) {
        auto tableError = checkTableValid(std::move(error), table, SYS_BLOCK_NUMBER_2_NONCES);
        if (tableError)
        {
            callback(std::move(tableError), nullptr);
            return;
        }

        auto numberList = std::vector<std::string>();
        for (BlockNumber i = _startNumber; i <= _startNumber + _offset; ++i)
        {
            numberList.push_back(boost::lexical_cast<std::string>(i));
        }

        table->asyncGetRows(numberList, [this, numberList, callback = std::move(callback)](
                                            auto&& error,
                                            std::vector<std::optional<Entry>>&& entries) {
            if (error)
            {
                LEDGER_LOG(ERROR) << "GetNonceList error" << boost::diagnostic_information(*error);
                callback(
                    BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError, "GetNonceList", *error),
                    nullptr);
                return;
            }

            auto retMap =
                std::make_shared<std::map<protocol::BlockNumber, protocol::NonceListPtr>>();

            for (size_t i = 0; i < numberList.size(); ++i)
            {
                try
                {
                    auto number = numberList[i];
                    auto entry = entries[i];
                    if (!entry)
                    {
                        continue;
                    }

                    auto value = entry->getField(0);
                    auto block = m_blockFactory->createBlock(
                        bcos::bytesConstRef((bcos::byte*)value.data(), value.size()), false, false);

                    auto nonceList = std::make_shared<protocol::NonceList>(block->nonceList());
                    retMap->emplace(
                        std::make_pair(boost::lexical_cast<BlockNumber>(number), nonceList));
                }
                catch (std::exception const& e)
                {
                    LEDGER_LOG(WARNING)
                        << "Parse nonce list error" << boost::diagnostic_information(e);
                    continue;
                }
            }

            LEDGER_LOG(INFO) << "GetNonceList success" << LOG_KV("retMap size", retMap->size());
            callback(nullptr, std::move(retMap));
        });
    });
}

void Ledger::asyncGetNodeListByType(const std::string& _type,
    std::function<void(Error::Ptr, consensus::ConsensusNodeListPtr)> _onGetConfig)
{
    LEDGER_LOG(INFO) << "GetNodeListByType request" << LOG_KV("type", _type);

    asyncGetBlockNumber([this, type = std::move(_type), callback = std::move(_onGetConfig)](
                            Error::Ptr&& error, bcos::protocol::BlockNumber blockNumber) {
        if (error)
        {
            LEDGER_LOG(ERROR) << "GetNodeListByType error" << boost::diagnostic_information(*error);
            callback(BCOS_ERROR_WITH_PREV_PTR(
                         LedgerError::GetStorageError, "GetNodeListByType error", *error),
                nullptr);
            return;
        }

        LEDGER_LOG(DEBUG) << "Get nodeList from" << LOG_KV("blockNumber", blockNumber);

        m_storage->asyncGetRow(SYS_CONSENSUS, "key",
            [callback = std::move(callback), type = type, this, blockNumber](
                Error::UniquePtr error, std::optional<Entry> entry) {
                if (error)
                {
                    callback(std::move(error), nullptr);
                    return;
                }

                auto nodeList = decodeConsensusList(entry->getField(0));
                auto nodes = std::make_shared<consensus::ConsensusNodeList>();

                auto effectNumber = blockNumber + 1;
                for (auto& it : nodeList)
                {
                    if (it.type == type &&
                        boost::lexical_cast<long>(it.enableNumber) <= effectNumber)
                    {
                        crypto::NodeIDPtr nodeID =
                            m_blockFactory->cryptoSuite()->keyFactory()->createKey(
                                fromHex(it.nodeID));
                        // Note: use try-catch to handle the exception case
                        nodes->emplace_back(std::make_shared<consensus::ConsensusNode>(
                            nodeID, it.weight.convert_to<uint64_t>()));
                    }
                }

                LEDGER_LOG(INFO) << "GetNodeListByType success"
                                 << LOG_KV("nodes size", nodes->size());
                callback(nullptr, std::move(nodes));
            });
    });
}

Error::Ptr Ledger::checkTableValid(Error::UniquePtr&& error,
    const std::optional<bcos::storage::Table>& table, const std::string_view& tableName)
{
    if (error)
    {
        std::stringstream ss;
        ss << "Open table: " << tableName << " failed!";
        LEDGER_LOG(ERROR) << ss.str() << boost::diagnostic_information(*error);

        return BCOS_ERROR_WITH_PREV_PTR(LedgerError::OpenTableFailed, ss.str(), *error);
    }

    if (!table)
    {
        std::stringstream ss;
        ss << "Table: " << tableName << " does not exists!";
        LEDGER_LOG(ERROR) << ss.str();
        return BCOS_ERROR_PTR(LedgerError::OpenTableFailed, ss.str());
    }

    return nullptr;
}

Error::Ptr Ledger::checkEntryValid(Error::UniquePtr&& error,
    const std::optional<bcos::storage::Entry>& entry, const std::string_view& key)
{
    if (error)
    {
        std::stringstream ss;
        ss << "Get row: " << key << " failed!";
        LEDGER_LOG(ERROR) << ss.str() << boost::diagnostic_information(*error);

        return BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError, ss.str(), *error);
    }

    if (!entry)
    {
        std::stringstream ss;
        ss << "Entry: " << key << " does not exists!";
        return BCOS_ERROR_PTR(LedgerError::GetStorageError, ss.str());
    }

    return nullptr;
}

void Ledger::asyncGetBlockHeader(bcos::protocol::Block::Ptr block,
    bcos::protocol::BlockNumber blockNumber, std::function<void(Error::Ptr&&)> callback)
{
    m_storage->asyncOpenTable(SYS_NUMBER_2_BLOCK_HEADER,
        [this, blockNumber, block, callback](auto&& error, std::optional<Table>&& table) {
            auto validError = checkTableValid(std::move(error), table, SYS_NUMBER_2_BLOCK_HEADER);
            if (validError)
            {
                callback(std::move(validError));
                return;
            }

            table->asyncGetRow(boost::lexical_cast<std::string>(blockNumber),
                [this, blockNumber, block, callback](auto&& error, std::optional<Entry>&& entry) {
                    auto validError = checkEntryValid(
                        std::move(error), entry, boost::lexical_cast<std::string>(blockNumber));
                    if (validError)
                    {
                        callback(std::move(validError));
                        return;
                    }

                    auto field = entry->getField(0);
                    auto headerPtr = m_blockFactory->blockHeaderFactory()->createBlockHeader(
                        bcos::bytesConstRef((bcos::byte*)field.data(), field.size()));

                    block->setBlockHeader(std::move(headerPtr));
                    callback(nullptr);
                });
        });
}

void Ledger::asyncGetBlockTransactionHashes(bcos::protocol::BlockNumber blockNumber,
    std::function<void(Error::Ptr&&, std::vector<std::string>&&)> callback)
{
    m_storage->asyncOpenTable(SYS_NUMBER_2_TXS,
        [this, blockNumber, callback](auto&& error, std::optional<Table>&& table) {
            auto validError = checkTableValid(std::move(error), table, SYS_NUMBER_2_BLOCK_HEADER);
            if (validError)
            {
                callback(std::move(validError), std::vector<std::string>());
                return;
            }

            table->asyncGetRow(boost::lexical_cast<std::string>(blockNumber),
                [this, blockNumber, callback](auto&& error, std::optional<Entry>&& entry) {
                    auto validError = checkEntryValid(
                        std::move(error), entry, boost::lexical_cast<std::string>(blockNumber));
                    if (validError)
                    {
                        callback(std::move(validError), std::vector<std::string>());
                        return;
                    }

                    auto txs = entry->getField(0);
                    auto blockWithTxs = m_blockFactory->createBlock(
                        bcos::bytesConstRef((bcos::byte*)txs.data(), txs.size()));

                    std::vector<std::string> hashList(blockWithTxs->transactionsHashSize());
                    for (size_t i = 0; i < blockWithTxs->transactionsHashSize(); ++i)
                    {
                        auto hash = blockWithTxs->transactionHash(i);
                        hashList[i] = hash.hex();
                    }

                    callback(nullptr, std::move(hashList));
                });
        });
}

void Ledger::asyncBatchGetTransactions(std::shared_ptr<std::vector<std::string>> hashes,
    std::function<void(Error::Ptr&&, std::vector<protocol::Transaction::Ptr>&&)> callback)
{
    LEDGER_LOG(TRACE) << "Hashes: " << hashes->size();

    m_storage->asyncOpenTable(
        SYS_HASH_2_TX, [this, hashes, callback](auto&& error, std::optional<Table>&& table) {
            auto validError = checkTableValid(std::move(error), table, SYS_HASH_2_TX);
            if (validError)
            {
                callback(std::move(validError), std::vector<protocol::Transaction::Ptr>());
                return;
            }

            std::vector<std::string_view> hashesView;
            hashesView.reserve(hashes->size());
            for (auto& hash : *hashes)
            {
                hashesView.push_back(hash);
            }

            table->asyncGetRows(hashesView, [this, hashes, callback](auto&& error,
                                                std::vector<std::optional<Entry>>&& entries) {
                if (error)
                {
                    LEDGER_LOG(TRACE)
                        << "Batch get transaction error!" << boost::diagnostic_information(*error);
                    callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                 "Batch get transaction error!", *error),
                        std::vector<protocol::Transaction::Ptr>());

                    return;
                }

                std::vector<protocol::Transaction::Ptr> transactions;
                size_t i = 0;
                for (auto& entry : entries)
                {
                    if (!entry.has_value())
                    {
                        LEDGER_LOG(TRACE)
                            << "Get transaction failed: " << (*hashes)[i] << " not found";
                    }
                    else
                    {
                        auto field = entry->getField(0);
                        auto transaction = m_blockFactory->transactionFactory()->createTransaction(
                            bcos::bytesConstRef((bcos::byte*)field.data(), field.size()));
                        transactions.push_back(std::move(transaction));
                    }

                    ++i;
                }
                if (transactions.size() != hashes->size())
                {
                    LEDGER_LOG(TRACE)
                        << "Batch get transaction error, transactions size not match hashesSize"
                        << LOG_KV("txsSize", transactions.size())
                        << LOG_KV("hashesSize", hashes->size());
                    callback(BCOS_ERROR_PTR(LedgerError::CollectAsyncCallbackError,
                                 "Batch get transaction error, transactions size not match "
                                 "hashesSize, txsSize: " +
                                     std::to_string(transactions.size()) +
                                     ", hashesSize: " + std::to_string(hashes->size())),
                        std::move(transactions));
                    return;
                }

                callback(nullptr, std::move(transactions));
            });
        });
}

void Ledger::asyncBatchGetReceipts(std::shared_ptr<std::vector<std::string>> hashes,
    std::function<void(Error::Ptr&&, std::vector<protocol::TransactionReceipt::Ptr>&&)> callback)
{
    m_storage->asyncOpenTable(
        SYS_HASH_2_RECEIPT, [this, hashes, callback](auto&& error, std::optional<Table>&& table) {
            auto validError = checkTableValid(std::move(error), table, SYS_HASH_2_RECEIPT);
            if (validError)
            {
                callback(std::move(validError), std::vector<protocol::TransactionReceipt::Ptr>());
                return;
            }

            table->asyncGetRows(*hashes, [this, hashes, callback](auto&& error,
                                             std::vector<std::optional<Entry>>&& entries) {
                if (error)
                {
                    LEDGER_LOG(ERROR)
                        << "Batch get receipt error!" << boost::diagnostic_information(*error);
                    callback(BCOS_ERROR_WITH_PREV_PTR(
                                 LedgerError::GetStorageError, "Batch get receipt error!", *error),
                        std::vector<protocol::TransactionReceipt::Ptr>());

                    return;
                }

                size_t i = 0;
                std::vector<protocol::TransactionReceipt::Ptr> receipts;
                receipts.reserve(hashes->size());
                for (auto& entry : entries)
                {
                    if (!entry.has_value())
                    {
                        LEDGER_LOG(ERROR) << "Get receipt error: " << (*hashes)[i];
                        callback(BCOS_ERROR_PTR(
                                     LedgerError::GetStorageError, "Batch get transaction error!"),
                            std::vector<protocol::TransactionReceipt::Ptr>());
                        return;
                    }

                    auto field = entry->getField(0);
                    auto receipt = m_blockFactory->receiptFactory()->createReceipt(
                        bcos::bytesConstRef((bcos::byte*)field.data(), field.size()));
                    receipts.push_back(std::move(receipt));

                    ++i;
                }

                callback(nullptr, std::move(receipts));
            });
        });
}

void Ledger::asyncGetSystemTableEntry(const std::string_view& table, const std::string_view& key,
    std::function<void(Error::Ptr&&, std::optional<bcos::storage::Entry>&&)> callback)
{
    m_storage->asyncOpenTable(table, [this, key = std::string(key), callback = std::move(callback)](
                                         auto&& error, std::optional<Table>&& table) {
        auto tableError =
            checkTableValid(std::forward<decltype(error)>(error), table, SYS_CURRENT_STATE);
        if (tableError)
        {
            callback(std::move(tableError), {});
            return;
        }

        table->asyncGetRow(key, [this, key, callback = std::move(callback)](
                                    auto&& error, std::optional<Entry>&& entry) {
            auto entryError = checkEntryValid(std::move(error), entry, key);
            if (entryError)
            {
                callback(std::move(entryError), {});
                return;
            }

            callback(nullptr, std::move(entry));
        });
    });
}

void Ledger::getTxProof(
    const HashType& _txHash, std::function<void(Error::Ptr&&, MerkleProofPtr&&)> _onGetProof)
{
    // txHash->receipt receipt->number number->txHash
    asyncGetTransactionReceiptByHash(_txHash, false,
        [this, _txHash, _onGetProof = std::move(_onGetProof)](
            Error::Ptr _error, TransactionReceipt::ConstPtr _receipt, const MerkleProofPtr&) {
            if (_error || !_receipt)
            {
                LEDGER_LOG(ERROR) << LOG_BADGE("getTxProof")
                                  << LOG_DESC("getReceiptByTxHash from storage error")
                                  << LOG_KV("txHash", _txHash.hex());
                _onGetProof(std::forward<decltype(_error)>(_error), nullptr);
                return;
            }
            asyncGetBlockTransactionHashes(_receipt->blockNumber(),
                [this, _onGetProof, _txHash = std::move(_txHash)](
                    Error::Ptr&& _error, std::vector<std::string>&& _hashList) {
                    if (_error || _hashList.empty())
                    {
                        LEDGER_LOG(ERROR)
                            << LOG_BADGE("getTxProof")
                            << LOG_DESC("asyncGetBlockTransactionHashes from storage error")
                            << LOG_KV("txHash", _txHash.hex());
                        _onGetProof(std::forward<decltype(_error)>(_error), nullptr);
                        return;
                    }
                    asyncBatchGetTransactions(std::make_shared<std::vector<std::string>>(_hashList),
                        [cryptoSuite = m_blockFactory->cryptoSuite(), _onGetProof,
                            _txHash = std::move(_txHash)](
                            Error::Ptr&& _error, std::vector<Transaction::Ptr>&& _txList) {
                            if (_error || _txList.empty())
                            {
                                LEDGER_LOG(ERROR)
                                    << LOG_BADGE("getTxProof") << LOG_DESC("getTxs callback error")
                                    << LOG_KV("errorCode", _error->errorCode())
                                    << LOG_KV("errorMsg", _error->errorMessage());
                                _onGetProof(std::forward<decltype(_error)>(_error), nullptr);
                                return;
                            }

                            auto merkleProofPtr = std::make_shared<MerkleProof>();
                            auto merkleProofUtility = std::make_shared<MerkleProofUtility>();

                            merkleProofUtility->getMerkleProof(_txHash,
                                std::forward<decltype(_txList)>(_txList), cryptoSuite,
                                merkleProofPtr);
                            LEDGER_LOG(TRACE)
                                << LOG_BADGE("getTxProof") << LOG_DESC("get merkle proof success")
                                << LOG_KV("txHash", _txHash.hex());
                            _onGetProof(nullptr, std::move(merkleProofPtr));
                        });
                });
        });
}

void Ledger::getReceiptProof(protocol::TransactionReceipt::Ptr _receipt,
    std::function<void(Error::Ptr&&, MerkleProofPtr&&)> _onGetProof)
{
    // receipt->number number->txs txs->receipts
    asyncGetBlockTransactionHashes(_receipt->blockNumber(),
        [this, _onGetProof = std::move(_onGetProof), receiptHash = _receipt->hash()](
            Error::Ptr&& _error, std::vector<std::string>&& _hashList) {
            if (_error)
            {
                _onGetProof(std::forward<decltype(_error)>(_error), nullptr);
                return;
            }

            asyncBatchGetReceipts(std::make_shared<std::vector<std::string>>(_hashList),
                [cryptoSuite = this->m_blockFactory->cryptoSuite(), _onGetProof,
                    receiptHash = receiptHash](Error::Ptr&& _error,
                    std::vector<protocol::TransactionReceipt::Ptr>&& _receiptList) {
                    if (_error || _receiptList.empty())
                    {
                        LEDGER_LOG(ERROR) << LOG_BADGE("getReceiptProof")
                                          << LOG_DESC("asyncBatchGetReceipts callback error");
                        _onGetProof(std::forward<decltype(_error)>(_error), nullptr);
                        return;
                    }
                    auto merkleProof = std::make_shared<MerkleProof>();
                    auto merkleProofUtility = std::make_shared<MerkleProofUtility>();
                    merkleProofUtility->getMerkleProof(receiptHash,
                        std::forward<decltype(_receiptList)>(_receiptList), cryptoSuite,
                        merkleProof);
                    LEDGER_LOG(INFO)
                        << LOG_BADGE("getReceiptProof") << LOG_DESC("call back receipt and proof");
                    _onGetProof(nullptr, std::move(merkleProof));
                });
        });
}

// sync method
bool Ledger::buildGenesisBlock(LedgerConfig::Ptr _ledgerConfig, size_t _gasLimit,
    const std::string& _genesisData, std::string const& _compatibilityVersion)
{
    LEDGER_LOG(INFO) << LOG_DESC("[#buildGenesisBlock]");
    if (_gasLimit < TX_GAS_LIMIT_MIN)
    {
        LEDGER_LOG(ERROR) << LOG_BADGE("buildGenesisBlock")
                          << LOG_DESC("gas limit too low, return false")
                          << LOG_KV("gasLimit", _gasLimit)
                          << LOG_KV("gasLimitMin", TX_GAS_LIMIT_MIN);
        return false;
    }

    std::promise<std::tuple<Error::Ptr, bcos::crypto::HashType>> getBlockPromise;
    asyncGetBlockHashByNumber(0, [&](Error::Ptr error, bcos::crypto::HashType hash) {
        getBlockPromise.set_value({std::move(error), hash});
    });

    auto getBlockResult = getBlockPromise.get_future().get();
    if (std::get<0>(getBlockResult) &&
        std::get<0>(getBlockResult)->errorCode() != LedgerError::GetStorageError)
    {
        BOOST_THROW_EXCEPTION(*(std::get<0>(getBlockResult)));
    }

    if (std::get<1>(getBlockResult))
    {
        // genesis block exists, quit
        LEDGER_LOG(INFO) << LOG_DESC("[#buildGenesisBlock] success, block exists");
        return true;
    }

    // clang-format off
    std::string_view tables[] = {
        SYS_CONFIG, "value,enable_number",
        SYS_CONSENSUS, SYS_VALUE,
        SYS_CURRENT_STATE, SYS_VALUE,
        SYS_HASH_2_TX, SYS_VALUE,
        SYS_HASH_2_NUMBER, SYS_VALUE,
        SYS_NUMBER_2_HASH, SYS_VALUE,
        SYS_NUMBER_2_BLOCK_HEADER, SYS_VALUE,
        SYS_NUMBER_2_TXS, SYS_VALUE,
        SYS_HASH_2_RECEIPT, SYS_VALUE,
        SYS_BLOCK_NUMBER_2_NONCES, SYS_VALUE,
        DAG_TRANSFER, "balance"
    };
    // clang-format on
    size_t total = sizeof(tables) / sizeof(std::string_view);

    for (size_t i = 0; i < total; i += 2)
    {
        std::promise<std::tuple<Error::UniquePtr>> createTablePromise;
        m_storage->asyncCreateTable(std::string(tables[i]), std::string(tables[i + 1]),
            [&createTablePromise](auto&& error, std::optional<Table>&&) {
                createTablePromise.set_value({std::move(error)});
            });
        auto createTableResult = createTablePromise.get_future().get();
        if (std::get<0>(createTableResult))
        {
            BOOST_THROW_EXCEPTION(*(std::get<0>(createTableResult)));
        }
    }

    createFileSystemTables();

    auto txLimit = _ledgerConfig->blockTxCountLimit();
    LEDGER_LOG(INFO) << LOG_DESC("Commit the genesis block") << LOG_KV("txLimit", txLimit)
                     << LOG_KV("leaderSwitchPeriod", _ledgerConfig->leaderSwitchPeriod())
                     << LOG_KV("blockTxCountLimit", _ledgerConfig->blockTxCountLimit())
                     << LOG_KV("compatibilityVersion", _compatibilityVersion)
                     << LOG_KV("minSupportedVersion", g_BCOSConfig.minSupportedVersion())
                     << LOG_KV("maxSupportedVersion", g_BCOSConfig.maxSupportedVersion());

    // build a block
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(0);
    header->setExtraData(asBytes(_genesisData));

    auto block = m_blockFactory->createBlock();
    block->setBlockHeader(header);

    std::promise<Error::Ptr> genesisBlockPromise;
    asyncPrewriteBlock(m_storage, nullptr, block, [&genesisBlockPromise](Error::Ptr&& error) {
        genesisBlockPromise.set_value(std::move(error));
    });

    auto error = genesisBlockPromise.get_future().get();
    if (error)
    {
        BOOST_THROW_EXCEPTION(*error);
    }

    // write sys config
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> sysTablePromise;
    m_storage->asyncOpenTable(
        SYS_CONFIG, [&sysTablePromise](auto&& error, std::optional<Table>&& table) {
            sysTablePromise.set_value({std::move(error), std::move(table)});
        });

    auto [tableError, sysTable] = sysTablePromise.get_future().get();
    if (tableError)
    {
        BOOST_THROW_EXCEPTION(*tableError);
    }

    if (!sysTable)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(LedgerError::OpenTableFailed, "Open SYS_CONFIG failed!"));
    }

    // tx count limit
    Entry txLimitEntry;
    txLimitEntry.setObject(
        SystemConfigEntry{boost::lexical_cast<std::string>(_ledgerConfig->blockTxCountLimit()), 0});
    sysTable->setRow(SYSTEM_KEY_TX_COUNT_LIMIT, std::move(txLimitEntry));

    // tx gas limit
    Entry gasLimitEntry;
    gasLimitEntry.setObject(SystemConfigEntry{boost::lexical_cast<std::string>(_gasLimit), 0});
    sysTable->setRow(SYSTEM_KEY_TX_GAS_LIMIT, std::move(gasLimitEntry));

    // consensus leader period
    Entry leaderPeriodEntry;
    leaderPeriodEntry.setObject(SystemConfigEntry{
        boost::lexical_cast<std::string>(_ledgerConfig->leaderSwitchPeriod()), 0});
    sysTable->setRow(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, std::move(leaderPeriodEntry));

    auto versionNumber = bcos::tool::toVersionNumber(_compatibilityVersion);
    LEDGER_LOG(INFO) << LOG_DESC("init the compatibilityVersion")
                     << LOG_KV("versionNumber", versionNumber);
    // write compatibility version
    Entry compatibilityVersionEntry;
    compatibilityVersionEntry.setObject(SystemConfigEntry{_compatibilityVersion, 0});
    sysTable->setRow(SYSTEM_KEY_COMPATIBILITY_VERSION, std::move(compatibilityVersionEntry));

    // write consensus node list
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> consensusTablePromise;
    m_storage->asyncOpenTable(SYS_CONSENSUS, [&consensusTablePromise](
                                                 auto&& error, std::optional<Table>&& table) {
        consensusTablePromise.set_value({std::forward<decltype(error)>(error), std::move(table)});
    });

    auto [consensusError, consensusTable] = consensusTablePromise.get_future().get();
    if (consensusError)
    {
        BOOST_THROW_EXCEPTION(*consensusError);
    }

    if (!consensusTable)
    {
        BOOST_THROW_EXCEPTION(
            BCOS_ERROR(LedgerError::OpenTableFailed, "Open SYS_CONSENSUS failed!"));
    }

    ConsensusNodeList consensusNodeList;

    for (auto& node : _ledgerConfig->consensusNodeList())
    {
        consensusNodeList.emplace_back(
            node->nodeID()->hex(), node->weight(), CONSENSUS_SEALER, "0");
    }

    for (auto& node : _ledgerConfig->observerNodeList())
    {
        consensusNodeList.emplace_back(
            node->nodeID()->hex(), node->weight(), CONSENSUS_OBSERVER, "0");
    }

    Entry consensusNodeListEntry;
    consensusNodeListEntry.importFields({encodeConsensusList(consensusNodeList)});

    std::promise<Error::UniquePtr> setConsensusNodeListPromise;
    consensusTable->asyncSetRow("key", std::move(consensusNodeListEntry),
        [&setConsensusNodeListPromise](
            Error::UniquePtr&& error) { setConsensusNodeListPromise.set_value(std::move(error)); });

    auto setConsensusNodeListError = setConsensusNodeListPromise.get_future().get();
    if (setConsensusNodeListError)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR_WITH_PREV(
            LedgerError::CallbackError, "Write genesis consensus node list error!", *error));
    }

    // write current state
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> stateTablePromise;
    m_storage->asyncOpenTable(
        SYS_CURRENT_STATE, [&stateTablePromise](auto&& error, std::optional<Table>&& table) {
            stateTablePromise.set_value({std::forward<decltype(error)>(error), std::move(table)});
        });

    auto [stateError, stateTable] = stateTablePromise.get_future().get();
    if (stateError)
    {
        BOOST_THROW_EXCEPTION(*stateError);
    }

    if (!stateTable)
    {
        BOOST_THROW_EXCEPTION(
            BCOS_ERROR(LedgerError::OpenTableFailed, "Open SYS_CURRENT_STATE failed!"));
    }

    Entry currentNumber;
    currentNumber.importFields({"0"});
    stateTable->setRow(SYS_KEY_CURRENT_NUMBER, std::move(currentNumber));

    Entry txNumber;
    txNumber.importFields({"0"});
    stateTable->setRow(SYS_KEY_TOTAL_TRANSACTION_COUNT, std::move(txNumber));

    Entry txFailedNumber;
    txFailedNumber.importFields({"0"});
    stateTable->setRow(SYS_KEY_TOTAL_FAILED_TRANSACTION, std::move(txFailedNumber));

    return true;
}

void Ledger::createFileSystemTables()
{
    // create / dir
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> createPromise;
    m_storage->asyncCreateTable(
        FS_ROOT, SYS_VALUE, [&createPromise](auto&& error, std::optional<Table>&& _table) {
            createPromise.set_value({std::forward<decltype(error)>(error), std::move(_table)});
        });
    auto [createError, rootTable] = createPromise.get_future().get();
    if (createError)
    {
        BOOST_THROW_EXCEPTION(*createError);
    }

    // root table must exist

    Entry tEntry, newSubEntry, aclTypeEntry, aclWEntry, aclBEntry, extraEntry;
    std::map<std::string, std::string> newSubMap;
    newSubMap.insert(std::make_pair("usr", FS_TYPE_DIR));
    newSubMap.insert(std::make_pair("apps", FS_TYPE_DIR));
    newSubMap.insert(std::make_pair("sys", FS_TYPE_DIR));
    newSubMap.insert(std::make_pair("tables", FS_TYPE_DIR));

    tEntry.importFields({FS_TYPE_DIR});
    newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
    aclTypeEntry.importFields({"0"});
    aclWEntry.importFields({""});
    aclBEntry.importFields({""});
    extraEntry.importFields({""});
    rootTable->setRow(FS_KEY_TYPE, std::move(tEntry));
    rootTable->setRow(FS_KEY_SUB, std::move(newSubEntry));
    rootTable->setRow(FS_ACL_TYPE, std::move(aclTypeEntry));
    rootTable->setRow(FS_ACL_WHITE, std::move(aclWEntry));
    rootTable->setRow(FS_ACL_BLACK, std::move(aclBEntry));
    rootTable->setRow(FS_KEY_EXTRA, std::move(extraEntry));

    buildDir(FS_USER);
    buildDir(FS_SYS_BIN);
    buildDir(FS_APPS);
    buildDir(FS_USER_TABLE);
}

void Ledger::buildDir(const std::string& _absoluteDir)
{
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> createPromise;
    m_storage->asyncCreateTable(
        _absoluteDir, SYS_VALUE, [&createPromise](auto&& error, std::optional<Table>&& _table) {
            createPromise.set_value({std::forward<decltype(error)>(error), std::move(_table)});
        });
    auto [createError, table] = createPromise.get_future().get();
    if (createError)
    {
        BOOST_THROW_EXCEPTION(*createError);
    }
    Entry tEntry, newSubEntry, aclTypeEntry, aclWEntry, aclBEntry, extraEntry;
    std::map<std::string, std::string> newSubMap;
    if (_absoluteDir == FS_SYS_BIN)
    {
        // clang-format off
        std::vector<std::string> sysContracts({
            getSysBaseName(precompiled::SYS_CONFIG_NAME),
            getSysBaseName(precompiled::CONSENSUS_NAME),
            getSysBaseName(precompiled::AUTH_MANAGER_NAME),
            getSysBaseName(precompiled::KV_TABLE_NAME),
            getSysBaseName(precompiled::CRYPTO_NAME),
            getSysBaseName(precompiled::BFS_NAME),
            getSysBaseName(precompiled::TABLE_MANAGER_NAME)
        });
        // clang-format on
        for (const auto& contract : sysContracts)
        {
            newSubMap.insert(std::make_pair(contract, FS_TYPE_CONTRACT));
        }
    }
    tEntry.importFields({FS_TYPE_DIR});
    newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
    aclTypeEntry.importFields({"0"});
    aclWEntry.importFields({""});
    aclBEntry.importFields({""});
    extraEntry.importFields({""});
    table->setRow(FS_KEY_TYPE, std::move(tEntry));
    table->setRow(FS_KEY_SUB, std::move(newSubEntry));
    table->setRow(FS_ACL_TYPE, std::move(aclTypeEntry));
    table->setRow(FS_ACL_WHITE, std::move(aclWEntry));
    table->setRow(FS_ACL_BLACK, std::move(aclBEntry));
    table->setRow(FS_KEY_EXTRA, std::move(extraEntry));
}