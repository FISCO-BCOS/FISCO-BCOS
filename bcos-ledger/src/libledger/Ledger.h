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
 * @file Ledger.h
 * @author: kyonRay
 * @date 2021-04-13
 */
#pragma once
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/Common.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "utilities/Common.h"
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/ThreadPool.h>
#include <boost/compute/detail/lru_cache.hpp>
#include <utility>

#define LEDGER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("LEDGER")

namespace bcos::ledger
{
class Ledger : public LedgerInterface
{
public:
    using CacheType =
        boost::compute::detail::lru_cache<int64_t, std::shared_ptr<std::vector<h256>>>;

    Ledger(bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::storage::StorageInterface::Ptr _storage, int merkleTreeCacheSize = 100)
      : m_blockFactory(std::move(_blockFactory)),
        m_storage(std::move(_storage)),
        m_threadPool(std::make_shared<ThreadPool>("WriteReceipts", 1)),
        m_merkleTreeCacheSize(merkleTreeCacheSize),
        m_txProofMerkleCache(m_merkleTreeCacheSize),
        m_receiptProofMerkleCache(m_merkleTreeCacheSize)
    {}

    ~Ledger() override = default;

    void asyncPreStoreBlockTxs(bcos::protocol::TransactionsPtr _blockTxs,
        bcos::protocol::Block::ConstPtr block,
        std::function<void(Error::UniquePtr&&)> _callback) override;
    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::TransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
        std::function<void(Error::Ptr&&)> callback, bool writeTxsAndReceipts = true) override;

    bcos::Error::Ptr storeTransactionsAndReceipts(
        bcos::protocol::TransactionsPtr blockTxs, bcos::protocol::Block::ConstPtr block) override;

    void asyncGetBlockDataByNumber(bcos::protocol::BlockNumber _blockNumber, int32_t _blockFlag,
        std::function<void(Error::Ptr, bcos::protocol::Block::Ptr)> _onGetBlock) override;

    void asyncGetBlockNumber(
        std::function<void(Error::Ptr, bcos::protocol::BlockNumber)> _onGetBlock) override;

    void asyncGetBlockHashByNumber(bcos::protocol::BlockNumber _blockNumber,
        std::function<void(Error::Ptr, bcos::crypto::HashType)> _onGetBlock) override;

    void asyncGetBlockNumberByHash(const crypto::HashType& _blockHash,
        std::function<void(Error::Ptr, bcos::protocol::BlockNumber)> _onGetBlock) override;

    void asyncGetBatchTxsByHashList(crypto::HashListPtr _txHashList, bool _withProof,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr,
            std::shared_ptr<std::map<std::string, MerkleProofPtr>>)>
            _onGetTx) override;

    void asyncGetTransactionReceiptByHash(bcos::crypto::HashType const& _txHash, bool _withProof,
        std::function<void(
            Error::Ptr, bcos::protocol::TransactionReceipt::ConstPtr, MerkleProofPtr)>
            _onGetTx) override;

    void asyncGetTotalTransactionCount(
        std::function<void(Error::Ptr, int64_t, int64_t, bcos::protocol::BlockNumber)> _callback)
        override;

    void asyncGetSystemConfigByKey(const std::string_view& _key,
        std::function<void(Error::Ptr, std::string, bcos::protocol::BlockNumber)> _onGetConfig)
        override;

    void asyncGetNonceList(bcos::protocol::BlockNumber _startNumber, int64_t _offset,
        std::function<void(
            Error::Ptr, std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>)>
            _onGetList) override;

    void asyncGetNodeListByType(const std::string_view& _type,
        std::function<void(Error::Ptr, consensus::ConsensusNodeListPtr)> _onGetConfig) override;

    void asyncGetCurrentStateByKey(std::string_view const& _key,
        std::function<void(Error::Ptr&&, std::optional<bcos::storage::Entry>&&)> _callback)
        override;

    /****** init ledger ******/
    bool buildGenesisBlock(LedgerConfig::Ptr _ledgerConfig, size_t _gasLimit,
        const std::string_view& _genesisData, std::string const& _compatibilityVersion,
        bool _isAuthCheck = false, std::string const& _consensusType = "pbft",
        std::int64_t _epochSealerNum = 4, std::int64_t _epochBlockNum = 1000);

    void asyncGetBlockTransactionHashes(bcos::protocol::BlockNumber blockNumber,
        std::function<void(Error::Ptr&&, std::vector<std::string>&&)> callback);


private:
    Error::Ptr checkTableValid(Error::UniquePtr&& error,
        const std::optional<bcos::storage::Table>& table, const std::string_view& tableName);

    Error::Ptr checkEntryValid(Error::UniquePtr&& error,
        const std::optional<bcos::storage::Entry>& entry, const std::string_view& key);

    void asyncGetBlockHeader(bcos::protocol::Block::Ptr block,
        bcos::protocol::BlockNumber blockNumber, std::function<void(Error::Ptr&&)> callback);

    void asyncGetTransactionByHash(std::string_view hash,
        std::function<void(Error::Ptr&&, protocol::Transaction::Ptr&&)> callback);

    void asyncBatchGetTransactions(std::shared_ptr<std::vector<std::string>> hashes,
        std::function<void(Error::Ptr&&, std::vector<protocol::Transaction::Ptr>&&)> callback);

    void asyncBatchGetReceipts(std::shared_ptr<std::vector<std::string>> hashes,
        std::function<void(Error::Ptr&&, std::vector<protocol::TransactionReceipt::Ptr>&&)>
            callback);

    void getTxProof(const crypto::HashType& _txHash,
        std::function<void(Error::Ptr&&, MerkleProofPtr&&)> _onGetProof);

    void getReceiptProof(protocol::TransactionReceipt::Ptr _receipt,
        std::function<void(Error::Ptr&&, MerkleProofPtr&&)> _onGetProof);

    void asyncGetSystemTableEntry(const std::string_view& table, const std::string_view& key,
        std::function<void(Error::Ptr&&, std::optional<bcos::storage::Entry>&&)> callback);

    void createFileSystemTables(uint32_t blockVersion);

    std::optional<storage::Table> buildDir(const std::string_view& _absoluteDir,
        uint32_t blockVersion, std::string valueField = SYS_VALUE);

    // only for /sys/
    inline std::string getSysBaseName(const std::string& _s)
    {
        return _s.substr(_s.find_last_of('/') + 1);
    }

    std::tuple<bool, bcos::crypto::HashListPtr, std::shared_ptr<std::vector<bytesConstPtr>>>
    needStoreUnsavedTxs(
        bcos::protocol::TransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr _block);

    bcos::consensus::ConsensusNodeListPtr selectWorkingSealer(
        const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig, std::int64_t _epochSealerNum);

    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::storage::StorageInterface::Ptr m_storage;

    mutable RecursiveMutex m_mutex;
    std::shared_ptr<bcos::ThreadPool> m_threadPool;

    // Maintain merkle trees of 100 blocks
    int m_merkleTreeCacheSize;
    Mutex m_txMerkleMtx;
    Mutex m_receiptMerkleMtx;
    CacheType m_txProofMerkleCache;
    CacheType m_receiptProofMerkleCache;
};
}  // namespace bcos::ledger