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
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/storage/StorageInvokes.h"
#include "bcos-tool/VersionConverter.h"
#include "bcos-utilities/Common.h"
#include "utilities/Common.h"
#include <bcos-codec/scale/Scale.h>
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-framework/consensus/ConsensusNode.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/storage/Table.h>
#include <bcos-protocol/ParallelMerkleProof.h>
#include <bcos-task/Wait.h>
#include <bcos-tool/BfsFileFactory.h>
#include <bcos-tool/ConsensusNode.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <tbb/parallel_for.h>
#include <tbb/task_group.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <cstddef>
#include <future>
#include <memory>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>
#include <utility>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::crypto;
using namespace bcos::tool;


void Ledger::asyncPreStoreBlockTxs(bcos::protocol::TransactionsPtr _blockTxs,
    bcos::protocol::Block::ConstPtr block, std::function<void(Error::UniquePtr&&)> _callback)
{
    auto txsToSaveResult = needStoreUnsavedTxs(_blockTxs, block);
    bool shouldStoreTxs = std::get<0>(txsToSaveResult);
    if (!shouldStoreTxs)
    {
        std::cout << __LINE__ << std::endl;
        _callback(nullptr);
        return;
    }
    auto startT = utcTime();
    auto blockTxsSize = _blockTxs->size();
    auto unstoredTxsHash = std::get<1>(txsToSaveResult);
    auto unstoredTxs = std::get<2>(txsToSaveResult);

    auto blockVersion = block->blockHeaderConst()->version();
    auto blockNumber = block->blockHeaderConst()->number();
    auto total = unstoredTxs->size();
    if (blockVersion >= uint32_t(bcos::protocol::BlockVersion::V3_5_VERSION))
    {
        std::vector<std::string> txHashKeys;
        txHashKeys.reserve(total);
        std::vector<std::string> values;
        values.reserve(total);
        auto transactionsBlock = m_blockFactory->createBlock();
        for (size_t i = 0; i < unstoredTxs->size(); ++i)
        {
            // save txs data in block
            auto tx = m_blockFactory->transactionFactory()->createTransaction(
                bcos::ref(*(*unstoredTxs)[i]));
            transactionsBlock->appendTransaction(tx);

            // save txhash : blockNumber , txIndex
            auto blockNumberStr = boost::lexical_cast<std::string>(blockNumber);
            auto txIndex = boost::lexical_cast<std::string>(i);
            // txHashKeys[i] = concepts::bytebuffer::toView((*unstoredTxsHash)[i]);
            txHashKeys.push_back(std::string(concepts::bytebuffer::toView((*unstoredTxsHash)[i])));
            auto str = blockNumberStr + "+" + txIndex;
            // std::string str = blockNumberStr.append("+").append(txIndex);
            values.push_back(str);

            LEDGER_LOG(INFO) << LOG_DESC("asyncPreStoreBlockTxs SYS_TXHASH_2_NUMBER")
                             << LOG_KV("key", toHex(txHashKeys[i])) << LOG_KV("value", values[i]);

            // Entry entry;
            // entry.setObject(std::vector<std::string>{
            //     blockNumberStr,
            //     txIndex,
            // });
            // entryValues.push_back(std::move(entry));
        }

        {
            // save txs data in block
            bytes transactionsBuffer;
            transactionsBlock->encode(transactionsBuffer);
            Entry number2BlockTxsEntry;
            number2BlockTxsEntry.importFields({std::move(transactionsBuffer)});
            m_storage->asyncSetRow(SYS_NUMBER_2_BLOCK_TXS,
                boost::lexical_cast<std::string>(blockNumber), std::move(number2BlockTxsEntry),
                [_callback](auto&& error) {
                    if (error)
                    {
                        std::cout << __LINE__ << std::endl;
                        _callback(std::forward<decltype(error)>(error));
                        return;
                    }
                });

            RecursiveGuard l(m_mutex);
            // tbb::parallel_for(tbb::blocked_range<size_t>(0, total),
            //     [this, &txHashKeys, &entryValues, blockNumber, _callback, total](
            //         const tbb::blocked_range<size_t>& range) {
            //         for (size_t j = 0; j < total; ++j)
            //         {
            //             m_storage->asyncSetRow(SYS_TXHASH_2_NUMBER, txHashKeys[j],
            //             entryValues[j],
            //                 [_callback, blockNumber, j, &entryValues, &txHashKeys](auto&& error)
            //                 {
            //                     if (error)
            //                     {
            //                         std::cout << __LINE__ << std::endl;
            //                         _callback(std::forward<decltype(error)>(error));
            //                         return;
            //                     }
            //                 });
            //         }
            //     });

            LEDGER_LOG(INFO) << LOG_DESC("asyncPreStoreBlockTxs SYS_TXHASH_2_NUMBER")
                             << LOG_KV("key size", txHashKeys.size())
                             << LOG_KV("value size", values.size());

            LEDGER_LOG(INFO) << LOG_DESC("asyncPreStoreBlockTxs SYS_TXHASH_2_NUMBER")
                    << LOG_KV("key", toHex(txHashKeys[0])) << LOG_KV("value", values[0]);

            bcos::Error::Ptr error = nullptr;
            error =
                m_storage->setRows(SYS_TXHASH_2_NUMBER, std::move(txHashKeys), std::move(values));

            LEDGER_LOG(INFO) << LOG_DESC("asyncPreStoreBlockTxs: store uncommitted txs")
                             << LOG_KV("blockNumber", blockNumber)
                             << LOG_KV("blockTxsSize", blockTxsSize)
                             << LOG_KV("unStoredTxs", unstoredTxsHash->size())
                             << LOG_KV("msg", error ? error->errorMessage() : "success")
                             << LOG_KV("code", error ? error->errorCode() : 0)
                             << LOG_KV("timeCost", (utcTime() - startT));
            if (error)
            {
                std::cout << __LINE__ << std::endl;
                _callback(std::make_unique<Error>(*error));
                return;
            }

            // set the flag when store success
            for (auto const& tx : *_blockTxs)
            {
                tx->setStoreToBackend(true);
            }

            asyncGetTransactionByHash(txHashKeys[0],
                [this, blockNumber](auto&& error, protocol::Transaction::Ptr transaction) {
                    if (error)
                    {
                        LEDGER_LOG(INFO) << LOG_DESC("setRows SYS_TXHASH_2_NUMBER")
                                << LOG_KV("blockNumber", blockNumber)
                                << LOG_DESC("error");
                        return;
                    }
                    if(transaction == nullptr)
                    {
                        LEDGER_LOG(INFO) << LOG_DESC("setRows SYS_TXHASH_2_NUMBER")
                                << LOG_KV("blockNumber", blockNumber)
                                << LOG_DESC(", set wrong");
                        return;
                    }
                    LEDGER_LOG(INFO) << LOG_DESC("setRows SYS_TXHASH_2_NUMBER")
                                << LOG_KV("blockNumber", blockNumber)
                                << LOG_DESC(", set right");
                }

            );
        }
    }
    else
    {
        std::vector<std::string_view> txHashKeys(total);
        std::vector<std::string_view> values(total);
        for (auto i = 0U; i < unstoredTxs->size(); ++i)
        {
            txHashKeys[i] = concepts::bytebuffer::toView((*unstoredTxsHash)[i]);
            values[i] = concepts::bytebuffer::toView((*(*unstoredTxs)[i]));
        }

        {
            // Note: transactions must be submitted serially, because transaction submissions are
            // transactional, preventing write conflicts
            RecursiveGuard l(m_mutex);
            bcos::Error::Ptr error = nullptr;
            error = m_storage->setRows(SYS_HASH_2_TX, std::move(txHashKeys), std::move(values));

            LEDGER_LOG(INFO) << LOG_DESC("asyncPreStoreBlockTxs: store uncommitted txs")
                             << LOG_KV("blockNumber", blockNumber)
                             << LOG_KV("blockTxsSize", blockTxsSize)
                             << LOG_KV("unStoredTxs", unstoredTxsHash->size())
                             << LOG_KV("msg", error ? error->errorMessage() : "success")
                             << LOG_KV("code", error ? error->errorCode() : 0)
                             << LOG_KV("timeCost", (utcTime() - startT));
            if (error)
            {
                std::cout << __LINE__ << std::endl;
                _callback(std::make_unique<Error>(*error));
                return;
            }
            // set the flag when store success
            for (auto const& tx : *_blockTxs)
            {
                tx->setStoreToBackend(true);
            }
        }
    }
    std::cout << __LINE__ << std::endl;
    _callback(nullptr);
}

void Ledger::asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
    bcos::protocol::TransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
    std::function<void(Error::Ptr&&)> callback,
    bool writeTxsAndReceipts)  // Unused flag writeTxsAndReceipts
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
            [callback](Error::Ptr&& error) {
                if (error)
                {
                    LEDGER_LOG(ERROR) << "System contract write ledger storage error "
                                      << LOG_KV("msg", error->errorMessage())
                                      << LOG_KV("code", error->errorCode());
                }
                callback(std::forward<decltype(error)>(error));
            });
        return;
    }
    auto header = block->blockHeaderConst();
    auto blockVersion = header->version();
    auto blockNumberStr = boost::lexical_cast<std::string>(header->number());


    size_t TOTAL_CALLBACK = 8;
    if (writeTxsAndReceipts)
    {  // 9 storage callbacks and write hash=>tx
        TOTAL_CALLBACK = 9;
        // if (blockVersion >= uint32_t(bcos::protocol::BlockVersion::V3_5_VERSION))
        // {
        //     --TOTAL_CALLBACK;
        // }
    }
    std::cout << "init total = " << TOTAL_CALLBACK << std::endl;
    // if (blockVersion >= uint32_t(bcos::protocol::BlockVersion::V3_5_VERSION))
    // {
    //     TOTAL_CALLBACK = 8;
    //     if (writeTxsAndReceipts)
    //     {
    //         TOTAL_CALLBACK = 9;
    //     }
    // }

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
        std::cout << "total = " << *total << std::endl;

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

            callback(nullptr);
        }
    };

    // number 2 hash
    Entry hashEntry;
    hashEntry.importFields({header->hash().asBytes()});
    storage->asyncSetRow(
        SYS_NUMBER_2_HASH, blockNumberStr, std::move(hashEntry), [setRowCallback](auto&& error) {
            std::cout << __LINE__ << std::endl;
            setRowCallback(std::forward<decltype(error)>(error));
        });

    // hash 2 number
    Entry hash2NumberEntry;
    hash2NumberEntry.importFields({blockNumberStr});
    storage->asyncSetRow(SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(header->hash()),
        std::move(hash2NumberEntry), [setRowCallback](auto&& error) {
            std::cout << __LINE__ << std::endl;
            setRowCallback(std::forward<decltype(error)>(error));
        });

    // number 2 header
    bytes headerBuffer;
    header->encode(headerBuffer);

    Entry number2HeaderEntry;
    number2HeaderEntry.importFields({std::move(headerBuffer)});
    storage->asyncSetRow(SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr, std::move(number2HeaderEntry),
        [setRowCallback](auto&& error) {
            std::cout << __LINE__ << std::endl;
            setRowCallback(std::forward<decltype(error)>(error));
        });

    // number 2 nonce
    auto nonceBlock = m_blockFactory->createBlock();
    protocol::NonceList nonceList;
    // get nonce from _blockTxs
    if (_blockTxs)
    {
        for (auto const& tx : *_blockTxs)
        {
            nonceList.emplace_back(tx->nonce());
        }
    }
    // get nonce from block txs
    else
    {
        for (uint64_t i = 0; i < block->transactionsSize(); i++)
        {
            auto const& tx = block->transaction(i);
            nonceList.emplace_back(tx->nonce());
        }
    }
    nonceBlock->setNonceList(nonceList);
    bytes nonceBuffer;
    nonceBlock->encode(nonceBuffer);

    Entry number2NonceEntry;
    number2NonceEntry.importFields({std::move(nonceBuffer)});
    storage->asyncSetRow(SYS_BLOCK_NUMBER_2_NONCES, blockNumberStr, std::move(number2NonceEntry),
        [setRowCallback](auto&& error) {
            std::cout << __LINE__ << std::endl;
            setRowCallback(std::forward<decltype(error)>(error));
        });

    // current number
    Entry numberEntry;
    numberEntry.importFields({blockNumberStr});
    storage->asyncSetRow(SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER, std::move(numberEntry),
        [setRowCallback](auto&& error) {
            std::cout << __LINE__ << std::endl;
            setRowCallback(std::forward<decltype(error)>(error));
        });

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
    else if (header->number() > 0)
    {
        LEDGER_LOG(WARNING) << "Empty transactions and metadata, empty block?"
                            << LOG_KV("blockNumber", blockNumberStr);
    }

    if (blockVersion >= uint32_t(bcos::protocol::BlockVersion::V3_5_VERSION))
    {
        if (header->number() == SYS_CONTRACT_DEPLOY_NUMBER)
        {
            bytes blockBuffer;
            block->encode(blockBuffer);
            Entry blockEntry;
            blockEntry.importFields({std::move(blockBuffer)});
            m_storage->asyncSetRow(SYS_NUMBER_2_BLOCK_TXS,
                boost::lexical_cast<std::string>(header->number()), std::move(blockEntry),
                [setRowCallback](auto&& error) {
                    std::cout << __LINE__ << std::endl;
                    setRowCallback(std::forward<decltype(error)>(error));
                });
        }
        else
        {
            auto start = utcTime();
            asyncPreStoreBlockTxs(_blockTxs, block, [setRowCallback](auto&& error) {
                std::cout << __LINE__ << std::endl;
                setRowCallback(std::forward<decltype(error)>(error));
            });
            auto writeTxsTime = utcTime() - start;
            LEDGER_LOG(INFO) << LOG_DESC("asyncPrewriteBlock")
                             << LOG_KV("number", block->blockHeaderConst()->number())
                             << LOG_KV("writeTxsAndReceipts", writeTxsAndReceipts)
                             << LOG_KV("writeTxsTime(ms)", writeTxsTime);
        }
    }
    else
    {
        bytes transactionsBuffer;
        transactionsBlock->encode(transactionsBuffer);

        Entry number2TransactionHashesEntry;
        number2TransactionHashesEntry.importFields({std::move(transactionsBuffer)});
        storage->asyncSetRow(SYS_NUMBER_2_TXS, blockNumberStr,
            std::move(number2TransactionHashesEntry), [setRowCallback](auto&& error) {
                std::cout << __LINE__ << std::endl;
                setRowCallback(std::forward<decltype(error)>(error));
            });
    }

    std::atomic_int64_t totalCount = 0;
    std::atomic_int64_t failedCount = 0;
    if (writeTxsAndReceipts)
    {
        // hash 2 receipts
        std::vector<std::string> txsHash(block->receiptsSize());
        std::vector<bytes> receipts(block->receiptsSize());
        std::vector<std::string_view> receiptsView(block->receiptsSize());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, block->receiptsSize(), 256),
            [&transactionsBlock, &block, &failedCount, &totalCount, &txsHash, &receipts,
                &receiptsView](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i < range.end(); ++i)
                {
                    auto hash = transactionsBlock->transactionHash(i);
                    txsHash[i] = std::string((char*)hash.data(), hash.size());
                    auto receipt = block->receipt(i);
                    if (receipt->status() != 0)
                    {
                        failedCount++;
                    }
                    totalCount++;
                    receipt->encode(receipts[i]);
                    receiptsView[i] = bcos::concepts::bytebuffer::toView(receipts[i]);
                }
            });

        auto start = utcTime();
        auto error = m_storage->setRows(
            SYS_HASH_2_RECEIPT, std::move(txsHash), std::move(receiptsView));  // only for tikv
        auto writeReceiptsTime = utcTime() - start;
        if (error)
        {
            LEDGER_LOG(ERROR) << LOG_DESC("ledger write receipts failed")
                              << LOG_KV("message", error->errorMessage());
        }

        // if (blockVersion < uint32_t(bcos::protocol::BlockVersion::V3_5_VERSION))
        // {
            start = utcTime();
            asyncPreStoreBlockTxs(_blockTxs, block, [setRowCallback](auto&& error) {
                std::cout << __LINE__ << std::endl;
                setRowCallback(std::forward<decltype(error)>(error));
            });
            auto writeTxsTime = utcTime() - start;
            LEDGER_LOG(INFO) << LOG_DESC("asyncPrewriteBlock")
                             << LOG_KV("number", block->blockHeaderConst()->number())
                             << LOG_KV("writeReceiptsTime(ms)", writeReceiptsTime)
                             << LOG_KV("writeTxsTime(ms)", writeTxsTime);
        // }
    }
    else
    {
        for (size_t i = 0; i < block->receiptsSize(); ++i)
        {
            auto receipt = block->receipt(i);
            if (receipt->status() != 0)
            {
                failedCount++;
            }
            totalCount++;
        }
    }

    LEDGER_LOG(DEBUG) << LOG_DESC("Calculate tx counts in block")
                      << LOG_KV("number", blockNumberStr) << LOG_KV("totalCount", totalCount)
                      << LOG_KV("failedCount", failedCount);

    // total transaction count
    asyncGetTotalTransactionCount(
        [storage, block, &setRowCallback, &totalCount, &failedCount](
            Error::Ptr error, int64_t total, int64_t failed, bcos::protocol::BlockNumber) {
            if (error)
            {
                std::cout << __LINE__ << std::endl;
                setRowCallback(std::make_unique<Error>(*error), 2);
                return;
            }
            auto totalTxsCount = total + totalCount;
            Entry totalEntry;
            totalEntry.importFields({boost::lexical_cast<std::string>(totalTxsCount)});
            storage->asyncSetRow(SYS_CURRENT_STATE, SYS_KEY_TOTAL_TRANSACTION_COUNT,
                std::move(totalEntry), [setRowCallback](auto&& error) {
                    std::cout << __LINE__ << std::endl;
                    setRowCallback(std::forward<decltype(error)>(error));
                });
            auto failedTxs = failed + failedCount;
            if (failedCount != 0)
            {
                Entry failedEntry;
                failedEntry.importFields({boost::lexical_cast<std::string>(failedTxs)});
                storage->asyncSetRow(SYS_CURRENT_STATE, SYS_KEY_TOTAL_FAILED_TRANSACTION,
                    std::move(failedEntry), [setRowCallback](auto&& error) {
                        std::cout << __LINE__ << std::endl;
                        setRowCallback(std::forward<decltype(error)>(error));
                    });
            }
            else
            {
                std::cout << __LINE__ << std::endl;
                setRowCallback({}, true);
            }
            LEDGER_LOG(INFO) << METRIC << LOG_DESC("asyncPrewriteBlock")
                             << LOG_KV("number", block->blockHeaderConst()->number())
                             << LOG_KV("totalTxs", totalTxsCount) << LOG_KV("failedTxs", failedTxs)
                             << LOG_KV("incTxs", totalCount) << LOG_KV("incFailedTxs", failedCount);
        });
}

std::tuple<bool, bcos::crypto::HashListPtr, std::shared_ptr<std::vector<bytesConstPtr>>>
Ledger::needStoreUnsavedTxs(
    bcos::protocol::TransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr _block)
{
    // Note: in the case of block-sync, no-need to save transactions when prewriteBlock
    if (!_blockTxs || _blockTxs->size() == 0)
    {
        LEDGER_LOG(INFO) << LOG_DESC("asyncPreStoreBlockTxs: needStoreUnsavedTxs: empty txs")
                         << LOG_KV("number", (_block ? _block->blockHeaderConst()->number() : -1));
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
        bcos::bytes encodeData;
        tx->encode(encodeData);
        unstoredTxs++;
        txsHash->emplace_back(tx->hash());
        txsToStore->emplace_back(std::make_shared<bytes>(std::move(encodeData)));
    }
    LEDGER_LOG(INFO) << LOG_DESC("asyncPreStoreBlockTxs: needStoreUnsavedTxs")
                     << LOG_KV("txsSize", _blockTxs->size()) << LOG_KV("unstoredTxs", unstoredTxs)
                     << LOG_KV("number", (_block ? _block->blockHeaderConst()->number() : -1));
    if (txsToStore->size() == 0)
    {
        return std::make_tuple(false, nullptr, nullptr);
    }
    return std::make_tuple(true, txsHash, txsToStore);
}

bcos::Error::Ptr Ledger::storeTransactionsAndReceipts(
    bcos::protocol::TransactionsPtr blockTxs, bcos::protocol::Block::ConstPtr block)
{
    // node commit synced block will give empty blockTxs, the block will never be null
    if (!block)
    {
        return BCOS_ERROR_PTR(LedgerError::ErrorArgument, "empty block");
    }
    auto start = utcTime();
    bcos::Error::Ptr error = nullptr;
    auto txSize = std::max(block->transactionsSize(), block->transactionsMetaDataSize());

    std::vector<std::string> txsHash(txSize);
    std::vector<bytes> receipts(txSize);
    std::vector<std::string_view> receiptsView(txSize);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, txSize, 256),
        [&blockTxs, &block, &txsHash, &receipts, &receiptsView](
            const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i < range.end(); ++i)
            {
                auto hash = blockTxs ? blockTxs->at(i)->hash() : block->transaction(i)->hash();
                txsHash[i] = std::string((char*)hash.data(), hash.size());
                block->receipt(i)->encode(receipts[i]);
                receiptsView[i] = std::string_view((char*)receipts[i].data(), receipts[i].size());
            }
        });
    auto promise = std::make_shared<std::promise<bcos::Error::Ptr>>();
    m_threadPool->enqueue([storage = m_storage, promise, keys = std::move(txsHash),
                              values = std::move(receiptsView)]() mutable {
        auto err = storage->setRows(SYS_HASH_2_RECEIPT, std::move(keys), std::move(values));
        promise->set_value(err);
    });
    auto txsToStore = std::make_shared<std::vector<bytes>>();
    txsToStore->reserve(txSize);
    auto txsToStoreHash = std::make_shared<HashList>();
    txsToStoreHash->reserve(txSize);
    std::vector<std::string_view> keys;
    keys.reserve(txSize);
    // std::vector<Entry> entryValues;
    // entryValues.reserve(txSize);
    auto blockNumber = block->blockHeaderConst()->number();
    auto blockVersion = block->blockHeaderConst()->version();

    RecursiveGuard guard(m_mutex);
    size_t unstoredTxs = 0;
    // TODO: usr block level flag to indicate whether the transactions has been stored
    if (blockVersion >= (uint32_t)BlockVersion::V3_5_VERSION)
    {
        
        std::vector<std::string> values;
        values.reserve(txSize);
        auto transactionsBlock = m_blockFactory->createBlock();
        auto blockNumberStr = boost::lexical_cast<std::string>(blockNumber);
        Entry emptyEntry;
        for (size_t i = 0; i < txSize; i++)
        {
            auto tx = blockTxs ? blockTxs->at(i) : block->transaction(i);
            if (blockTxs && tx->storeToBackend())
            {
                // entryValues.push_back(emptyEntry);
                continue;
            }

            bcos::bytes encodeData;
            tx->encode(encodeData);
            bcos::protocol::Transaction::Ptr txData;
            if (blockNumber == SYS_CONTRACT_DEPLOY_NUMBER)
            {
                txData = m_blockFactory->transactionFactory()->createTransaction(
                    bcos::ref(encodeData), false, false);
            }
            else
            {
                txData =
                    m_blockFactory->transactionFactory()->createTransaction(bcos::ref(encodeData));
            }
            transactionsBlock->appendTransaction(txData);

            txsToStoreHash->emplace_back(tx->hash());
            keys.push_back(concepts::bytebuffer::toView((*txsToStoreHash)[unstoredTxs]));

            auto txIndex = boost::lexical_cast<std::string>(unstoredTxs);
            auto valueStr = blockNumberStr + "+" + txIndex;
            // std::string valueStr = blockNumberStr.append("+").append(txIndex);
            values.push_back(valueStr);

            LEDGER_LOG(INFO) << LOG_DESC("storeTransactionsAndReceipts SYS_TXHASH_2_NUMBER")
                             << LOG_KV("blockNumber", blockNumberStr)
                             << LOG_KV("key", toHex(keys[unstoredTxs]))
                             << LOG_KV("value", values.back())
                             << LOG_KV("key size", keys.size())
                             << LOG_KV("values size", values.size());

            // Entry entry;
            // entry.setObject(std::vector<std::string>{
            //     blockNumberStr,
            //     txIndex,
            // });
            // entryValues.push_back(std::move(entry));

            ++unstoredTxs;
        }

        if (!keys.empty())
        {
            bytes transactionsBuffer;
            transactionsBlock->encode(transactionsBuffer);

            Entry number2BlockTxsEntry;
            number2BlockTxsEntry.importFields({std::move(transactionsBuffer)});
            m_storage->asyncSetRow(SYS_NUMBER_2_BLOCK_TXS, std::to_string(blockNumber),
                std::move(number2BlockTxsEntry), [blockNumber](bcos::Error::Ptr _error) {
                    if (_error != nullptr)
                    {
                        LEDGER_LOG(ERROR)
                            << LOG_DESC("ledger write transactions to block failed")
                            << LOG_KV("number", blockNumber) << LOG_KV("code", _error->errorCode())
                            << LOG_KV("message", _error->errorMessage());
                        return;
                    }
                });

            // asyncPreStoreBlockTxs also write txs to DB, needStoreUnsavedTxs is out of lock, so
            // the transactions may be write twice
            // for (size_t j = 0; j < txSize; ++j)
            // {
            //     if (keys[j].empty())
            //     {
            //         continue;
            //     }
            //     m_storage->asyncSetRow(SYS_TXHASH_2_NUMBER, keys[j], std::move(entryValues[j]),
            //         [blockNumber](bcos::Error::Ptr _error) {
            //             if (_error != nullptr)
            //             {
            //                 LEDGER_LOG(ERROR)
            //                     << LOG_DESC("ledger write transactions to block failed")
            //                     << LOG_KV("number", blockNumber)
            //                     << LOG_KV("code", _error->errorCode())
            //                     << LOG_KV("message", _error->errorMessage());
            //                 return;
            //             }
            //         });
            // }

            error = m_storage->setRows(SYS_TXHASH_2_NUMBER, std::move(keys), std::move(values));
            if (error)
            {
                LEDGER_LOG(ERROR) << LOG_DESC("ledger write transactions failed")
                                  << LOG_KV("code", error->errorCode())
                                  << LOG_KV("message", error->errorMessage());
                return error;
            }
            // set the flag when store success
            if (blockTxs)
            {
                for (size_t i = 0; i < block->transactionsSize(); i++)
                {
                    blockTxs->at(i)->setStoreToBackend(true);
                }
            }
            // set the flag when store success
            if (blockTxs)
            {
                for (size_t i = 0; i < block->transactionsSize(); i++)
                {
                    blockTxs->at(i)->setStoreToBackend(true);
                }
            }
        }
    }
    else
    {
        
        std::vector<std::string_view> values;
        values.reserve(txSize);
        for (size_t i = 0; i < txSize; i++)
        {
            auto tx = blockTxs ? blockTxs->at(i) : block->transaction(i);
            if (blockTxs && tx->storeToBackend())
            {
                continue;
            }
            bcos::bytes encodeData;
            tx->encode(encodeData);
            txsToStoreHash->emplace_back(tx->hash());
            txsToStore->emplace_back(std::move(encodeData));
            keys.push_back(bcos::concepts::bytebuffer::toView((*txsToStoreHash)[unstoredTxs]));
            values.push_back(bcos::concepts::bytebuffer::toView((*txsToStore)[unstoredTxs]));
            ++unstoredTxs;
        }
        if (!keys.empty())
        {
            // asyncPreStoreBlockTxs also write txs to DB, needStoreUnsavedTxs is out of lock, so
            // the transactions may be write twice
            error = m_storage->setRows(SYS_HASH_2_TX, std::move(keys), std::move(values));
            if (error)
            {
                LEDGER_LOG(ERROR) << LOG_DESC("ledger write transactions failed")
                                  << LOG_KV("code", error->errorCode())
                                  << LOG_KV("message", error->errorMessage());
                return error;
            }
            // set the flag when store success
            if (blockTxs)
            {
                for (size_t i = 0; i < block->transactionsSize(); i++)
                {
                    blockTxs->at(i)->setStoreToBackend(true);
                }
            }
        }
    }

    auto err = promise->get_future().get();
    if (err)
    {
        LEDGER_LOG(ERROR) << LOG_DESC("ledger write receipts failed")
                          << LOG_KV("code", err->errorCode())
                          << LOG_KV("message", err->errorMessage());
        return err;
    }
    auto writeReceiptsTime = utcTime();

    LEDGER_LOG(INFO) << LOG_DESC("storeTransactionsAndReceipts finished")
                     << LOG_KV("blockNumber", blockNumber) << LOG_KV("blockTxsSize", txSize)
                     << LOG_KV("unStoredTxs", unstoredTxs)
                     << LOG_KV("timeCost", (utcTime() - start));
    return nullptr;
}

void Ledger::asyncGetBlockDataByNumber(bcos::protocol::BlockNumber _blockNumber, int32_t _blockFlag,
    std::function<void(Error::Ptr, bcos::protocol::Block::Ptr)> _onGetBlock)
{
    LEDGER_LOG(TRACE) << "GetBlockDataByNumber request" << LOG_KV("blockNumber", _blockNumber)
                      << LOG_KV("blockFlag", _blockFlag);
    if (_blockNumber < 0 || _blockFlag < 0)
    {
        LEDGER_LOG(INFO) << "GetBlockDataByNumber, wrong argument";
        _onGetBlock(BCOS_ERROR_PTR(LedgerError::ErrorArgument, "Wrong argument"), nullptr);
        return;
    }
    if (((_blockFlag & TRANSACTIONS) != 0) && ((_blockFlag & TRANSACTIONS_HASH) != 0))
    {
        LEDGER_LOG(INFO) << "GetBlockDataByNumber, wrong argument, transaction already has hash";
        _onGetBlock(BCOS_ERROR_PTR(LedgerError::ErrorArgument, "Wrong argument"), nullptr);
        return;
    }

    std::list<std::function<void()>> fetchers;
    auto block = m_blockFactory->createBlock();
    auto total = std::make_shared<size_t>(0);
    auto result = std::make_shared<std::tuple<std::atomic<size_t>, std::atomic<size_t>>>(0, 0);

    auto finally = [_blockNumber, total, result, block, _onGetBlock](Error::Ptr&& error) {
        if (error)
        {
            ++std::get<1>(*result);
        }
        else
        {
            ++std::get<0>(*result);
        }

        if (std::get<0>(*result) + std::get<1>(*result) == *total)
        {
            // All finished
            if (std::get<0>(*result) != *total)
            {
                LEDGER_LOG(DEBUG) << "GetBlockDataByNumber request failed!"
                                  << LOG_KV("number", _blockNumber);
                _onGetBlock(BCOS_ERROR_PTR(LedgerError::CollectAsyncCallbackError,
                                "Get block failed with errors!"),
                    nullptr);
                return;
            }

            _onGetBlock(nullptr, std::move(block));
        }
    };

    if (_blockFlag & HEADER)
    {
        ++(*total);

        fetchers.push_back([this, _blockNumber, block, finally]() {
            asyncGetBlockHeader(block, _blockNumber,
                [block, finally](Error::Ptr&& error) { finally(std::move(error)); });
        });
    }
    if ((_blockFlag & TRANSACTIONS) != 0 || (_blockFlag & TRANSACTIONS_HASH) != 0)
    {
        ++(*total);
    }
    if ((_blockFlag & RECEIPTS) != 0)
    {
        ++(*total);
    }
    if (((_blockFlag & TRANSACTIONS) != 0) || ((_blockFlag & RECEIPTS) != 0) ||
        (_blockFlag & TRANSACTIONS_HASH) != 0)
    {
        fetchers.push_back([this, block, _blockNumber, finally, _blockFlag]() {
            if (block->blockHeader()->version() >= (uint32_t)BlockVersion::V3_5_VERSION)
            {
                m_storage->asyncOpenTable(
                    SYS_NUMBER_2_BLOCK_TXS, [this, block, _blockNumber, finally](
                                                auto&& error, std::optional<Table>&& table) {
                        auto validError = checkTableValid(
                            std::forward<decltype(error)>(error), table, SYS_NUMBER_2_BLOCK_TXS);
                        if (validError)
                        {
                            finally(std::move(validError));
                            return;
                        }

                        // int64_t blockNumberBig = boost::endian::native_to_big(_blockNumber);
                        // std::string blockNumberString((char*)&blockNumberBig, 8);
                        std::string blockNumberStr = boost::lexical_cast<std::string>(_blockNumber);

                        table->asyncGetRow(blockNumberStr, [this, block, finally](auto&& error,
                                                               std::optional<Entry>&& entry) {
                            if (error)
                            {
                                LEDGER_LOG(INFO)
                                    << "Batch get SYS_NUMBER_2_BLOCK_TXS transaction failed "
                                    << boost::diagnostic_information(*error);
                                finally(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                    "Batch get transaction failed ", *error));
                                return;
                            }
                            if (!entry)
                            {
                                LEDGER_LOG(INFO) << "Batch get SYS_NUMBER_2_BLOCK_TXS transaction "
                                                    "failed, entry is empty";
                                finally(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                    "Batch get transaction failed, entry is empty", *error));
                                return;
                            }
                            // auto _block = m_blockFactory->createBlock();

                            auto header = block->blockHeader();
                            bytes headerBuffer;
                            header->encode(headerBuffer);

                            auto blockHeader =
                                m_blockFactory->blockHeaderFactory()->createBlockHeader(
                                    headerBuffer);

                            std::string_view value = entry->getField(0);
                            block->decode(
                                bcos::bytesConstRef((bcos::byte*)value.data(), value.size()), true,
                                true);
                            block->setBlockHeader(blockHeader);

                            finally(nullptr);
                        });
                    });
            }
            else
            {
                asyncGetBlockTransactionHashes(
                    _blockNumber, [this, _blockFlag, block, finally](
                                      Error::Ptr&& error, std::vector<std::string>&& hashes) {
                        if (error)
                        {
                            // if flag has both TRANSACTIONS and RECEIPTS, then the finally need
                            // to be called twice, so has below if logic
                            if ((_blockFlag & TRANSACTIONS) != 0 ||
                                (_blockFlag & TRANSACTIONS_HASH) != 0)
                            {
                                finally(std::move(error));
                            }
                            if ((_blockFlag & RECEIPTS) != 0)
                            {
                                finally(std::move(error));
                            }
                            return;
                        }

                        LEDGER_LOG(TRACE)
                            << "Get transactions hash list success, size:" << hashes.size();

                        auto hashesPtr =
                            std::make_shared<std::vector<std::string>>(std::move(hashes));
                        if ((_blockFlag & TRANSACTIONS) != 0)
                        {
                            asyncBatchGetTransactions(hashesPtr,
                                [block, finally](Error::Ptr&& error,
                                    std::vector<protocol::Transaction::Ptr>&& transactions) {
                                    if (error)
                                    {
                                        LEDGER_LOG(DEBUG) << LOG_DESC(
                                                                 "asyncGetBlockDataByNumber batch "
                                                                 "getTransactions failed")
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
                        if ((_blockFlag & RECEIPTS) != 0)
                        {
                            asyncBatchGetReceipts(hashesPtr,
                                [block, finally](Error::Ptr&& error,
                                    std::vector<protocol::TransactionReceipt::Ptr>&& receipts) {
                                    for (auto& it : receipts)
                                    {
                                        block->appendReceipt(it);
                                    }
                                    finally(std::move(error));
                                });
                        }
                        if ((_blockFlag & TRANSACTIONS_HASH) != 0)
                        {
                            for (auto& hash : *hashesPtr)
                            {
                                auto txMeta = m_blockFactory->createTransactionMetaData();
                                txMeta->setHash(bcos::crypto::HashType(
                                    hash, bcos::crypto::HashType::StringDataType::FromBinary));
                                block->appendTransactionMetaData(std::move(txMeta));
                            }
                            finally(nullptr);
                        }
                    });
            }
        });
    }

    auto getBlockTime = utcTime();
    for (auto& it : fetchers)
    {
        it();
    }
    LEDGER_LOG(INFO) << LOG_DESC("+++++++getBlockByNumber finished+++++++++")
                     << LOG_KV("blockNumber", _blockNumber)
                     << LOG_KV("timeCost", (utcTime() - getBlockTime));
}

void Ledger::asyncGetBlockNumber(
    std::function<void(Error::Ptr, bcos::protocol::BlockNumber)> _onGetBlock)
{
    asyncGetSystemTableEntry(SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER,
        [callback = std::move(_onGetBlock)](
            Error::Ptr&& error, std::optional<bcos::storage::Entry>&& entry) {
            if (error)
            {
                LEDGER_LOG(DEBUG) << "GetBlockNumber failed"
                                  << boost::diagnostic_information(error);
                callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                             "Get block number storage failed", *error),
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
                LEDGER_LOG(INFO) << "Cast blockNumber failed, may be empty, set to default value -1"
                                 << LOG_KV("blockNumber str", entry->getField(0));
            }

            LEDGER_LOG(TRACE) << "GetBlockNumber success" << LOG_KV("blockNumber", blockNumber);
            callback(nullptr, blockNumber);
        });
}

void Ledger::asyncGetBlockHashByNumber(bcos::protocol::BlockNumber _blockNumber,
    std::function<void(Error::Ptr, bcos::crypto::HashType)> _onGetBlock)
{
    LEDGER_LOG(TRACE) << "GetBlockHashByNumber request" << LOG_KV("blockNumber", _blockNumber);
    if (_blockNumber < 0)
    {
        _onGetBlock(BCOS_ERROR_PTR(
                        LedgerError::ErrorArgument, "GetBlockHashByNumber error, wrong argument"),
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
                    LEDGER_LOG(DEBUG)
                        << "GetBlockHashByNumber failed" << boost::diagnostic_information(error);
                    callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                 "GetBlockHashByNumber failed", *error),
                        bcos::crypto::HashType());
                    return;
                }

                auto hashStr = entry->getField(0);
                bcos::crypto::HashType hash(
                    std::string(hashStr), bcos::crypto::HashType::FromBinary);

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
    auto key = _blockHash;
    LEDGER_LOG(TRACE) << "GetBlockNumberByHash request" << LOG_KV("hash", key.hex());

    asyncGetSystemTableEntry(SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(key),
        [callback = std::move(_onGetBlock)](
            Error::Ptr&& error, std::optional<bcos::storage::Entry>&& entry) {
            try
            {
                if (error)
                {
                    LEDGER_LOG(DEBUG)
                        << "GetBlockNumberByHash failed " << boost::diagnostic_information(*error);
                    callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                 "GetBlockNumberByHash failed ", *error),
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
                        << "Cast blockNumber failed, may be empty, set to default value -1"
                        << LOG_KV("blockNumber str", entry->getField(0));
                }
                callback(nullptr, blockNumber);
            }
            catch (std::exception& e)
            {
                LEDGER_LOG(INFO) << "GetBlockNumberByHash failed "
                                 << boost::diagnostic_information(e);
                callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                             "GetBlockNumberByHash failed ", std::move(e)),
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
        std::string hex(it.begin(), it.end());
        hexList->emplace_back(std::move(hex));
    }

    asyncBatchGetTransactions(
        hexList, [this, callback = std::move(_onGetTx), _txHashList, _withProof](
                     Error::Ptr&& error, std::vector<protocol::Transaction::Ptr>&& transactions) {
            if (error)
            {
                LEDGER_LOG(DEBUG) << "GetBatchTxsByHashList failed: " << error->errorMessage();
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
                        LEDGER_LOG(TRACE) << LOG_BADGE("GetBatchTxsByHashList success")
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
    auto key = _txHash;

    LEDGER_LOG(TRACE) << "GetTransactionReceiptByHash" << LOG_KV("hash", key);

    asyncGetSystemTableEntry(SYS_HASH_2_RECEIPT, bcos::concepts::bytebuffer::toView(key),
        [this, callback = std::move(_onGetTx), _withProof](
            Error::Ptr&& error, std::optional<bcos::storage::Entry>&& entry) {
            if (error)
            {
                LEDGER_LOG(DEBUG) << "GetTransactionReceiptByHash: "
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
                            LEDGER_LOG(DEBUG) << "GetTransactionReceiptByHash"
                                              << LOG_KV("code", _error->errorCode())
                                              << LOG_KV("msg", _error->errorMessage())
                                              << boost::diagnostic_information(_error);
                            _onGetTx(std::move(_error), receipt, nullptr);
                            return;
                        }

                        _onGetTx(nullptr, receipt, std::move(_proof));
                    });
            }
            else
            {
                callback(nullptr, receipt, nullptr);
            }
        });
}

void Ledger::asyncGetTotalTransactionCount(
    std::function<void(Error::Ptr, int64_t, int64_t, bcos::protocol::BlockNumber)> _callback)
{
    static std::string_view keys[] = {
        SYS_KEY_TOTAL_TRANSACTION_COUNT, SYS_KEY_TOTAL_FAILED_TRANSACTION, SYS_KEY_CURRENT_NUMBER};

    m_storage->asyncOpenTable(SYS_CURRENT_STATE,
        [this, callback = std::move(_callback)](auto&& error, std::optional<Table>&& table) {
            auto tableError =
                checkTableValid(std::forward<decltype(error)>(error), table, SYS_CURRENT_STATE);
            if (tableError)
            {
                LEDGER_LOG(DEBUG) << "GetTotalTransactionCount"
                                  << boost::diagnostic_information(*tableError);
                callback(std::move(tableError), -1, -1, -1);
                return;
            }

            table->asyncGetRows(keys, [callback = std::move(callback)](auto&& error,
                                          std::vector<std::optional<Entry>>&& entries) {
                if (error)
                {
                    LEDGER_LOG(DEBUG)
                        << "GetTotalTransactionCount" << boost::diagnostic_information(*error);
                    callback(BCOS_ERROR_WITH_PREV_PTR(
                                 LedgerError::GetStorageError, "Get row failed", *error),
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
                            << "GetTotalTransactionCount failed" << LOG_KV("index", i) << " empty";
                    }
                    else
                    {
                        try
                        {
                            value = boost::lexical_cast<int64_t>(entry->getField(0));
                        }
                        catch (boost::bad_lexical_cast& e)
                        {
                            LEDGER_LOG(WARNING)
                                << "Lexical cast transaction count failed, entry value: "
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

                LEDGER_LOG(TRACE) << "GetTotalTransactionCount success"
                                  << LOG_KV("totalCount", totalCount)
                                  << LOG_KV("failedCount", failedCount)
                                  << LOG_KV("blockNumber", blockNumber);
                callback(nullptr, totalCount, failedCount, blockNumber);
            });
        });
}

void Ledger::asyncGetSystemConfigByKey(const std::string_view& _key,
    std::function<void(Error::Ptr, std::string, bcos::protocol::BlockNumber)> _onGetConfig)
{
    LEDGER_LOG(TRACE) << "GetSystemConfigByKey request" << LOG_KV("key", _key);

    asyncGetBlockNumber([this, callback = std::move(_onGetConfig), _key](
                            Error::Ptr error, bcos::protocol::BlockNumber blockNumber) {
        if (error)
        {
            LEDGER_LOG(DEBUG) << "GetSystemConfigByKey, " << boost::diagnostic_information(*error);
            callback(std::move(error), "", -1);
            return;
        }

        asyncGetSystemTableEntry(SYS_CONFIG, _key,
            [blockNumber, callback = std::move(callback)](
                Error::Ptr&& error, std::optional<bcos::storage::Entry>&& entry) {
                try
                {
                    // Note: should considerate the case that the compatibility_version is not
                    // set
                    if (error)
                    {
                        LEDGER_LOG(DEBUG) << "GetSystemConfigByKey, " << error->errorMessage();
                        callback(std::move(error), "", -1);
                        return;
                    }

                    if (!entry)
                    {
                        LEDGER_LOG(DEBUG) << "asyncGetSystemTableEntry: entry doesn't exists";
                        callback(
                            BCOS_ERROR_PTR(-1, "asyncGetSystemTableEntry failed for empty entry"),
                            "", -1);
                        return;
                    }

                    LEDGER_LOG(TRACE) << "Entry value: " << toHex(entry->get());

                    auto [value, number] = entry->getObject<SystemConfigEntry>();

                    // The param was reset at height getLatestBlockNumber(), and takes effect in
                    // next block. So we query the status of getLatestBlockNumber() + 1.
                    auto effectNumber = blockNumber + 1;
                    if (number > effectNumber)
                    {
                        LEDGER_LOG(INFO) << "GetSystemConfigByKey, config not available"
                                         << LOG_KV("currentBlockNumber", effectNumber)
                                         << LOG_KV("available number", number);
                        callback(BCOS_ERROR_PTR(LedgerError::ErrorArgument, "Config not available"),
                            "", -1);
                        return;
                    }

                    LEDGER_LOG(TRACE) << "GetSystemConfigByKey success" << LOG_KV("value", value)
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
    LEDGER_LOG(TRACE) << "GetNonceList request" << LOG_KV("startNumber", _startNumber)
                      << LOG_KV("offset", _offset);

    if (_startNumber < 0 || _offset < 0)
    {
        LEDGER_LOG(ERROR) << "GetNonceList error arguments" << LOG_KV("startNumber", _startNumber)
                          << LOG_KV("offset", _offset);
        _onGetList(BCOS_ERROR_PTR(LedgerError::ErrorArgument, "Wrong argument"), nullptr);
        return;
    }

    m_storage->asyncOpenTable(SYS_BLOCK_NUMBER_2_NONCES, [this, callback = std::move(_onGetList),
                                                             _startNumber, _offset](auto&& error,
                                                             std::optional<Table>&& table) mutable {
        auto tableError =
            checkTableValid(std::forward<decltype(error)>(error), table, SYS_BLOCK_NUMBER_2_NONCES);
        if (tableError)
        {
            callback(std::move(tableError), nullptr);
            return;
        }

        auto numberRange = RANGES::views::iota(_startNumber, _startNumber + _offset + 1);
        auto numberList = numberRange | RANGES::views::transform([](BlockNumber blockNumber) {
            return boost::lexical_cast<std::string>(blockNumber);
        }) | RANGES::to<std::vector<std::string>>();

        table->asyncGetRows(numberList, [this, numberRange, callback = std::move(callback)](
                                            auto&& error,
                                            std::vector<std::optional<Entry>>&& entries) {
            if (error)
            {
                LEDGER_LOG(INFO) << "GetNonceList failed" << boost::diagnostic_information(*error);
                callback(
                    BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError, "GetNonceList", *error),
                    nullptr);
                return;
            }

            auto retMap =
                std::make_shared<std::map<protocol::BlockNumber, protocol::NonceListPtr>>();

            for (auto const& [number, entry] : RANGES::views::zip(numberRange, entries))
            {
                try
                {
                    if (!entry)
                    {
                        continue;
                    }

                    auto value = entry->getField(0);
                    auto block = m_blockFactory->createBlock(
                        bcos::bytesConstRef((bcos::byte*)value.data(), value.size()), false, false);

                    retMap->emplace(std::make_pair(number,
                        std::make_shared<NonceList>(block->nonceList() | RANGES::to<NonceList>())));
                }
                catch (std::exception const& e)
                {
                    LEDGER_LOG(WARNING)
                        << "Parse nonce list failed" << boost::diagnostic_information(e);
                    continue;
                }
            }

            LEDGER_LOG(TRACE) << "GetNonceList success" << LOG_KV("retMap size", retMap->size());
            callback(nullptr, std::move(retMap));
        });
    });
}

void Ledger::asyncGetNodeListByType(const std::string_view& _type,
    std::function<void(Error::Ptr, consensus::ConsensusNodeListPtr)> _onGetConfig)
{
    LEDGER_LOG(DEBUG) << "GetNodeListByType request" << LOG_KV("type", _type);

    asyncGetBlockNumber([this, type = std::move(_type), callback = std::move(_onGetConfig)](
                            Error::Ptr&& error, bcos::protocol::BlockNumber blockNumber) {
        if (error)
        {
            LEDGER_LOG(DEBUG) << "GetNodeListByType" << boost::diagnostic_information(*error);
            callback(BCOS_ERROR_WITH_PREV_PTR(
                         LedgerError::GetStorageError, "GetNodeListByType failed", *error),
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
                    if (it.type == type && boost::lexical_cast<bcos::protocol::BlockNumber>(
                                               it.enableNumber) <= effectNumber)
                    {
                        crypto::NodeIDPtr nodeID =
                            m_blockFactory->cryptoSuite()->keyFactory()->createKey(
                                fromHex(it.nodeID));
                        // Note: use try-catch to handle the exception case
                        nodes->emplace_back(std::make_shared<consensus::ConsensusNode>(
                            nodeID, it.weight.convert_to<uint64_t>()));
                    }
                }

                LEDGER_LOG(DEBUG) << "GetNodeListByType success" << LOG_KV("type", type)
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
        LEDGER_LOG(DEBUG) << ss.str() << boost::diagnostic_information(*error);

        return BCOS_ERROR_WITH_PREV_PTR(LedgerError::OpenTableFailed, ss.str(), *error);
    }

    if (!table)
    {
        std::stringstream ss;
        ss << "Table: " << tableName << " does not exists!";
        LEDGER_LOG(DEBUG) << ss.str();
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
        LEDGER_LOG(DEBUG) << ss.str() << boost::diagnostic_information(*error);

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
    auto block = m_blockFactory->createBlock();
    std::promise<Error::Ptr> promise;
    asyncGetBlockHeader(
        block, blockNumber, [&promise](auto&& error) { promise.set_value(std::move(error)); });
    auto error = promise.get_future().get();
    if (error)
    {
        callback(std::move(error), std::vector<std::string>());
        return;
    }
    if (block->blockHeaderConst()->version() >= (uint32_t)BlockVersion::V3_5_VERSION)
    {
        m_storage->asyncOpenTable(SYS_NUMBER_2_BLOCK_TXS,
            [this, block, blockNumber, callback](auto&& error, std::optional<Table>&& table) {
                auto validError = checkTableValid(
                    std::forward<decltype(error)>(error), table, SYS_NUMBER_2_BLOCK_TXS);
                if (validError)
                {
                    callback(std::move(error), std::vector<std::string>());
                    return;
                }
                std::string blockNumberStr = boost::lexical_cast<std::string>(blockNumber);

                table->asyncGetRow(blockNumberStr, [this, block, callback](
                                                       auto&& error, std::optional<Entry>&& entry) {
                    if (error)
                    {
                        LEDGER_LOG(INFO) << "Batch get SYS_NUMBER_2_BLOCK_TXS transaction failed "
                                         << boost::diagnostic_information(*error);
                        callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                     "Batch get transaction failed ", *error),
                            std::vector<std::string>());
                        return;
                    }
                    if (!entry)
                    {
                        LEDGER_LOG(INFO) << "Batch get SYS_NUMBER_2_BLOCK_TXS transaction "
                                            "failed, entry is empty";
                        callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                     "Batch get transaction failed, entry is empty", *error),
                            std::vector<std::string>());
                        return;
                    }

                    std::string_view value = entry->getField(0);
                    block->decode(
                        bcos::bytesConstRef((bcos::byte*)value.data(), value.size()), true, true);

                    std::vector<std::string> hashList(block->transactionsSize());
                    for (size_t i = 0; i < block->transactionsSize(); ++i)
                    {
                        auto hash = block->transaction(i)->hash();
                        hashList[i].assign(hash.begin(), hash.end());
                    }

                    callback(nullptr, std::move(hashList));
                });
            });
    }
    else
    {
        m_storage->asyncOpenTable(SYS_NUMBER_2_TXS, [this, blockNumber, callback](auto&& error,
                                                        std::optional<Table>&& table) {
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
                        hashList[i].assign(hash.begin(), hash.end());
                        // hashList[i] = hash.hex();
                    }

                    callback(nullptr, std::move(hashList));
                });
        });
    }
}

void Ledger::asyncGetTransactionByHash(
    std::string_view hash, std::function<void(Error::Ptr&&, protocol::Transaction::Ptr&&)> callback)
{
    // std::cout << "asyncGetTransactionByHash hash = " << toHex(std::string(hash)) << std::endl;
    m_storage->asyncOpenTable(SYS_TXHASH_2_NUMBER, [this, hash, callback](
                                                       auto&& error, std::optional<Table>&& table) {
        auto validError =
            checkTableValid(std::forward<decltype(error)>(error), table, SYS_TXHASH_2_NUMBER);
        if (validError)
        {
            callback(std::move(validError), nullptr);
            return;
        }

        table->asyncGetRow(hash, [this, hash, callback](
                                     auto&& error, std::optional<Entry>&& entry) {
            if (error)
            {
                LEDGER_LOG(INFO) << "Batch get transaction failed in SYS_TXHASH_2_NUMBER"
                                 << boost::diagnostic_information(*error);
                // callback(BCOS_ERROR_WITH_PREV_PTR(
                //              LedgerError::GetStorageError, "Batch get transaction failed ",
                //              *error),
                //     nullptr);
                return;
            }

            if (entry.has_value() == 0)
            {
                LEDGER_LOG(INFO) << "Get transaction failed in SYS_TXHASH_2_NUMBER: "
                                 << LOG_KV("txHash", toHex(hash));
                // callback(
                //     BCOS_ERROR_PTR(LedgerError::GetStorageError, "Batch get transaction failed"), nullptr);
                return;
            }

            // auto value = entry->getObject<std::vector<std::string>>();
            // auto blockNumber = boost::lexical_cast<int64_t>(value[0]);
            // auto txIndex = boost::lexical_cast<int64_t>(value[1]);
            auto hashStr = toHex(std::string(hash));
            std::cout << hashStr << std::endl;

            auto field = std::string(entry->getField(0));
            auto pos = field.find("+");
            if (pos == std::string::npos)
            {
                LEDGER_LOG(INFO) << LOG_DESC("getRow SYS_TXHASH_2_NUMBER")
                                << LOG_KV("txHash", hashStr)
                                << LOG_DESC(", get wrong");
                return;
            }
            auto blockNumber = boost::lexical_cast<int64_t>(field.substr(0, pos));
            auto txIndex = boost::lexical_cast<int64_t>(field.substr(pos + 1));

            m_storage->asyncOpenTable(SYS_NUMBER_2_BLOCK_TXS, [this, blockNumber, txIndex,
                                                                  callback](auto&& error,
                                                                  std::optional<Table>&& table) {
                if (error)
                {
                    LEDGER_LOG(INFO)
                        << "Batch number get block transactions failed in SYS_NUMBER_2_BLOCK_TXS "
                        << boost::diagnostic_information(*error);
                    callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                 "Batch number get block transactions failed ", *error),
                        nullptr);
                    return;
                }
                table->asyncGetRow(std::to_string(blockNumber), [this, blockNumber, txIndex,
                                                                    callback](auto&& error,
                                                                    std::optional<Entry>&& entry) {
                    if (error)
                    {
                        LEDGER_LOG(INFO)
                            << "Get transaction by hash failed in SYS_NUMBER_2_BLOCK_TXS "
                            << boost::diagnostic_information(*error);
                        callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                     "Get transaction by hash failed ", *error),
                            nullptr);
                        return;
                    }

                    if (!entry.has_value())
                    {
                        LEDGER_LOG(INFO)
                            << "Get block failed: " << LOG_KV("blockNumber", blockNumber);
                        callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                     "Get transaction by hash failed ", *error),
                            nullptr);
                        return;
                    }
                    else
                    {
                        auto block = m_blockFactory->createBlock();
                        std::string_view value = entry->getField(0);
                        block->decode(bcos::bytesConstRef((bcos::byte*)value.data(), value.size()),
                            true, true);
                        auto blockTransaction = block->getTransaction(txIndex);
                        bytes transactionsBuffer;
                        blockTransaction->encode(transactionsBuffer);

                        auto transaction = m_blockFactory->transactionFactory()->createTransaction(
                            bcos::ref(transactionsBuffer));

                        callback(nullptr, std::move(transaction));
                    }
                });
            });
        });
    });
}

void Ledger::asyncBatchGetTransactions(std::shared_ptr<std::vector<std::string>> hashes,
    std::function<void(Error::Ptr&&, std::vector<protocol::Transaction::Ptr>&&)> callback)
{
    LEDGER_LOG(DEBUG) << "asyncBatchGetTransactions ";

    std::vector<std::string_view> hashesView;
    hashesView.reserve(hashes->size());
    for (auto& hash : *hashes)
    {
        hashesView.push_back(hash);
    }

    auto promise = std::make_shared<std::promise<bool>>();

    std::vector<protocol::Transaction::Ptr> transactions;
    transactions.reserve(hashes->size());

    size_t count = 0;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, hashesView.size()),
        [this, &transactions, &hashesView, &count, promise, callback](
            const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i < range.end(); ++i)
            {
                size_t num = range.end() - 1;
                auto hashView = hashesView[i];
                asyncGetTransactionByHash(hashesView[i],
                    [this, i, &count, &transactions, &hashView, promise, num, callback](
                        auto&& error, protocol::Transaction::Ptr transaction) {
                        if (error)
                        {
                            LEDGER_LOG(INFO)
                                << "Batch get transaction failed in SYS_NUMBER_2_BLOCK_TXS"
                                << boost::diagnostic_information(*error);
                            // callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                            //              "Batch get transaction failed ", *error),
                            //     std::vector<protocol::Transaction::Ptr>());
                            // return;
                        }

                        // size_t size = transactions.size();
                        if (transaction != nullptr)
                        {
                            transactions.push_back(std::move(transaction));
                            ++count;
                            if (i == num)
                            {
                                promise->set_value(true);
                            }
                            return;
                        }

                        m_storage->asyncOpenTable(SYS_HASH_2_TX, [this, i, &count, &transactions,
                                                                     hashView, promise, num,
                                                                     callback](auto&& error,
                                                                     std::optional<Table>&& table) {
                            auto validError = checkTableValid(
                                std::forward<decltype(error)>(error), table, SYS_HASH_2_TX);
                            if (validError)
                            {
                                callback(std::move(validError),
                                    std::vector<protocol::Transaction::Ptr>());
                                return;
                            }

                            table->asyncGetRow(hashView, [this, i, &transactions, hashView, &count,
                                                             promise, num, callback](auto&& error,
                                                             std::optional<Entry>&& entry) {
                                LEDGER_LOG(INFO) << "asyncBatchGetTransactions SYS_HASH_2_TX";
                                if (error)
                                {
                                    LEDGER_LOG(INFO) << "Batch get transaction failed "
                                                     << boost::diagnostic_information(*error);
                                    callback(BCOS_ERROR_WITH_PREV_PTR(LedgerError::GetStorageError,
                                                 "Batch get transaction failed ", *error),
                                        std::vector<protocol::Transaction::Ptr>());

                                    return;
                                }

                                if (!entry.has_value())
                                {
                                    LEDGER_LOG(INFO)
                                        << "Get transaction failed: " << LOG_KV("txHash", toHex(hashView));
                                }
                                else
                                {
                                    auto field = entry->getField(0);
                                    auto transaction =
                                        m_blockFactory->transactionFactory()->createTransaction(
                                            bcos::bytesConstRef(
                                                (bcos::byte*)field.data(), field.size()));
                                    transactions.push_back(std::move(transaction));
                                    ++count;
                                    if (i == num)
                                    {
                                        promise->set_value(true);
                                    }
                                }
                            });
                        });
                    });
            }
        });

    promise->get_future().wait();

    if (count != hashes->size())
    {
        LEDGER_LOG(INFO) << "Batch get transaction failed, transactions size not match hashesSize"
                         << LOG_KV("txsSize", transactions.size())
                         << LOG_KV("hashesSize", hashes->size());
        callback(BCOS_ERROR_PTR(LedgerError::CollectAsyncCallbackError,
                     "Batch get transaction failed, transactions size not match "
                     "hashesSize, txsSize: " +
                         std::to_string(transactions.size()) +
                         ", hashesSize: " + std::to_string(hashes->size())),
            std::move(transactions));
        return;
    }

    callback(nullptr, std::move(transactions));
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
                    LEDGER_LOG(DEBUG)
                        << "Batch get receipt failed!" << boost::diagnostic_information(*error);
                    callback(BCOS_ERROR_WITH_PREV_PTR(
                                 LedgerError::GetStorageError, "Batch get receipt failed!", *error),
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
                        LEDGER_LOG(DEBUG) << "Get receipt with empty entry: " << (*hashes)[i];
                        callback(BCOS_ERROR_PTR(
                                     LedgerError::GetStorageError, "Batch get transaction failed"),
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
            auto entryError = checkEntryValid(std::forward<decltype(error)>(error), entry, key);
            if (entryError)
            {
                callback(std::move(entryError), {});
                return;
            }

            callback(nullptr, std::move(entry));
        });
    });
}

template <typename MerkleType, typename HashRangeType>
static std::vector<h256> getMerkleTreeFromCache(int64_t blockNumber, Ledger::CacheType& cache,
    std::mutex& mutex, const std::string& cacheName, const MerkleType& merkle,
    const HashRangeType& hashesRange)
{
    std::shared_ptr<std::vector<h256>> merkleTree;
    {
        UniqueGuard l(mutex);
        if (!cache.contains(blockNumber))
        {
            merkleTree = std::make_shared<std::vector<h256>>();
            merkle.template generateMerkle(hashesRange, *merkleTree);
            cache.insert(blockNumber, merkleTree);
            LEDGER_LOG(DEBUG)
                << LOG_BADGE(cacheName)
                << LOG_DESC("Failed to hit the cache and build a new Merkel tree from scratch")
                << LOG_KV("blockNumber", blockNumber);
        }
        else
        {
            merkleTree = *(cache.get(blockNumber));
            LEDGER_LOG(DEBUG) << LOG_BADGE(cacheName) << LOG_DESC("Hit cache")
                              << LOG_KV("blockNumber", blockNumber);
        }
    }
    return *merkleTree;
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
                LEDGER_LOG(DEBUG) << LOG_BADGE("getTxProof")
                                  << LOG_DESC("getReceiptByTxHash from storage failed")
                                  << LOG_KV("txHash", _txHash.hex());
                _onGetProof(std::forward<decltype(_error)>(_error), nullptr);
                return;
            }
            auto blockNumber = _receipt->blockNumber();
            asyncGetBlockTransactionHashes(
                blockNumber, [this, _onGetProof, _txHash = std::move(_txHash), blockNumber](
                                 Error::Ptr&& _error, std::vector<std::string>&& _hashList) {
                    if (_error || _hashList.empty())
                    {
                        LEDGER_LOG(DEBUG)
                            << LOG_BADGE("getTxProof")
                            << LOG_DESC("asyncGetBlockTransactionHashes from storage failed")
                            << LOG_KV("txHash", _txHash.hex());
                        _onGetProof(std::forward<decltype(_error)>(_error), nullptr);
                        return;
                    }
                    asyncBatchGetTransactions(std::make_shared<std::vector<std::string>>(_hashList),
                        [this, cryptoSuite = m_blockFactory->cryptoSuite(), _onGetProof,
                            _txHash = std::move(_txHash), blockNumber](
                            Error::Ptr&& _error, std::vector<Transaction::Ptr>&& _txList) {
                            if (_error || _txList.empty())
                            {
                                LEDGER_LOG(DEBUG)
                                    << LOG_BADGE("getTxProof") << LOG_DESC("getTxs callback failed")
                                    << LOG_KV("code", _error->errorCode())
                                    << LOG_KV("msg", _error->errorMessage());
                                _onGetProof(std::forward<decltype(_error)>(_error), nullptr);
                                return;
                            }
                            auto merkleProofPtr = std::make_shared<MerkleProof>();
                            bcos::crypto::merkle::Merkle merkle(cryptoSuite->hashImpl()->hasher());
                            auto hashesRange =
                                _txList |
                                RANGES::views::transform([](const Transaction::Ptr& transaction) {
                                    return transaction->hash();
                                });

                            auto merkleTree =
                                getMerkleTreeFromCache(blockNumber, m_txProofMerkleCache,
                                    m_txMerkleMtx, "getTxProof", merkle, hashesRange);
                            merkle.template generateMerkleProof(
                                hashesRange, merkleTree, _txHash, *merkleProofPtr);

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
    auto blockNumber = _receipt->blockNumber();
    asyncGetBlockTransactionHashes(blockNumber,
        [this, _onGetProof = std::move(_onGetProof), receiptHash = _receipt->hash(), blockNumber](
            Error::Ptr&& _error, std::vector<std::string>&& _hashList) {
            if (_error)
            {
                _onGetProof(std::forward<decltype(_error)>(_error), nullptr);
                return;
            }

            asyncBatchGetReceipts(std::make_shared<std::vector<std::string>>(_hashList),
                [this, cryptoSuite = this->m_blockFactory->cryptoSuite(), _onGetProof,
                    receiptHash = receiptHash, blockNumber](Error::Ptr&& _error,
                    std::vector<protocol::TransactionReceipt::Ptr>&& _receiptList) {
                    if (_error || _receiptList.empty())
                    {
                        LEDGER_LOG(DEBUG) << LOG_BADGE("getReceiptProof")
                                          << LOG_DESC("asyncBatchGetReceipts callback failed");
                        _onGetProof(std::forward<decltype(_error)>(_error), nullptr);
                        return;
                    }
                    auto merkleProofPtr = std::make_shared<MerkleProof>();
                    bcos::crypto::merkle::Merkle merkle(cryptoSuite->hashImpl()->hasher());
                    auto hashesRange =
                        _receiptList |
                        RANGES::views::transform(
                            [](const TransactionReceipt::Ptr& receipt) { return receipt->hash(); });

                    auto merkleTree = getMerkleTreeFromCache(blockNumber, m_receiptProofMerkleCache,
                        m_receiptMerkleMtx, "getReceiptProof", merkle, hashesRange);

                    merkle.template generateMerkleProof(
                        hashesRange, merkleTree, receiptHash, *merkleProofPtr);

                    LEDGER_LOG(TRACE)
                        << LOG_BADGE("getReceiptProof") << LOG_DESC("get merkle proof success")
                        << LOG_KV("receiptHash", receiptHash.hex());
                    _onGetProof(nullptr, std::move(merkleProofPtr));
                });
        });
}

// sync method
bool Ledger::buildGenesisBlock(LedgerConfig::Ptr _ledgerConfig, size_t _gasLimit,
    const std::string_view& _genesisData, std::string const& _compatibilityVersion,
    bool _isAuthCheck, std::string const& _consensusType, std::int64_t _epochSealerNum,
    std::int64_t _epochBlockNum)
{
    LEDGER_LOG(INFO) << LOG_DESC("[#buildGenesisBlock]");
    if (_gasLimit < TX_GAS_LIMIT_MIN)
    {
        LEDGER_LOG(FATAL) << LOG_BADGE("buildGenesisBlock")
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
        std::promise<protocol::BlockHeader::Ptr> blockHeaderFuture;
        // get genesisBlockHeader
        asyncGetBlockDataByNumber(
            0, HEADER, [&blockHeaderFuture](Error::Ptr error, Block::Ptr block) {
                if (error)
                {
                    LEDGER_LOG(INFO) << "Get genesisBlockHeader from storage failed";
                    blockHeaderFuture.set_value(nullptr);
                }
                else
                {
                    blockHeaderFuture.set_value(block->blockHeader());
                }
            });
        bcos::protocol::BlockHeader::Ptr m_genesisBlockHeader =
            blockHeaderFuture.get_future().get();
        auto initialGenesisData = m_genesisBlockHeader->extraData().toStringView();
        // check genesisData whether inconsistent with initialGenesisData
        if (initialGenesisData == _genesisData)
        {
            auto version = bcos::tool::toVersionNumber(_compatibilityVersion);
            if (version > (uint32_t)protocol::BlockVersion::MAX_VERSION ||
                version < (uint32_t)protocol::BlockVersion::MIN_VERSION)
            {
                BOOST_THROW_EXCEPTION(bcos::tool::InvalidVersion() << errinfo_comment(
                                          "Current genesis compatibilityVersion is " +
                                          _compatibilityVersion + ", No support this version"));
            }

            // Before return, make sure sharding flag is placed
            task::syncWait([&]() -> task::Task<void> {
                auto versionEntry = co_await storage2::readOne(
                    *m_storage, std::make_tuple(SYS_CONFIG, SYSTEM_KEY_COMPATIBILITY_VERSION));
                auto blockNumberEntry = co_await storage2::readOne(
                    *m_storage, std::make_tuple(SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER));
                if (versionEntry && blockNumberEntry)
                {
                    auto [versionStr, _] = versionEntry->getObject<SystemConfigEntry>();
                    auto storageVersion = bcos::tool::toVersionNumber(versionStr);

                    Features shardingFeature;
                    shardingFeature.setToShardingDefault((protocol::BlockVersion)storageVersion);
                    co_await shardingFeature.writeToStorage(
                        *m_storage, boost::lexical_cast<long>(blockNumberEntry->get()));
                }
            }());

            return true;
        }
        // GetBlockDataByNumber success but not consistent with initialGenesisData
        if (m_genesisBlockHeader)
        {
            std::cout << "The Genesis Data is inconsistent with the initial Genesis Data. "
                      << std::endl
                      << LOG_KV("initialGenesisData", initialGenesisData) << std::endl
                      << LOG_KV("genesisData", _genesisData) << std::endl;
            BOOST_THROW_EXCEPTION(
                bcos::tool::InvalidConfig() << errinfo_comment(
                    "The Genesis Data is inconsistent with the initial Genesis Data"));
        }
        else
        {
            LEDGER_LOG(INFO) << "failed, initialGenesisDate is null";
        }
    }

    auto versionNumber = bcos::tool::toVersionNumber(_compatibilityVersion);
    if (versionNumber > (uint32_t)protocol::BlockVersion::MAX_VERSION)
    {
        BOOST_THROW_EXCEPTION(bcos::tool::InvalidVersion() << errinfo_comment(
                                  "The genesis compatibilityVersion is " + _compatibilityVersion +
                                  ", high than support maxVersion"));
    }
    // clang-format off
    std::vector<std::string_view> tables {
        SYS_CONFIG, SYS_VALUE_AND_ENABLE_BLOCK_NUMBER,
        SYS_CONSENSUS, SYS_VALUE,
        SYS_CURRENT_STATE, SYS_VALUE,
        SYS_HASH_2_TX, SYS_VALUE,
        SYS_HASH_2_NUMBER, SYS_VALUE,
        SYS_NUMBER_2_HASH, SYS_VALUE,
        SYS_NUMBER_2_BLOCK_HEADER, SYS_VALUE,
        SYS_NUMBER_2_TXS, SYS_VALUE,
        SYS_HASH_2_RECEIPT, SYS_VALUE,
        SYS_BLOCK_NUMBER_2_NONCES, SYS_VALUE,
        SYS_TXHASH_2_NUMBER, SYS_VALUE,
        SYS_NUMBER_2_BLOCK_TXS, SYS_VALUE,
    };

    if (versionNumber >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
    {
        std::vector<std::string_view> moreTables{
            SYS_CODE_BINARY, SYS_VALUE,
            SYS_CONTRACT_ABI, SYS_VALUE
        };

        for (auto v : moreTables)
        {
            tables.push_back(v);
        }
    }
    // clang-format on

    size_t total = tables.size();

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


    createFileSystemTables(versionNumber);
    auto txLimit = _ledgerConfig->blockTxCountLimit();
    LEDGER_LOG(INFO) << LOG_DESC("Commit the genesis block") << LOG_KV("txLimit", txLimit)
                     << LOG_KV("leaderSwitchPeriod", _ledgerConfig->leaderSwitchPeriod())
                     << LOG_KV("blockTxCountLimit", _ledgerConfig->blockTxCountLimit())
                     << LOG_KV("compatibilityVersion", _compatibilityVersion)
                     << LOG_KV("minSupportedVersion", g_BCOSConfig.minSupportedVersion())
                     << LOG_KV("maxSupportedVersion", g_BCOSConfig.maxSupportedVersion())
                     << LOG_KV("isAuthCheck", _isAuthCheck);

    // build a block
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(0);
    if (versionNumber >= (uint32_t)protocol::BlockVersion::V3_1_VERSION)
    {
        header->setVersion(versionNumber);
    }
    header->setExtraData(bcos::bytes(_genesisData.begin(), _genesisData.end()));
    header->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());

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

    // Write default features
    Features features;
    features.setToDefault(protocol::BlockVersion(versionNumber));

    // tx count limit
    Entry txLimitEntry;
    txLimitEntry.setObject(
        SystemConfigEntry{boost::lexical_cast<std::string>(_ledgerConfig->blockTxCountLimit()), 0});
    sysTable->setRow(SYSTEM_KEY_TX_COUNT_LIMIT, std::move(txLimitEntry));

    // tx gas limit
    Entry gasLimitEntry;
    gasLimitEntry.setObject(SystemConfigEntry{boost::lexical_cast<std::string>(_gasLimit), 0});
    sysTable->setRow(SYSTEM_KEY_TX_GAS_LIMIT, std::move(gasLimitEntry));

    if (RPBFT_CONSENSUS_TYPE == _consensusType &&
        versionNumber >= (uint32_t)protocol::BlockVersion::V3_5_VERSION)
    {
        // rpbft config
        features.set(ledger::Features::Flag::feature_rpbft);

        Entry epochSealerNumEntry;
        epochSealerNumEntry.setObject(
            SystemConfigEntry{boost::lexical_cast<std::string>(_epochSealerNum), 0});
        sysTable->setRow(SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, std::move(epochSealerNumEntry));

        Entry epochBlockNumEntry;
        epochBlockNumEntry.setObject(
            SystemConfigEntry{boost::lexical_cast<std::string>(_epochBlockNum), 0});
        sysTable->setRow(SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, std::move(epochBlockNumEntry));

        Entry notifyRotateEntry;
        notifyRotateEntry.setObject(SystemConfigEntry("0", 0));
        sysTable->setRow(INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE, std::move(notifyRotateEntry));
    }

    // consensus leader period
    Entry leaderPeriodEntry;
    leaderPeriodEntry.setObject(SystemConfigEntry{
        boost::lexical_cast<std::string>(_ledgerConfig->leaderSwitchPeriod()), 0});
    sysTable->setRow(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, std::move(leaderPeriodEntry));

    LEDGER_LOG(INFO) << LOG_DESC("init the compatibilityVersion")
                     << LOG_KV("versionNumber", versionNumber);
    // write compatibility version
    Entry compatibilityVersionEntry;
    compatibilityVersionEntry.setObject(SystemConfigEntry{_compatibilityVersion, 0});
    sysTable->setRow(SYSTEM_KEY_COMPATIBILITY_VERSION, std::move(compatibilityVersionEntry));

    if (versionCompareTo(versionNumber, BlockVersion::V3_3_VERSION) >= 0)
    {
        // write auth check status
        Entry authCheckStatusEntry;
        authCheckStatusEntry.setObject(SystemConfigEntry{_isAuthCheck ? "1" : "0", 0});
        sysTable->setRow(SYSTEM_KEY_AUTH_CHECK_STATUS, std::move(authCheckStatusEntry));
    }

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

    for (const auto& node : _ledgerConfig->consensusNodeList())
    {
        consensusNodeList.emplace_back(
            node->nodeID()->hex(), node->weight(), std::string{CONSENSUS_SEALER}, "0");
    }

    // update some node type to CONSENSUS_CANDIDATE_SEALER
    if (versionNumber >= (uint32_t)protocol::BlockVersion::V3_5_VERSION &&
        RPBFT_CONSENSUS_TYPE == _consensusType)
    {
        auto workingSealerList = selectWorkingSealer(_ledgerConfig, _epochSealerNum);
        for (auto& node : consensusNodeList)
        {
            auto iter = std::find_if(
                workingSealerList->begin(), workingSealerList->end(), [&node](auto&& workingNode) {
                    return workingNode->nodeID()->hex() == node.nodeID;
                });
            if (iter == workingSealerList->end())
            {
                node.type = CONSENSUS_CANDIDATE_SEALER;
            }
        }
    }

    for (const auto& node : _ledgerConfig->observerNodeList())
    {
        consensusNodeList.emplace_back(
            node->nodeID()->hex(), node->weight(), std::string{CONSENSUS_OBSERVER}, "0");
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
            LedgerError::CallbackError, "Write genesis consensus node list failed!", *error));
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

    Entry archivedNumber;
    archivedNumber.importFields({"0"});
    stateTable->setRow(SYS_KEY_ARCHIVED_NUMBER, std::move(archivedNumber));

    task::syncWait(features.writeToStorage(*m_storage, 0));
    return true;
}

bcos::consensus::ConsensusNodeListPtr Ledger::selectWorkingSealer(
    const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig, std::int64_t _epochSealerNum)
{
    auto sealerList = _ledgerConfig->consensusNodeList();
    std::sort(sealerList.begin(), sealerList.end(), bcos::consensus::ConsensusNodeComparator());

    std::int64_t sealersSize = sealerList.size();
    std::int64_t selectedNum = std::min(_epochSealerNum, sealersSize);

    // shuffle the sealerList according to the genesis hash
    // select the genesis working sealers randomly according to genesis hash
    if (sealersSize > selectedNum)
    {
        // from back to front, hash random swap selectedNode
        // [0,1,2,3] (i=3, chose n in [0,1,2,3] to swap)
        // => [0,1,3],2  (i=2, chose n in [0,1,3] to swap)
        // => [0,3],1,2  (i=1, chose n in [0,3] to swap)
        // => [0],3,1,2
        // no need to swap 0th element
        for (std::int64_t i = sealersSize - 1; i > 0; --i)
        {
            auto hashImpl = m_blockFactory->cryptoSuite()->hashImpl();
            std::int64_t selectedNode =
                (std::int64_t)((u256)(hashImpl->hash(sealerList[i]->nodeID()->data())) % (i + 1));
            std::swap(sealerList[i], sealerList[selectedNode]);
        }
    }

    auto workingSealerList = std::make_shared<bcos::consensus::ConsensusNodeList>();
    for (std::int64_t i = 0; i < selectedNum; ++i)
    {
        LEDGER_LOG(INFO) << LOG_DESC("selectWorkingSealer")
                         << LOG_KV("nodeID", sealerList[i]->nodeID()->hex())
                         << LOG_KV("weight", sealerList[i]->weight());
        workingSealerList->emplace_back(sealerList[i]);
    }
    return workingSealerList;
}

void Ledger::createFileSystemTables(uint32_t blockVersion)
{
    std::array<std::string_view, 4> rootSubNames = {
        tool::FS_APPS, tool::FS_USER, tool::FS_USER_TABLE, tool::FS_SYS_BIN};

    /// blockVersion >= 3.1.0, use executor build
    if (blockVersion >= (uint32_t)BlockVersion::V3_1_VERSION)
    {
        return;
    }
    buildDir(tool::FS_ROOT, blockVersion);
    // root table must exist

    Entry rootSubEntry;
    std::map<std::string, std::string> rootSubMap;
    for (const auto& sub : rootSubNames | RANGES::views::transform(
                                              [](std::string_view const& sub) -> std::string_view {
                                                  return sub.substr(1);
                                              }))
    {
        rootSubMap.insert(std::make_pair(sub, FS_TYPE_DIR));
    }
    rootSubEntry.importFields({asString(codec::scale::encode(rootSubMap))});
    std::promise<Error::UniquePtr> setPromise;
    m_storage->asyncSetRow(
        bcos::tool::FS_ROOT, FS_KEY_SUB, std::move(rootSubEntry), [&setPromise](auto&& error) {
            setPromise.set_value(std::forward<decltype(error)>(error));
        });
    auto setError = setPromise.get_future().get();
    if (setError)
    {
        BOOST_THROW_EXCEPTION(*setError);
    }

    buildDir(tool::FS_USER, blockVersion);
    buildDir(tool::FS_APPS, blockVersion);
    buildDir(tool::FS_USER_TABLE, blockVersion);
    auto sysTable = buildDir(tool::FS_SYS_BIN, blockVersion);
    Entry sysSubEntry;
    std::map<std::string, std::string> sysSubMap;
    for (const auto& contract :
        precompiled::BFS_SYS_SUBS_V30 |
            RANGES::views::transform([](std::string_view const& sub) -> std::string_view {
                return sub.substr(tool::FS_SYS_BIN.length() + 1);
            }))
    {
        sysSubMap.insert(std::make_pair(contract, FS_TYPE_CONTRACT));
    }
    sysSubEntry.importFields({asString(codec::scale::encode(sysSubMap))});
    sysTable->setRow(FS_KEY_SUB, std::move(sysSubEntry));
}

std::optional<storage::Table> Ledger::buildDir(
    const std::string_view& _absoluteDir, uint32_t blockVersion, std::string valueField)
{
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> createPromise;
    m_storage->asyncCreateTable(std::string(_absoluteDir), std::move(valueField),
        [&createPromise](auto&& error, std::optional<Table>&& _table) {
            createPromise.set_value({std::forward<decltype(error)>(error), std::move(_table)});
        });
    auto [createError, table] = createPromise.get_future().get();
    if (createError)
    {
        BOOST_THROW_EXCEPTION(*createError);
    }
    if (blockVersion >= (uint32_t)BlockVersion::V3_1_VERSION)
    {
        // >= 3.1.0 logic
        return table;
    }
    // 3.0.0 logic
    Entry tEntry;
    Entry newSubEntry;
    Entry aclTypeEntry;
    Entry aclWEntry;
    Entry aclBEntry;
    Entry extraEntry;
    std::map<std::string, std::string> newSubMap;
    newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
    tEntry.importFields({std::string(FS_TYPE_DIR)});
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
    return table;
}

void Ledger::asyncGetCurrentStateByKey(std::string_view const& _key,
    std::function<void(Error::Ptr&&, std::optional<bcos::storage::Entry>&&)> _callback)
{
    m_storage->asyncOpenTable(SYS_CURRENT_STATE, [this, callback = std::move(_callback), _key](
                                                     auto&& error, std::optional<Table>&& table) {
        auto tableError =
            checkTableValid(std::forward<decltype(error)>(error), table, SYS_CURRENT_STATE);
        if (tableError)
        {
            LEDGER_LOG(DEBUG) << LOG_DESC("asyncGetCurrentStateByKey failed") << LOG_KV("key", _key)
                              << boost::diagnostic_information(*tableError);
            callback(std::move(tableError), {});
            return;
        }
        table->asyncGetRow(_key, [_key, &callback](auto&& error, std::optional<Entry>&& entry) {
            if (error)
            {
                LEDGER_LOG(DEBUG) << LOG_DESC("asyncGetCurrentStateByKey exception")
                                  << LOG_KV("key", _key) << boost::diagnostic_information(*error);
                callback(BCOS_ERROR_WITH_PREV_PTR(
                             LedgerError::GetStorageError, "Get row failed", *error),
                    {});
                return;
            }
            callback(nullptr, std::move(entry));
        });
    });
}
