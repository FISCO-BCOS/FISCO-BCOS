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

#include <libethcore/Block.h>
#include <libethcore/Common.h>
#include <libethcore/Protocol.h>
#include <libethcore/Transaction.h>
#include <libethcore/TransactionReceipt.h>
#include <libexecutive/StateFactoryInterface.h>
#include <libledger/LedgerParam.h>
#include <libprecompiled/SystemConfigPrecompiled.h>
#include <libstorage/Common.h>
#include <libstorage/Storage.h>
#include <libstorage/Table.h>
#include <libstoragestate/StorageStateFactory.h>
#include <libutilities/Exceptions.h>
#include <libutilities/ThreadPool.h>
#include <boost/thread/shared_mutex.hpp>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

#define BLOCKCHAIN_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("BLOCKCHAIN")

namespace bcos
{
namespace blockverifier
{
class ExecutiveContext;
}  // namespace blockverifier
namespace blockchain
{
class BlockChainImp;

class BlockCache
{
public:
    BlockCache(){};
    std::shared_ptr<bcos::eth::Block> add(std::shared_ptr<bcos::eth::Block> _block);
    std::pair<std::shared_ptr<bcos::eth::Block>, bcos::h256> get(h256 const& _hash);

    void setDestructorThread(bcos::ThreadPool::Ptr _destructorThread)
    {
        m_destructorThread = _destructorThread;
    }

private:
    mutable boost::shared_mutex m_sharedMutex;
    mutable std::map<bcos::h256, std::shared_ptr<bcos::eth::Block>> m_blockCache;
    mutable std::deque<bcos::h256> m_blockCacheFIFO;  // insert queue log for m_blockCache
    const unsigned c_blockCacheSize = 10;             // m_blockCache size, default set 10
    // used to destructor time-consuming, large memory objects
    bcos::ThreadPool::Ptr m_destructorThread;
};
DERIVE_BCOS_EXCEPTION(OpenSysTableFailed);

using Parent2ChildListMap = std::map<std::string, std::vector<std::string>>;
using Child2ParentMap = tbb::concurrent_unordered_map<std::string, std::string>;
using BlockHeaderInfo =
    std::pair<std::shared_ptr<bcos::eth::BlockHeader>, bcos::eth::Block::SigListPtrType>;
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
    bcos::h256 numberHash(int64_t _i) override;
    bcos::eth::Transaction::Ptr getTxByHash(bcos::h256 const& _txHash) override;
    bcos::eth::LocalisedTransaction::Ptr getLocalisedTxByHash(bcos::h256 const& _txHash) override;
    bcos::eth::TransactionReceipt::Ptr getTransactionReceiptByHash(
        bcos::h256 const& _txHash) override;
    virtual bcos::eth::LocalisedTransactionReceipt::Ptr getLocalisedTxReceiptByHash(
        bcos::h256 const& _txHash) override;
    std::shared_ptr<bcos::eth::Block> getBlockByHash(
        bcos::h256 const& _blockHash, int64_t _blockNumber = -1) override;
    std::shared_ptr<bcos::eth::Block> getBlockByNumber(int64_t _i) override;
    std::shared_ptr<bcos::bytes> getBlockRLPByNumber(int64_t _i) override;
    CommitResult commitBlock(std::shared_ptr<bcos::eth::Block> block,
        std::shared_ptr<bcos::blockverifier::ExecutiveContext> context) override;

    virtual void setStateStorage(bcos::storage::Storage::Ptr stateStorage);
    virtual void setStateFactory(bcos::executive::StateFactoryInterface::Ptr _stateFactory);
    virtual std::shared_ptr<bcos::storage::TableFactory> getMemoryTableFactory(int64_t num = 0);

    // When there is no genesis block, write relevant configuration items to the genesis block and
    // system table; when there is a genesis block, load immutable configuration information from
    // the genesis block
    bool checkAndBuildGenesisBlock(std::shared_ptr<bcos::ledger::LedgerParamInterface> _initParam,
        bool _shouldBuild = true) override;

    std::pair<int64_t, int64_t> totalTransactionCount() override;
    std::pair<int64_t, int64_t> totalFailedTransactionCount() override;
    bcos::bytes getCode(bcos::Address _address) override;

    bcos::h512s sealerList() override;
    bcos::h512s observerList() override;
    // get workingSealer list: type == NODE_TYPE_WORKING_SEALER
    bcos::h512s workingSealerList() override;
    // get pending list: type ==  NODE_TYPE_SEALER
    bcos::h512s pendingSealerList() override;

    std::string getSystemConfigByKey(std::string const& key, int64_t num = -1) override;

    std::shared_ptr<std::vector<bcos::eth::NonceKeyType>> getNonces(int64_t _blockNumber) override;

    std::pair<std::string, bcos::eth::BlockNumber> getSystemConfigInfoByKey(
        std::string const& _key, int64_t const& _num = -1) override;

    void setTableFactoryFactory(bcos::storage::TableFactoryFactory::Ptr tableFactoryFactory)
    {
        m_tableFactoryFactory = tableFactoryFactory;
    }

    std::pair<bcos::eth::LocalisedTransaction::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionByHashWithProof(bcos::h256 const& _txHash) override;


    std::pair<bcos::eth::LocalisedTransactionReceipt::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionReceiptByHashWithProof(
        bcos::h256 const& _txHash, bcos::eth::LocalisedTransaction& transaction) override;

    void setEnableHexBlock(bool const& _enableHexBlock) { m_enableHexBlock = _enableHexBlock; }

    std::shared_ptr<MerkleProofType> getTransactionReceiptProof(
        bcos::eth::Block::Ptr _block, uint64_t const& _index) override;

    std::shared_ptr<MerkleProofType> getTransactionProof(
        bcos::eth::Block::Ptr _block, uint64_t const& _index) override;

    std::shared_ptr<BlockHeaderInfo> getBlockHeaderInfo(int64_t _blockNumber) override;
    std::shared_ptr<BlockHeaderInfo> getBlockHeaderInfoByHash(
        bcos::h256 const& _blockHash) override;

private:
    std::shared_ptr<BlockHeaderInfo> getBlockHeaderFromBlock(bcos::eth::Block::Ptr _block);

    // Randomly select epochSealerSize nodes from workingList as workingSealer and write them into
    // the system table Only used in vrf rpbft consensus type
    virtual void initGenesisWorkingSealers(bcos::storage::Table::Ptr _consTable,
        std::shared_ptr<bcos::ledger::LedgerParamInterface> _initParam);

    virtual void initGensisConsensusInfoByNodeType(bcos::storage::Table::Ptr _consTable,
        std::string const& _nodeType, bcos::h512s const& _nodeList, int64_t _nodeNum = -1,
        bool _update = false);

    bcos::h512s getNodeList(bcos::eth::BlockNumber& _cachedNumber, bcos::h512s& _cachedNodeList,
        SharedMutex& _mutex, std::string const& _nodeListType);

    std::shared_ptr<Parent2ChildListMap> getParent2ChildListByReceiptProofCache(
        bcos::eth::Block::Ptr _block);
    std::shared_ptr<Parent2ChildListMap> getParent2ChildListByTxsProofCache(
        bcos::eth::Block::Ptr _block);

    std::shared_ptr<Child2ParentMap> getChild2ParentCacheByReceipt(
        std::shared_ptr<Parent2ChildListMap> _parent2ChildList, bcos::eth::Block::Ptr _block);
    std::shared_ptr<Child2ParentMap> getChild2ParentCacheByTransaction(
        std::shared_ptr<Parent2ChildListMap> _parent2Child, bcos::eth::Block::Ptr _block);

    std::shared_ptr<Child2ParentMap> getChild2ParentCache(SharedMutex& _mutex,
        std::pair<bcos::eth::BlockNumber, std::shared_ptr<Child2ParentMap>>& _cache,
        std::shared_ptr<Parent2ChildListMap> _parent2Child, bcos::eth::Block::Ptr _block);

    void initSystemConfig(
        bcos::storage::Table::Ptr _tb, std::string const& _key, std::string const& _value);

    std::shared_ptr<bcos::eth::Block> decodeBlock(bcos::storage::Entry::ConstPtr _entry);
    std::shared_ptr<bcos::bytes> getDataBytes(
        bcos::storage::Entry::ConstPtr _entry, std::string const& _fieldName);

    void writeBytesToField(std::shared_ptr<bcos::bytes> _data, bcos::storage::Entry::Ptr _entry,
        std::string const& _fieldName = bcos::storage::SYS_VALUE);
    void writeBlockToField(bcos::eth::Block const& _block, bcos::storage::Entry::Ptr _entry);

    std::shared_ptr<bcos::eth::Block> getBlock(int64_t _blockNumber);
    std::shared_ptr<bcos::eth::Block> getBlock(
        bcos::h256 const& _blockHash, int64_t _blockNumber = -1);
    std::shared_ptr<bcos::bytes> getBlockRLP(int64_t _i);
    std::shared_ptr<bcos::bytes> getBlockRLP(bcos::h256 const& _blockHash, int64_t _blockNumber);
    int64_t obtainNumber();
    void writeNumber(const bcos::eth::Block& block,
        std::shared_ptr<bcos::blockverifier::ExecutiveContext> context);
    void writeTotalTransactionCount(const bcos::eth::Block& block,
        std::shared_ptr<bcos::blockverifier::ExecutiveContext> context);
    void writeTxToBlock(const bcos::eth::Block& block,
        std::shared_ptr<bcos::blockverifier::ExecutiveContext> context);
    void writeNumber2Hash(const bcos::eth::Block& block,
        std::shared_ptr<bcos::blockverifier::ExecutiveContext> context);
    void writeHash2Block(
        bcos::eth::Block& block, std::shared_ptr<bcos::blockverifier::ExecutiveContext> context);
    void writeHash2BlockHeader(
        bcos::eth::Block& _block, std::shared_ptr<bcos::blockverifier::ExecutiveContext> _context);

    bool isBlockShouldCommit(int64_t const& _blockNumber);

    void parseMerkleMap(
        std::shared_ptr<std::map<std::string, std::vector<std::string>>> parent2ChildList,
        Child2ParentMap& child2Parent);

    void getMerkleProof(bcos::bytes const& _txHash,
        const std::map<std::string, std::vector<std::string>>& parent2ChildList,
        const Child2ParentMap& child2Parent,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>& merkleProof);

    bcos::bytes getHashNeed2Proof(uint32_t index, const bcos::bytes& data);
    bool getBlockAndIndexByTxHash(const bcos::h256& _txHash,
        std::pair<std::shared_ptr<bcos::eth::Block>, std::string>& blockInfoWithTxIndex);

    bcos::storage::Storage::Ptr m_stateStorage;
    std::mutex commitMutex;
    std::shared_ptr<bcos::executive::StateFactoryInterface> m_stateFactory;

    bcos::h512s getNodeListByType(int64_t num, std::string const& type);
    mutable SharedMutex m_nodeListMutex;
    bcos::h512s m_sealerList;
    bcos::h512s m_observerList;
    bcos::h512s m_workingSealerList;

    int64_t m_cacheNumByWorkingSealer = -1;
    int64_t m_cacheNumBySealer = -1;
    int64_t m_cacheNumByObserver = -1;


    struct SystemConfigRecord
    {
        std::string value;
        bcos::eth::BlockNumber enableNumber;
        int64_t curBlockNum = -1;  // at which block gets the configuration value
        SystemConfigRecord(std::string const& _value, bcos::eth::BlockNumber const& _enableNumber,
            int64_t const& _num)
          : value(_value), enableNumber(_enableNumber), curBlockNum(_num){};
    };
    std::map<std::string, SystemConfigRecord> m_systemConfigRecord;
    mutable SharedMutex m_systemConfigMutex;
    BlockCache m_blockCache;

    /// cache the block number
    mutable SharedMutex m_blockNumberMutex;
    int64_t m_blockNumber = -1;

    bcos::storage::TableFactoryFactory::Ptr m_tableFactoryFactory;

    std::pair<bcos::eth::BlockNumber,
        std::shared_ptr<std::map<std::string, std::vector<std::string>>>>
        m_transactionWithProof = std::make_pair(0, nullptr);

    mutable SharedMutex m_transactionWithProofMutex;

    std::pair<bcos::eth::BlockNumber,
        std::shared_ptr<std::map<std::string, std::vector<std::string>>>>
        m_receiptWithProof = std::make_pair(0, nullptr);
    mutable SharedMutex m_receiptWithProofMutex;

    std::pair<bcos::eth::BlockNumber, std::shared_ptr<Child2ParentMap>> m_receiptChild2ParentCache;
    mutable SharedMutex x_receiptChild2ParentCache;

    std::pair<bcos::eth::BlockNumber, std::shared_ptr<Child2ParentMap>> m_txsChild2ParentCache;
    mutable SharedMutex x_txsChild2ParentCache;

    bool m_enableHexBlock = false;
    bcos::ThreadPool::Ptr m_destructorThread;
};
}  // namespace blockchain
}  // namespace bcos
