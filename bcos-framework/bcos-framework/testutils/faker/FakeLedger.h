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
 * @brief fake ledger
 * @file FakeLedger.h
 * @author: yujiechen
 * @date 2021-05-25
 */
#pragma once
#include "../../ledger/LedgerConfig.h"
#include "../../ledger/LedgerInterface.h"
#include "../../protocol/Block.h"
#include "FakeBlock.h"
#include <bcos-utilities/ThreadPool.h>

#include <map>
#include <string>
#include <utility>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::consensus;
namespace bcos
{
namespace test
{

class FakeLedger : public LedgerInterface, public std::enable_shared_from_this<FakeLedger>
{
public:
    class FakeStorage : public storage::StorageInterface
    {
    public:
        using Value = storage::Entry;  // 必须的类型别名

        FakeStorage(FakeLedger* ledger) : m_ledger(ledger) {}

        // 修复 asyncGetRow 方法签名
        void asyncGetRow(std::string_view table, std::string_view _key,
            std::function<void(Error::UniquePtr, std::optional<storage::Entry>)> _callback) override
        {
            std::string tableStr(table);
            std::string keyStr(_key);

            // 直接使用 FakeLedger 的 fakeStorageEntryMaps
            if (m_ledger->fakeStorageEntryMaps.count(tableStr) &&
                m_ledger->fakeStorageEntryMaps[tableStr].count(keyStr))
            {
                _callback(nullptr, m_ledger->fakeStorageEntryMaps[tableStr][keyStr]);
            }
            else
            {
                _callback(nullptr, std::nullopt);
            }
        }

        // 修复 asyncSetRow 方法签名
        void asyncSetRow(std::string_view table, std::string_view key, storage::Entry entry,
            std::function<void(Error::UniquePtr)> callback) override
        {
            // 直接使用 FakeLedger 的 fakeStorageEntryMaps
            m_ledger->fakeStorageEntryMaps[std::string(table)][std::string(key)] = std::move(entry);
            callback(nullptr);
        }

        // 修复 asyncGetRows 方法签名
        void asyncGetRows(std::string_view table,
            RANGES::any_view<std::string_view,
                RANGES::category::input | RANGES::category::random_access | RANGES::category::sized>
                keys,
            std::function<void(Error::UniquePtr, std::vector<std::optional<storage::Entry>>)>
                _callback) override
        {
            std::vector<std::optional<storage::Entry>> results;
            for (auto const& key : keys)
            {
                std::string tableStr(table);
                std::string keyStr(key);

                if (m_ledger->fakeStorageEntryMaps.count(tableStr) &&
                    m_ledger->fakeStorageEntryMaps[tableStr].count(keyStr))
                {
                    results.push_back(m_ledger->fakeStorageEntryMaps[tableStr][keyStr]);
                }
                else
                {
                    results.push_back(std::nullopt);
                }
            }
            _callback(nullptr, std::move(results));
        }

        // 添加其他必需的虚函数实现
        void asyncGetPrimaryKeys(std::string_view table,
            const std::optional<storage::Condition const>& _condition,
            std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override
        {
            std::vector<std::string> keys;
            std::string tableStr(table);
            if (m_ledger->fakeStorageEntryMaps.count(tableStr))
            {
                for (const auto& [key, value] : m_ledger->fakeStorageEntryMaps[tableStr])
                {
                    keys.push_back(key);
                }
            }
            _callback(nullptr, std::move(keys));
        }

    private:
        FakeLedger* m_ledger;
    };
    using Ptr = std::shared_ptr<FakeLedger>;
    FakeLedger() = default;
    FakeLedger(BlockFactory::Ptr _blockFactory, size_t _blockNumber, size_t _txsSize, size_t,
        std::vector<bytes> _sealerList)
      : m_blockFactory(std::move(_blockFactory)),
        m_ledgerConfig(std::make_shared<LedgerConfig>()),
        m_sealerList(std::move(std::move(_sealerList)))
    {
        init(_blockNumber, _txsSize, 0);
        m_worker = std::make_shared<ThreadPool>("ledgerWorker", 1);
    }
    ~FakeLedger() override
    {
        stop();
        m_hash2Block.clear();
        std::map<HashType, BlockNumber> emptyHash2Block;
        m_hash2Block.swap(emptyHash2Block);

        m_txsHashToData.clear();
        std::map<HashType, bytesConstPtr> emptyTxsData;
        m_txsHashToData.swap(emptyTxsData);
    }
    void stop()
    {
        if (m_worker)
        {
            m_worker->stop();
        }
    }

    FakeLedger(
        BlockFactory::Ptr _blockFactory, size_t _blockNumber, size_t _txsSize, size_t _receiptsSize)
      : m_blockFactory(std::move(_blockFactory)), m_ledgerConfig(std::make_shared<LedgerConfig>())
    {
        auto sigImpl = m_blockFactory->cryptoSuite()->signatureImpl();
        m_sealerList = fakeSealerList(m_keyPairVec, sigImpl, 4);
        init(_blockNumber, _txsSize, _receiptsSize);
        m_worker = std::make_shared<ThreadPool>("ledgerWorker", 1);
    }

    void init(size_t _blockNumber, size_t _txsSize, int64_t _timestamp = utcTime())
    {
        auto genesisBlock = init(nullptr, true, 0, 0, 0);
        m_ledger.push_back(genesisBlock);
        m_hash2Block[genesisBlock->blockHeader()->hash()] = 0;

        auto parentHeader = genesisBlock->blockHeader();
        for (size_t i = 1; i < _blockNumber; i++)
        {
            auto block = init(parentHeader, true, i, _txsSize, _timestamp);
            parentHeader = block->blockHeader();
            m_ledger.push_back(block);
            m_hash2Block[block->blockHeader()->hash()] = i;
        }
        auto latestBlock = m_ledger[m_ledger.size() - 1];
        updateLedgerConfig(latestBlock->blockHeader());
    }

    Block::Ptr init(BlockHeader::Ptr _parentBlockHeader, bool _withHeader, BlockNumber _blockNumber,
        size_t _txsSize, int64_t _timestamp = utcTime())
    {
        auto block = fakeAndCheckBlock(m_blockFactory->cryptoSuite(), m_blockFactory, _txsSize,
            _txsSize, _blockNumber, true, false);
        if (!_withHeader)
        {
            return block;
        }
        ParentInfoList parentInfo;
        if (_parentBlockHeader != nullptr)
        {
            ParentInfo info{_parentBlockHeader->number(), _parentBlockHeader->hash()};
            parentInfo.push_back(info);
        }
        auto rootHash =
            m_blockFactory->cryptoSuite()->hashImpl()->hash(std::to_string(_blockNumber));
        u256 gasUsed = 1232342523;

        SignatureList signatureList;
        // fake blockHeader
        auto blockHeader = fakeAndTestBlockHeader(m_blockFactory->cryptoSuite(), 0, parentInfo,
            rootHash, rootHash, rootHash, _blockNumber, gasUsed, _timestamp, 0, m_sealerList,
            bytes(), signatureList, false);
        auto sigImpl = m_blockFactory->cryptoSuite()->signatureImpl();
        blockHeader->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());
        signatureList = fakeSignatureList(sigImpl, m_keyPairVec, blockHeader->hash());
        blockHeader->setSignatureList(signatureList);
        block->setBlockHeader(blockHeader);
        return block;
    }

    Block::Ptr populateFromHeader(BlockHeader::Ptr _blockHeader)
    {
        auto block = m_blockFactory->createBlock();
        block->blockHeader()->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());
        block->setBlockHeader(_blockHeader);
        return block;
    }

    void updateLedgerConfig(BlockHeader::Ptr _blockHeader)
    {
        m_ledgerConfig->setBlockNumber(_blockHeader->number());
        m_ledgerConfig->setHash(_blockHeader->hash());
    }

    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr block,
        std::function<void(std::string, Error::Ptr&&)> callback, bool writeTxsAndReceipts,
        std::optional<bcos::ledger::Features> features) override
    {
        (void)storage;
        (void)block;
        callback("", nullptr);
    }

    void setStorageState(std::string address, ledger::StorageState _state)
    {
        m_storageState[std::move(address)] = std::move(_state);
    }

    task::Task<std::optional<ledger::StorageState>> getStorageState(
        std::string_view _address, protocol::BlockNumber) override
    {
        if (auto const it = m_storageState.find(std::string(_address)); it != m_storageState.end())
        {
            co_return std::make_optional(it->second);
        }
        co_return std::nullopt;
    }

    void asyncPreStoreBlockTxs(bcos::protocol::ConstTransactionsPtr,
        bcos::protocol::Block::ConstPtr, std::function<void(Error::UniquePtr&&)> _callback) override
    {
        if (!_callback)
        {
            return;
        }
        _callback(nullptr);
    }

    // the txpool module use this interface to store txs
    bcos::Error::Ptr storeTransactionsAndReceipts(bcos::protocol::ConstTransactionsPtr blockTxs,
        bcos::protocol::Block::ConstPtr block) override
    {
        WriteGuard l(x_txsHashToData);
        for (size_t i = 0; i < block->transactionsSize(); i++)
        {
            auto tx = blockTxs ? blockTxs->at(i) : block->transaction(i);
            auto txHash = tx->hash();
            std::shared_ptr<bcos::bytes> txData;
            tx->encode(*txData);
            m_txsHashToData[txHash] = txData;
        }
        return nullptr;
    }

    // maybe sync module or rpc module need this interface to return header/txs/receipts
    void asyncGetBlockDataByNumber(BlockNumber _number, int32_t,
        std::function<void(Error::Ptr, Block::Ptr)> _callback) override
    {
        ReadGuard l(x_ledger);
        if (m_ledger.size() <= (size_t)(_number))
        {
            _callback(BCOS_ERROR_UNIQUE_PTR(-1, "block not found"), nullptr);
            return;
        }
        auto block = m_ledger[_number];
        _callback(nullptr, block);
    }

    void asyncGetBlockNumberByHash(
        crypto::HashType const&, std::function<void(Error::Ptr, BlockNumber)>) override
    {}

    void asyncGetBlockHashByNumber(BlockNumber _blockNumber,
        std::function<void(Error::Ptr, crypto::HashType)> _onGetBlock) override
    {
        ReadGuard l(x_ledger);
        auto const& hash = m_ledger[_blockNumber]->blockHeader()->hash();
        _onGetBlock(nullptr, hash);
    }

    void asyncGetBlockNumber(std::function<void(Error::Ptr, BlockNumber)> _onGetBlock) override
    {
        _onGetBlock(nullptr, m_ledgerConfig->blockNumber());
    }

    void asyncGetBatchTxsByHashList(crypto::HashListPtr _txHashList, bool,
        std::function<void(
            Error::Ptr, TransactionsPtr, std::shared_ptr<std::map<std::string, MerkleProofPtr>>)>
            _onGetTx) override
    {
        ReadGuard l(x_txsHashToData);
        auto txs = std::make_shared<Transactions>();
        for (auto const& hash : *_txHashList)
        {
            if (m_txsHashToData.count(hash))
            {
                auto tx = m_blockFactory->transactionFactory()->createTransaction(
                    ref(*(m_txsHashToData[hash])), false);
                txs->emplace_back(tx);
            }
        }
        _onGetTx(nullptr, txs, nullptr);
    }

    void asyncGetTransactionReceiptByHash(crypto::HashType const&, bool,
        std::function<void(Error::Ptr, TransactionReceipt::ConstPtr, MerkleProofPtr)> _onGetTx)
        override
    {
        _onGetTx(nullptr, nullptr, nullptr);
    }

    void asyncGetTotalTransactionCount(std::function<void(Error::Ptr, int64_t _totalTxCount,
            int64_t _failedTxCount, BlockNumber _latestBlockNumber)>
            _callback) override
    {
        _callback(nullptr, m_totalTxCount, 0, m_ledgerConfig->blockNumber());
    }

    void asyncGetCurrentStateByKey(std::string_view const& _key,
        std::function<void(Error::Ptr&&, std::optional<bcos::storage::Entry>&&)> _callback) override
    {
        _callback(nullptr, {});
    }
    Error::Ptr setCurrentStateByKey(
        std::string_view const& _key, bcos::storage::Entry entry) override
    {
        return nullptr;
    }

    void asyncGetSystemConfigByKey(std::string_view const& _key,
        std::function<void(Error::Ptr, std::string, BlockNumber)> _onGetConfig) override
    {
        std::string value = "";
        if (m_systemConfig.contains(_key))
        {
            value = m_systemConfig[std::string{_key}];
        }
        else
        {
            _onGetConfig(BCOS_ERROR_PTR(3008, "key not found"), "", m_ledgerConfig->blockNumber());
            return;
        }
        _onGetConfig(nullptr, value, m_ledgerConfig->blockNumber());
    }

    task::Task<bcos::ledger::SystemConfigs> fetchAllSystemConfigs(
        protocol::BlockNumber _blockNumber) override
    {
        ledger::SystemConfigs configs;
        for (auto const& [key, value] : m_systemConfig)
        {
            configs.set(key, value, 0);
        }
        co_return configs;
    }

    void asyncGetNodeListByType(std::string_view const& _type,
        std::function<void(Error::Ptr, ConsensusNodeList)> _onGetNodeList) override
    {
        if (_type == CONSENSUS_SEALER)
        {
            auto consensusNodes = m_ledgerConfig->consensusNodeList();
            _onGetNodeList(nullptr, consensusNodes);
            return;
        }
        if (_type == CONSENSUS_OBSERVER)
        {
            auto observerNodes = m_ledgerConfig->observerNodeList();
            _onGetNodeList(nullptr, observerNodes);
            return;
        }
        if (_type == CONSENSUS_CANDIDATE_SEALER)
        {
            auto consensusNodes = m_ledgerConfig->candidateSealerNodeList();
            _onGetNodeList(nullptr, consensusNodes);
            return;
        }
        if (_type.empty())
        {
            auto allNodes =
                ::ranges::views::concat(m_ledgerConfig->consensusNodeList(),
                    m_ledgerConfig->observerNodeList(), m_ledgerConfig->candidateSealerNodeList()) |
                ::ranges::to<std::vector>();
            _onGetNodeList(nullptr, allNodes);
            return;
        }
        _onGetNodeList(BCOS_ERROR_UNIQUE_PTR(-1, "invalid Type"), {});
    }

    void asyncGetNonceList(BlockNumber _startNumber, int64_t _offset,
        std::function<void(Error::Ptr, std::shared_ptr<std::map<BlockNumber, NonceListPtr>>)>
            _onGetList) override
    {
        if (_startNumber > m_ledgerConfig->blockNumber())
        {
            _onGetList(nullptr, nullptr);
        }
        auto endNumber = std::min(_startNumber + _offset - 1, m_ledgerConfig->blockNumber());
        auto nonceList = std::make_shared<std::map<BlockNumber, NonceListPtr>>();
        ReadGuard l(x_ledger);
        for (auto index = _startNumber; index <= endNumber; index++)
        {
            auto nonces = m_ledger[index]->nonces();
            nonceList->insert(std::make_pair(index, nonces));
        }
        _onGetList(nullptr, nonceList);
    }
    void removeExpiredNonce(protocol::BlockNumber blockNumber, bool sync) override {}

    void setStatus(bool _normal) { m_statusNormal = _normal; }
    void setTotalTxCount(size_t _totalTxCount) { m_totalTxCount = _totalTxCount; }
    void setSystemConfig(std::string_view _key, std::string const& _value)
    {
        m_systemConfig[std::string{_key}] = _value;
    }

    void setConsensusNodeList(ConsensusNodeList _consensusNodes)
    {
        m_ledgerConfig->setConsensusNodeList(_consensusNodes);
    }
    void setObserverNodeList(ConsensusNodeList _observerNodes)
    {
        m_ledgerConfig->setObserverNodeList(_observerNodes);
    }

    LedgerConfig::Ptr ledgerConfig() { return m_ledgerConfig; }
    BlockNumber blockNumber() { return m_ledgerConfig->blockNumber(); }
    std::vector<Block::Ptr> const& ledgerData()
    {
        ReadGuard l(x_ledger);
        return m_ledger;
    }

    size_t storedTxsSize()
    {
        ReadGuard l(x_txsHashToData);
        return m_txsHashToData.size();
    }
    // Note thread-safe
    std::map<HashType, bytesConstPtr> txsHashToData()
    {
        ReadGuard l(x_txsHashToData);
        return m_txsHashToData;
    }

    std::vector<bytes> sealerList() { return m_sealerList; }

    // Consensus and block-sync module use this interface to commit block
    virtual void asyncCommitBlock(const bcos::protocol::BlockHeader::ConstPtr& _blockHeader,
        std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr)> _onCommitBlock)
    {
        auto nonConstHeader = std::const_pointer_cast<bcos::protocol::BlockHeader>(_blockHeader);
        if (nonConstHeader->number() != m_ledgerConfig->blockNumber() + 1)
        {
            _onCommitBlock(BCOS_ERROR_PTR(-1, "invalid block"), nullptr);
            return;
        }

        auto self = std::weak_ptr<FakeLedger>(shared_from_this());
        m_worker->enqueue([nonConstHeader, _onCommitBlock, self]() {
            auto ledger = self.lock();
            if (!self.lock())
            {
                return;
            }
            WriteGuard l(ledger->x_ledger);
            auto block = ledger->populateFromHeader(nonConstHeader);
            ledger->m_ledger.push_back(block);
            ledger->updateLedgerConfig(nonConstHeader);
            _onCommitBlock(nullptr, ledger->m_ledgerConfig);
        });
    }

    void initEoaContext(std::string eoaInLedger, std::string nonce)
    {
        this->eoaInLedger = std::move(eoaInLedger);
        eoaInLedgerNonce = std::move(nonce);
    }

    void setStorageAt(std::string _address, std::string _key, std::optional<storage::Entry> _data)
    {
        fakeStorageEntryMaps[_address][_key] = std::move(_data);
    }

    task::Task<std::optional<storage::Entry>> getStorageAt(std::string_view _address,
        std::string_view _key, protocol::BlockNumber _blockNumber) override
    {
        co_return fakeStorageEntryMaps[std::string(_address)][std::string(_key)];
    }

    storage::StorageInterface::Ptr getStateStorage() override
    {
        if (!m_fakeStorage)
        {
            m_fakeStorage = std::make_shared<FakeStorage>(this);
        }
        return m_fakeStorage;
    }

private:
    BlockFactory::Ptr m_blockFactory;
    std::vector<KeyPairInterface::Ptr> m_keyPairVec;
    LedgerConfig::Ptr m_ledgerConfig;

    size_t m_totalTxCount;
    bool m_statusNormal = true;

    std::vector<Block::Ptr> m_ledger;
    std::map<HashType, BlockNumber> m_hash2Block;
    SharedMutex x_ledger;

    std::map<HashType, bytesConstPtr> m_txsHashToData;
    SharedMutex x_txsHashToData;

    std::map<std::string, std::string, std::less<>> m_systemConfig;
    std::vector<bytes> m_sealerList;
    std::shared_ptr<ThreadPool> m_worker = nullptr;
    std::unordered_map<std::string, ledger::StorageState> m_storageState = {};
    std::string eoaInLedger;
    std::string eoaInLedgerNonce;
    std::shared_ptr<FakeStorage> m_fakeStorage;
    std::map<std::string, std::map<std::string, std::optional<storage::Entry>>>
        fakeStorageEntryMaps;
};
}  // namespace test
}  // namespace bcos