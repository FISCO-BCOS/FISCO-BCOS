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
#include "bcos-framework/ledger/IL2ConfigLoader.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/StorageInterface.h"
#include <bcos-framework/ledger/SystemConfigs.h>
#include <bcos-table/src/StateStorageFactory.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/compute/detail/lru_cache.hpp>
#include <utility>
#include <bcos-utilities/BoostLog.h>

#define LEDGER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("LEDGER")

namespace bcos::ledger
{

DERIVE_BCOS_EXCEPTION(NotFoundTransaction);
DERIVE_BCOS_EXCEPTION(UnexpectedRowIndex);
DERIVE_BCOS_EXCEPTION(MismatchTransactionCount);
DERIVE_BCOS_EXCEPTION(MismatchParentHash);
DERIVE_BCOS_EXCEPTION(NotFoundBlockHeader);
DERIVE_BCOS_EXCEPTION(GetABIError);
DERIVE_BCOS_EXCEPTION(GetBlockDataError);

class Ledger : public LedgerInterface
{
public:
    using CacheType =
        boost::compute::detail::lru_cache<int64_t, std::shared_ptr<std::vector<h256>>>;

    Ledger(bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::storage::StorageInterface::Ptr _storage, size_t _blockLimit,
        bcos::storage::StorageInterface::Ptr _blockStorage = nullptr, int merkleTreeCacheSize = 100,
        bcos::IOServicePool::Ptr _ioServicePool = nullptr)
      : m_blockFactory(std::move(_blockFactory)),
        m_stateStorage(std::move(_storage)),
        m_blockStorage(std::move(_blockStorage)),
        m_ioServicePool(std::move(_ioServicePool)),
        m_blockLimit(_blockLimit),
        m_merkleTreeCacheSize(merkleTreeCacheSize),
        m_txProofMerkleCache(m_merkleTreeCacheSize),
        m_receiptProofMerkleCache(m_merkleTreeCacheSize)
    {}

    ~Ledger() override = default;

    void asyncPreStoreBlockTxs(bcos::protocol::ConstTransactionsPtr _blockTxs,
        bcos::protocol::Block::ConstPtr block,
        std::function<void(Error::UniquePtr&&)> _callback) override;
    // No default arguments here — see LedgerInterface::asyncPrewriteBlock.
    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::ConstTransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
        std::function<void(std::string, Error::Ptr&&)> callback, bool writeTxsAndReceipts,
        std::optional<bcos::ledger::Features> features,
        std::optional<bcos::crypto::HashType> blockHashOverride, bool writeNonces) override;

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
        std::function<void(Error::Ptr, bcos::protocol::TransactionReceipt::Ptr, MerkleProofPtr)>
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

    void asyncGetNodeListByType(std::string_view const& _type,
        std::function<void(Error::Ptr, consensus::ConsensusNodeList)> _onGetConfig) override;

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

    task::Task<bcos::ledger::SystemConfigs> fetchAllSystemConfigs(
        protocol::BlockNumber number = INT64_MAX) override;

    task::Task<bcos::ledger::Features> fetchAllFeatures(protocol::BlockNumber) override;

    // Single-flag read (round-2 Finding E): one SYS_CONFIG row instead of fetchAllFeatures'
    // ~61-key scan; used by the historical state-read path for feature_l2_ethereum_compat.
    task::Task<bool> fetchFeature(
        bcos::ledger::Features::Flag flag, protocol::BlockNumber blockNumber) override;

    storage::StorageInterface::Ptr getStateStorage() override;

    // L2 mode: inject the per-block SystemConfig loader. AIR/MAX wire this only
    // when running in L2 chain mode; a null loader makes loadL2Config a no-op so
    // PBFT/non-L2 paths short-circuit without an EVM staticcall.
    void setL2ConfigLoader(ledger::IL2ConfigLoader::Ptr loader) { m_l2Loader = std::move(loader); }
    task::Task<void> loadL2Config(protocol::BlockNumber blockNumber, ledger::LedgerConfig& cfg);

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

    task::Task<std::optional<ledger::StorageState>> getStorageState(
        std::string_view _address, protocol::BlockNumber _blockNumber) override;

    std::tuple<bool, bcos::crypto::HashListPtr, std::shared_ptr<std::vector<bytesConstPtr>>>
    needStoreUnsavedTxs(
        bcos::protocol::ConstTransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr _block);

    bcos::consensus::ConsensusNodeList selectWorkingSealer(
        const bcos::ledger::LedgerConfig& _ledgerConfig, std::int64_t _epochSealerNum);

    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::storage::StorageInterface::Ptr m_stateStorage;
    // if m_blockStorage,txs and receipts will be stored in m_blockStorage
    bcos::storage::StorageInterface::Ptr m_blockStorage = nullptr;

    mutable RecursiveMutex m_mutex;
    std::shared_ptr<bcos::IOServicePool> m_ioServicePool;
    size_t m_blockLimit;

    // Maintain merkle trees of 100 blocks
    int m_merkleTreeCacheSize;
    RecursiveMutex m_txMerkleMtx;
    RecursiveMutex m_receiptMerkleMtx;
    CacheType m_txProofMerkleCache;
    CacheType m_receiptProofMerkleCache;
    size_t m_keyPageSize = 0;
    // null unless running in L2 chain mode; see setL2ConfigLoader/loadL2Config.
    ledger::IL2ConfigLoader::Ptr m_l2Loader;
};
}  // namespace bcos::ledger
