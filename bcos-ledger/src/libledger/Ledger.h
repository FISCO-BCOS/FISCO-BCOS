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
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "utilities/Common.h"
#include <bcos-framework/ledger/SystemConfigs.h>
#include <bcos-table/src/StateStorageFactory.h>
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
        bcos::storage::StorageInterface::Ptr _storage, size_t _blockLimit,
        bcos::storage::StorageInterface::Ptr _blockStorage = nullptr, int merkleTreeCacheSize = 100)
      : m_blockFactory(std::move(_blockFactory)),
        m_stateStorage(std::move(_storage)),
        m_blockStorage(std::move(_blockStorage)),
        m_threadPool(std::make_shared<ThreadPool>("ledgerWrite", 2)),
        m_blockLimit(_blockLimit),
        m_merkleTreeCacheSize(merkleTreeCacheSize),
        m_txProofMerkleCache(m_merkleTreeCacheSize),
        m_receiptProofMerkleCache(m_merkleTreeCacheSize)
    {}

    ~Ledger() override = default;

    void asyncPreStoreBlockTxs(bcos::protocol::ConstTransactionsPtr _blockTxs,
        bcos::protocol::Block::ConstPtr block,
        std::function<void(Error::UniquePtr&&)> _callback) override;
    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::ConstTransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
        std::function<void(std::string, Error::Ptr&&)> callback,
        bool writeTxsAndReceipts = true) override;

    bcos::Error::Ptr storeTransactionsAndReceipts(bcos::protocol::ConstTransactionsPtr blockTxs,
        bcos::protocol::Block::ConstPtr block) override;

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
    void removeExpiredNonce(protocol::BlockNumber blockNumber, bool sync = false) override;

    void asyncGetNodeListByType(const std::string_view& _type,
        std::function<void(Error::Ptr, consensus::ConsensusNodeListPtr)> _onGetConfig) override;

    void asyncGetCurrentStateByKey(std::string_view const& _key,
        std::function<void(Error::Ptr&&, std::optional<bcos::storage::Entry>&&)> _callback)
        override;
    Error::Ptr setCurrentStateByKey(
        std::string_view const& _key, bcos::storage::Entry entry) override;

    task::Task<std::optional<storage::Entry>> getStorageAt(std::string_view _address,
        std::string_view _key, protocol::BlockNumber _blockNumber) override;

    bool buildGenesisBlock(GenesisConfig const& genesis, ledger::LedgerConfig const& ledgerConfig);

    void asyncGetBlockTransactionHashes(bcos::protocol::BlockNumber blockNumber,
        std::function<void(Error::Ptr&&, std::vector<std::string>&&)> callback);
    void setKeyPageSize(size_t keyPageSize) { m_keyPageSize = keyPageSize; }

protected:
    storage::StateStorageInterface::Ptr getStateStorage()
    {
        if (m_keyPageSize > 0)
        {
            // create keyPageStorage
            storage::StateStorageFactory stateStorageFactory(m_keyPageSize);
            // getABI function begin in version 320
            auto keyPageIgnoreTables = std::make_shared<std::set<std::string, std::less<>>>(
                storage::IGNORED_ARRAY_310.begin(), storage::IGNORED_ARRAY_310.end());
            auto [error, entry] = m_stateStorage->getRow(
                ledger::SYS_CONFIG, ledger::SYSTEM_KEY_COMPATIBILITY_VERSION);
            if (!entry || error)
            {
                BOOST_THROW_EXCEPTION(
                    BCOS_ERROR(GetStorageError, "Not found compatibilityVersion."));
            }
            auto [compatibilityVersionStr, _] = entry->template getObject<SystemConfigEntry>();
            auto const version = bcos::tool::toVersionNumber(compatibilityVersionStr);
            auto stateStorage = stateStorageFactory.createStateStorage(
                m_stateStorage, version, true, false, keyPageIgnoreTables);
            return stateStorage;
        }
        return std::make_shared<bcos::storage::StateStorage>(m_stateStorage, true);
    }

private:
    Error::Ptr checkTableValid(Error::UniquePtr&& error,
        const std::optional<bcos::storage::Table>& table, const std::string_view& tableName);

    Error::Ptr checkEntryValid(Error::UniquePtr&& error,
        const std::optional<bcos::storage::Entry>& entry, const std::string_view& key);

    void asyncGetBlockHeader(bcos::protocol::Block::Ptr block,
        bcos::protocol::BlockNumber blockNumber, std::function<void(Error::Ptr&&)> callback);

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

    bcos::storage::StorageInterface::Ptr getBlockStorage()
    {
        return m_blockStorage ? m_blockStorage : m_stateStorage;
    }
    std::optional<storage::Table> buildDir(const std::string_view& _absoluteDir,
        uint32_t blockVersion, std::string valueField = SYS_VALUE);

    // only for /sys/
    static inline std::string getSysBaseName(const std::string& _s)
    {
        return _s.substr(_s.find_last_of('/') + 1);
    }

    std::tuple<bool, bcos::crypto::HashListPtr, std::shared_ptr<std::vector<bytesConstPtr>>>
    needStoreUnsavedTxs(
        bcos::protocol::ConstTransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr _block);

    bcos::consensus::ConsensusNodeListPtr selectWorkingSealer(
        const bcos::ledger::LedgerConfig& _ledgerConfig, std::int64_t _epochSealerNum);

    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::storage::StorageInterface::Ptr m_stateStorage;
    // if m_blockStorage,txs and receipts will be stored in m_blockStorage
    bcos::storage::StorageInterface::Ptr m_blockStorage = nullptr;

    mutable RecursiveMutex m_mutex;
    std::shared_ptr<bcos::ThreadPool> m_threadPool;
    size_t m_blockLimit;

    // Maintain merkle trees of 100 blocks
    int m_merkleTreeCacheSize;
    RecursiveMutex m_txMerkleMtx;
    RecursiveMutex m_receiptMerkleMtx;
    CacheType m_txProofMerkleCache;
    CacheType m_receiptProofMerkleCache;
    size_t m_keyPageSize = 0;
};
}  // namespace bcos::ledger
