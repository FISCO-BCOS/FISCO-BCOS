/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : Imp of blockchain
 * @author: mingzhenliu
 * @date: 2018-09-21
 */
#pragma once

#include "BlockChainInterface.h"

#include <libdevcore/Exceptions.h>
#include <libdevcore/ThreadPool.h>
#include <libethcore/Block.h>
#include <libethcore/Common.h>
#include <libethcore/Protocol.h>
#include <libethcore/Transaction.h>
#include <libethcore/TransactionReceipt.h>
#include <libexecutive/StateFactoryInterface.h>
#include <libprecompiled/SystemConfigPrecompiled.h>
#include <libstorage/Common.h>
#include <libstorage/Storage.h>
#include <libstorage/Table.h>
#include <libstoragestate/StorageStateFactory.h>
#include <boost/thread/shared_mutex.hpp>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

#define BLOCKCHAIN_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("BLOCKCHAIN")

namespace dev
{
namespace blockverifier
{
class ExecutiveContext;
}  // namespace blockverifier
namespace storage
{
class MemoryTableFactory;
}

namespace blockchain
{
class BlockChainImp;

class BlockCache
{
public:
    BlockCache(){};
    std::shared_ptr<dev::eth::Block> add(std::shared_ptr<dev::eth::Block> _block);
    std::pair<std::shared_ptr<dev::eth::Block>, dev::h256> get(h256 const& _hash);

    void setDestructorThread(dev::ThreadPool::Ptr _destructorThread)
    {
        m_destructorThread = _destructorThread;
    }

private:
    mutable boost::shared_mutex m_sharedMutex;
    mutable std::map<dev::h256, std::shared_ptr<dev::eth::Block>> m_blockCache;
    mutable std::deque<dev::h256> m_blockCacheFIFO;  // insert queue log for m_blockCache
    const unsigned c_blockCacheSize = 10;            // m_blockCache size, default set 10
    // used to destructor time-consuming, large memory objects
    dev::ThreadPool::Ptr m_destructorThread;
};
DEV_SIMPLE_EXCEPTION(OpenSysTableFailed);

using Parent2ChildListMap = std::map<std::string, std::vector<std::string>>;
using Child2ParentMap = tbb::concurrent_unordered_map<std::string, std::string>;
class BlockChainImp : public BlockChainInterface
{
public:
    BlockChainImp()
    {
        // used to destructor time-consuming, large memory objects
        m_destructorThread = std::make_shared<ThreadPool>("blkCache", 1);
        m_blockCache.setDestructorThread(m_destructorThread);
    }
    virtual ~BlockChainImp(){};
    int64_t number() override;
    dev::h256 numberHash(int64_t _i) override;
    dev::eth::Transaction::Ptr getTxByHash(dev::h256 const& _txHash) override;
    dev::eth::LocalisedTransaction::Ptr getLocalisedTxByHash(dev::h256 const& _txHash) override;
    dev::eth::TransactionReceipt::Ptr getTransactionReceiptByHash(
        dev::h256 const& _txHash) override;
    virtual dev::eth::LocalisedTransactionReceipt::Ptr getLocalisedTxReceiptByHash(
        dev::h256 const& _txHash) override;
    std::shared_ptr<dev::eth::Block> getBlockByHash(
        dev::h256 const& _blockHash, int64_t _blockNumber = -1) override;
    std::shared_ptr<dev::eth::Block> getBlockByNumber(int64_t _i) override;
    std::shared_ptr<dev::bytes> getBlockRLPByNumber(int64_t _i) override;
    CommitResult commitBlock(std::shared_ptr<dev::eth::Block> block,
        std::shared_ptr<dev::blockverifier::ExecutiveContext> context) override;

    virtual void setStateStorage(dev::storage::Storage::Ptr stateStorage);
    virtual void setStateFactory(dev::executive::StateFactoryInterface::Ptr _stateFactory);
    virtual std::shared_ptr<dev::storage::TableFactory> getMemoryTableFactory(int64_t num = 0);
    bool checkAndBuildGenesisBlock(GenesisBlockParam& initParam, bool _shouldBuild = true) override;
    std::pair<int64_t, int64_t> totalTransactionCount() override;
    std::pair<int64_t, int64_t> totalFailedTransactionCount() override;
    dev::bytes getCode(dev::Address _address) override;

    dev::h512s sealerList() override;
    dev::h512s observerList() override;

    std::string getSystemConfigByKey(std::string const& key, int64_t num = -1) override;

    std::shared_ptr<std::vector<dev::eth::NonceKeyType>> getNonces(int64_t _blockNumber) override;

    std::pair<std::string, dev::eth::BlockNumber> getSystemConfigInfoByKey(
        std::string const& _key, int64_t const& _num = -1) override;

    void setTableFactoryFactory(dev::storage::TableFactoryFactory::Ptr tableFactoryFactory)
    {
        m_tableFactoryFactory = tableFactoryFactory;
    }

    std::pair<dev::eth::LocalisedTransaction::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionByHashWithProof(dev::h256 const& _txHash) override;


    std::pair<dev::eth::LocalisedTransactionReceipt::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionReceiptByHashWithProof(
        dev::h256 const& _txHash, dev::eth::LocalisedTransaction& transaction) override;

    void setEnableHexBlock(bool const& _enableHexBlock) { m_enableHexBlock = _enableHexBlock; }

    std::shared_ptr<MerkleProofType> getTransactionReceiptProof(
        dev::eth::Block::Ptr _block, uint64_t const& _index) override;

    std::shared_ptr<MerkleProofType> getTransactionProof(
        dev::eth::Block::Ptr _block, uint64_t const& _index) override;

private:
    std::shared_ptr<Parent2ChildListMap> getParent2ChildListByReceiptProofCache(
        dev::eth::Block::Ptr _block);
    std::shared_ptr<Parent2ChildListMap> getParent2ChildListByTxsProofCache(
        dev::eth::Block::Ptr _block);

    std::shared_ptr<Child2ParentMap> getChild2ParentCacheByReceipt(
        std::shared_ptr<Parent2ChildListMap> _parent2ChildList, dev::eth::Block::Ptr _block);
    std::shared_ptr<Child2ParentMap> getChild2ParentCacheByTransaction(
        std::shared_ptr<Parent2ChildListMap> _parent2Child, dev::eth::Block::Ptr _block);

    std::shared_ptr<Child2ParentMap> getChild2ParentCache(SharedMutex& _mutex,
        std::pair<dev::eth::BlockNumber, std::shared_ptr<Child2ParentMap>>& _cache,
        std::shared_ptr<Parent2ChildListMap> _parent2Child, dev::eth::Block::Ptr _block);

    void initSystemConfig(
        dev::storage::Table::Ptr _tb, std::string const& _key, std::string const& _value);

    std::shared_ptr<dev::eth::Block> decodeBlock(dev::storage::Entry::ConstPtr _entry);
    std::shared_ptr<dev::bytes> getDataBytes(
        dev::storage::Entry::ConstPtr _entry, std::string const& _fieldName);

    void writeBytesToField(std::shared_ptr<dev::bytes> _data, dev::storage::Entry::Ptr _entry,
        std::string const& _fieldName = dev::storage::SYS_VALUE);
    void writeBlockToField(dev::eth::Block const& _block, dev::storage::Entry::Ptr _entry);

    std::shared_ptr<dev::eth::Block> getBlock(int64_t _blockNumber);
    std::shared_ptr<dev::eth::Block> getBlock(dev::h256 const& _blockHash, int64_t _blockNumber);
    std::shared_ptr<dev::bytes> getBlockRLP(int64_t _i);
    std::shared_ptr<dev::bytes> getBlockRLP(dev::h256 const& _blockHash, int64_t _blockNumber);
    int64_t obtainNumber();
    void writeNumber(const dev::eth::Block& block,
        std::shared_ptr<dev::blockverifier::ExecutiveContext> context);
    void writeTotalTransactionCount(const dev::eth::Block& block,
        std::shared_ptr<dev::blockverifier::ExecutiveContext> context);
    void writeTxToBlock(const dev::eth::Block& block,
        std::shared_ptr<dev::blockverifier::ExecutiveContext> context);
    void writeNumber2Hash(const dev::eth::Block& block,
        std::shared_ptr<dev::blockverifier::ExecutiveContext> context);
    void writeHash2Block(
        dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext> context);

    bool isBlockShouldCommit(int64_t const& _blockNumber);

    void parseMerkleMap(
        std::shared_ptr<std::map<std::string, std::vector<std::string>>> parent2ChildList,
        Child2ParentMap& child2Parent);

    void getMerkleProof(dev::bytes const& _txHash,
        const std::map<std::string, std::vector<std::string>>& parent2ChildList,
        const Child2ParentMap& child2Parent,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>& merkleProof);

    dev::bytes getHashNeed2Proof(uint32_t index, const dev::bytes& data);
    bool getBlockAndIndexByTxHash(const dev::h256& _txHash,
        std::pair<std::shared_ptr<dev::eth::Block>, std::string>& blockInfoWithTxIndex);

    dev::storage::Storage::Ptr m_stateStorage;
    std::mutex commitMutex;
    std::shared_ptr<dev::executive::StateFactoryInterface> m_stateFactory;

    dev::h512s getNodeListByType(int64_t num, std::string const& type);
    mutable SharedMutex m_nodeListMutex;
    dev::h512s m_sealerList;
    dev::h512s m_observerList;
    int64_t m_cacheNumBySealer = -1;
    int64_t m_cacheNumByObserver = -1;

    struct SystemConfigRecord
    {
        std::string value;
        dev::eth::BlockNumber enableNumber;
        int64_t curBlockNum = -1;  // at which block gets the configuration value
        SystemConfigRecord(std::string const& _value, dev::eth::BlockNumber const& _enableNumber,
            int64_t const& _num)
          : value(_value), enableNumber(_enableNumber), curBlockNum(_num){};
    };
    std::map<std::string, SystemConfigRecord> m_systemConfigRecord;
    mutable SharedMutex m_systemConfigMutex;
    BlockCache m_blockCache;

    /// cache the block number
    mutable SharedMutex m_blockNumberMutex;
    int64_t m_blockNumber = -1;

    dev::storage::TableFactoryFactory::Ptr m_tableFactoryFactory;

    std::pair<dev::eth::BlockNumber,
        std::shared_ptr<std::map<std::string, std::vector<std::string>>>>
        m_transactionWithProof = std::make_pair(0, nullptr);

    mutable SharedMutex m_transactionWithProofMutex;

    std::pair<dev::eth::BlockNumber,
        std::shared_ptr<std::map<std::string, std::vector<std::string>>>>
        m_receiptWithProof = std::make_pair(0, nullptr);
    mutable SharedMutex m_receiptWithProofMutex;

    std::pair<dev::eth::BlockNumber, std::shared_ptr<Child2ParentMap>> m_receiptChild2ParentCache;
    mutable SharedMutex x_receiptChild2ParentCache;

    std::pair<dev::eth::BlockNumber, std::shared_ptr<Child2ParentMap>> m_txsChild2ParentCache;
    mutable SharedMutex x_txsChild2ParentCache;

    bool m_enableHexBlock = false;
    dev::ThreadPool::Ptr m_destructorThread;
};
}  // namespace blockchain
}  // namespace dev
