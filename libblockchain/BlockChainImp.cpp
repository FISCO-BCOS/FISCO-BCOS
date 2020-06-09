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
 * @brief : BlockChainImp
 * @author: mingzhenliu
 * @date: 2018-09-21
 */

#include "BlockChainImp.h"
#include "libdevcrypto/CryptoInterface.h"
#include "tbb/parallel_for_each.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/CommonData.h>
#include <libethcore/Block.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <libprecompiled/ConsensusPrecompiled.h>
#include <libstorage/StorageException.h>
#include <libstorage/Table.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_invoke.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>
#include <csignal>
#include <string>
#include <utility>
#include <vector>

using namespace dev;
using namespace std;
using namespace dev::eth;
using namespace dev::blockchain;
using namespace dev::storage;
using namespace dev::blockverifier;
using namespace dev::executive;
using namespace dev::precompiled;

using boost::lexical_cast;

std::shared_ptr<Block> BlockCache::add(std::shared_ptr<Block> _block)
{
    {
        WriteGuard guard(m_sharedMutex);
        if (m_blockCache.size() > c_blockCacheSize)
        {
            BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[add]Block cache full, start to remove old item...");
            auto firstHash = m_blockCacheFIFO.front();
            m_blockCacheFIFO.pop_front();
            auto removedBlock = m_blockCache[firstHash];

            m_blockCache.erase(firstHash);
            // Destruct the block in m_destructorThread
            HolderForDestructor<Block> holder(std::move(removedBlock));
            m_destructorThread->enqueue(std::move(holder));

            // in case something unexcept error
            if (m_blockCache.size() > c_blockCacheSize)
            {
                // meet error, cache and cacheFIFO not sync, clear the cache
                m_blockCache.clear();
                m_blockCacheFIFO.clear();
            }
        }

        auto blockHash = _block->blockHeader().hash();
        auto block = _block;
        m_blockCache.insert(std::make_pair(blockHash, block));
        // add hashindex to the blockCache queue, use to remove first element when the cache is full
        m_blockCacheFIFO.push_back(blockHash);

        return block;
    }
}

std::pair<std::shared_ptr<Block>, h256> BlockCache::get(h256 const& _hash)
{
    {
        ReadGuard guard(m_sharedMutex);

        auto it = m_blockCache.find(_hash);
        if (it == m_blockCache.end())
        {
            return std::make_pair(nullptr, h256(0));
        }

        return std::make_pair(it->second, _hash);
    }

    return std::make_pair(nullptr, h256(0));  // just make compiler happy
}

void BlockChainImp::setStateStorage(Storage::Ptr stateStorage)
{
    m_stateStorage = stateStorage;
}

void BlockChainImp::setStateFactory(StateFactoryInterface::Ptr _stateFactory)
{
    m_stateFactory = _stateFactory;
}

shared_ptr<TableFactory> BlockChainImp::getMemoryTableFactory(int64_t num)
{
    auto memoryTableFactory = m_tableFactoryFactory->newTableFactory(dev::h256(), num);
    return memoryTableFactory;
}

std::shared_ptr<Block> BlockChainImp::getBlock(int64_t _blockNumber)
{
    /// the future block
    if (_blockNumber > number())
    {
        return nullptr;
    }
    Table::Ptr tb = getMemoryTableFactory(_blockNumber)->openTable(SYS_NUMBER_2_HASH);
    if (tb)
    {
        auto entries = tb->select(lexical_cast<std::string>(_blockNumber), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            h256 blockHash = h256((entry->getField(SYS_VALUE)));
            return getBlock(blockHash, _blockNumber);
        }
    }

    BLOCKCHAIN_LOG(WARNING) << LOG_DESC("[getBlock]Can't find block")
                            << LOG_KV("number", _blockNumber);
    return nullptr;
}

std::shared_ptr<Block> BlockChainImp::getBlock(dev::h256 const& _blockHash, int64_t _blockNumber)
{
    auto start_time = utcTime();
    auto record_time = utcTime();
    auto cachedBlock = m_blockCache.get(_blockHash);
    auto getCache_time_cost = utcTime() - record_time;
    record_time = utcTime();

    if (bool(cachedBlock.first))
    {
        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getBlock]Cache hit, read from cache")
                              << LOG_KV("blockNumber", _blockNumber)
                              << LOG_KV("hash", _blockHash.abridged());
        return cachedBlock.first;
    }
    else
    {
        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getBlock]Cache missed, read from storage")
                              << LOG_KV("blockNumber", _blockNumber);
        ;
        Table::Ptr tb = getMemoryTableFactory(_blockNumber)->openTable(SYS_HASH_2_BLOCK);
        auto openTable_time_cost = utcTime() - record_time;
        record_time = utcTime();
        if (tb)
        {
            auto entries = tb->select(_blockHash.hex(), tb->newCondition());
            auto select_time_cost = utcTime() - record_time;
            record_time = utcTime();
            if (entries->size() > 0)
            {
                auto entry = entries->get(0);

                record_time = utcTime();
                // use binary block since v2.2.0
                auto block = decodeBlock(entry);

                auto constructBlock_time_cost = utcTime() - record_time;
                record_time = utcTime();

                BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getBlock]Write to cache");
                auto blockPtr = m_blockCache.add(block);
                auto addCache_time_cost = utcTime() - record_time;
                BLOCKCHAIN_LOG(DEBUG) << LOG_DESC("Get block from db")
                                      << LOG_KV("getCacheTimeCost", getCache_time_cost)
                                      << LOG_KV("openTableTimeCost", openTable_time_cost)
                                      << LOG_KV("selectTimeCost", select_time_cost)
                                      << LOG_KV("constructBlockTimeCost", constructBlock_time_cost)
                                      << LOG_KV("addCacheTimeCost", addCache_time_cost)
                                      << LOG_KV("totalTimeCost", utcTime() - start_time);
                return blockPtr;
            }
        }

        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getBlock]Can't find the block")
                              << LOG_KV("blockNumber", _blockNumber)
                              << LOG_KV("blockHash", _blockHash);
        return nullptr;
    }
}

std::shared_ptr<bytes> BlockChainImp::getBlockRLP(int64_t _i)
{
    /// the future block
    if (_i > number())
    {
        return nullptr;
    }
    string blockHash = "";
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_NUMBER_2_HASH);
    if (tb)
    {
        auto entries = tb->select(lexical_cast<std::string>(_i), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            h256 blockHash = h256((entry->getField(SYS_VALUE)));
            return getBlockRLP(blockHash, _i);
        }
    }

    BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getBlockRLP]Can't find block") << LOG_KV("height", _i);
    return nullptr;
}

std::shared_ptr<bytes> BlockChainImp::getBlockRLP(dev::h256 const& _blockHash, int64_t _blockNumber)
{
    auto start_time = utcTime();
    auto record_time = utcTime();
    auto cachedBlock = m_blockCache.get(_blockHash);
    auto getCache_time_cost = utcTime() - record_time;
    record_time = utcTime();

    if (bool(cachedBlock.first))
    {
        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getBlockRLP]Cache hit, read from cache");
        std::shared_ptr<bytes> blockRLP = cachedBlock.first->rlpP();
        BLOCKCHAIN_LOG(DEBUG) << LOG_DESC("Get block RLP from cache")
                              << LOG_KV("getCacheTimeCost", getCache_time_cost)
                              << LOG_KV("totalTimeCost", utcTime() - start_time);
        return blockRLP;
    }
    else
    {
        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getBlockRLP]Cache missed, read from storage");
        Table::Ptr tb = getMemoryTableFactory(_blockNumber)->openTable(SYS_HASH_2_BLOCK);
        auto openTable_time_cost = utcTime() - record_time;
        record_time = utcTime();
        if (tb)
        {
            auto entries = tb->select(_blockHash.hex(), tb->newCondition());
            auto select_time_cost = utcTime() - record_time;
            record_time = utcTime();
            if (entries->size() > 0)
            {
                auto entry = entries->get(0);

                record_time = utcTime();
                auto blockRLP = getDataBytes(entry, SYS_VALUE);
                auto blockRLP_time_cost = utcTime() - record_time;

                BLOCKCHAIN_LOG(DEBUG) << LOG_DESC("Get block RLP from db")
                                      << LOG_KV("getCacheTimeCost", getCache_time_cost)
                                      << LOG_KV("openTableTimeCost", openTable_time_cost)
                                      << LOG_KV("selectTimeCost", select_time_cost)
                                      << LOG_KV("constructblockRLPTimeCost", blockRLP_time_cost)
                                      << LOG_KV("totalTimeCost", utcTime() - start_time);
                return blockRLP;
            }
        }

        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getBlock]Can't find the block")
                              << LOG_KV("blockNumber", _blockNumber)
                              << LOG_KV("blockHash", _blockHash);
        return nullptr;
    }
}

int64_t BlockChainImp::number()
{
    UpgradableGuard ul(m_blockNumberMutex);
    if (m_blockNumber == -1)
    {
        int64_t num = obtainNumber();
        UpgradeGuard l(ul);
        m_blockNumber = num;
    }
    return m_blockNumber;
}

int64_t BlockChainImp::obtainNumber()
{
    int64_t num = 0;
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_CURRENT_STATE, false);
    if (tb)
    {
        auto entries = tb->select(SYS_KEY_CURRENT_NUMBER, tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            std::string currentNumber = entry->getField(SYS_VALUE);
            num = lexical_cast<int64_t>(currentNumber);
        }
    }
    return num;
}

std::shared_ptr<std::vector<dev::eth::NonceKeyType>> BlockChainImp::getNonces(int64_t _blockNumber)
{
    if (_blockNumber > number())
    {
        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("getNonces failed for invalid block number")
                              << LOG_KV("invalidNumber", _blockNumber)
                              << LOG_KV("blockNumber", m_blockNumber);
        return std::shared_ptr<std::vector<dev::eth::NonceKeyType>>();
    }
    BLOCKCHAIN_LOG(DEBUG) << LOG_DESC("getNonces") << LOG_KV("blkNumber", _blockNumber);
    Table::Ptr tb = getMemoryTableFactory(_blockNumber)->openTable(SYS_BLOCK_2_NONCES);
    if (tb)
    {
        auto entries = tb->select(lexical_cast<std::string>(_blockNumber), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            auto nonceRLPData = getDataBytes(entry, SYS_VALUE);
            RLP rlp(*nonceRLPData);
            return std::make_shared<std::vector<dev::eth::NonceKeyType>>(
                rlp.toVector<dev::eth::NonceKeyType>());
        }
    }

    return std::make_shared<std::vector<dev::eth::NonceKeyType>>();
}

std::pair<int64_t, int64_t> BlockChainImp::totalTransactionCount()
{
    int64_t count = 0;
    int64_t number = 0;
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_CURRENT_STATE, false);
    if (tb)
    {
        auto entries = tb->select(SYS_KEY_TOTAL_TRANSACTION_COUNT, tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            std::string strCount = entry->getField(SYS_VALUE);
            count = lexical_cast<int64_t>(strCount);
            number = entry->num();
            if (g_BCOSConfig.version() <= RC2_VERSION)
            {
                std::string strNumber = entry->getField(NUM_FIELD);
                if (!strNumber.empty())
                {
                    // Bugfix: rc2 leveldb has NUM_FIELD field but rc2 amdb is not
                    number = lexical_cast<int64_t>(strNumber);
                }
            }
        }
    }
    return std::make_pair(count, number);
}

std::pair<int64_t, int64_t> BlockChainImp::totalFailedTransactionCount()
{
    int64_t count = 0;
    int64_t number = 0;
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_CURRENT_STATE, false);
    if (tb)
    {
        auto entries = tb->select(SYS_KEY_TOTAL_FAILED_TRANSACTION, tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            std::string strCount = entry->getField(SYS_VALUE);
            count = lexical_cast<int64_t>(strCount);
            number = entry->num();
        }
    }
    return std::make_pair(count, number);
}

bytes BlockChainImp::getCode(Address _address)
{
    bytes ret;
    int64_t num = number();
    auto block = getBlockByNumber(num);

    if (!block)
    {
        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getCode]Can't find the block, return empty code");
        return ret;
    }

    auto stateRoot = block->header().stateRoot();
    auto memoryFactory = getMemoryTableFactory();

    auto state = m_stateFactory->getState(stateRoot, memoryFactory);
    auto code = state->code(_address);
    return code;
}

h256 BlockChainImp::numberHash(int64_t _i)
{
    string numberHash = "";
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_NUMBER_2_HASH, false);
    if (tb)
    {
        auto entries = tb->select(lexical_cast<std::string>(_i), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            numberHash = entry->getField(SYS_VALUE);
        }
    }
    return h256(numberHash);
}

std::shared_ptr<Block> BlockChainImp::getBlockByHash(h256 const& _blockHash, int64_t _blockNumber)
{
    auto block = getBlock(_blockHash, _blockNumber);
    if (bool(block))
    {
        return block;
    }
    else
    {
        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getBlockByHash]Can't find the block, return nullptr");
        return nullptr;
    }
}

void BlockChainImp::initGenesisWorkingSealers(
    dev::storage::Table::Ptr _consTable, GenesisBlockParam& _initParam)
{
    // only used for vrf based rPBFT
    if (dev::stringCmpIgnoreCase(_initParam.consensusType, "vrf_rpbft") != 0)
    {
        return;
    }
    // TODO: select the genesis working sealers randomly according to genesis hash
    std::sort(_initParam.sealerList.begin(), _initParam.sealerList.end());
    int64_t sealersSize = _initParam.sealerList.size();
    auto selectedNum = std::min(_initParam.rpbftEpochSize, sealersSize);
    // output workingSealers
    std::string workingSealers;
    for (int64_t i = 0; i < selectedNum; i++)
    {
        workingSealers += (_initParam.sealerList[i]).abridged() + ",";
    }
    BLOCKCHAIN_LOG(INFO) << LOG_DESC("initGenesisWorkingSealers")
                         << LOG_KV("workingSealerNum", selectedNum)
                         << LOG_KV("workingSealers", workingSealers);
    // update selected sealers into workingSealers
    initGensisConsensusInfoByNodeType(
        _consTable, NODE_TYPE_WORKING_SEALER, _initParam.sealerList, selectedNum, true);
}

bool BlockChainImp::checkAndBuildGenesisBlock(GenesisBlockParam& initParam, bool _shouldBuild)
{
    BLOCKCHAIN_LOG(INFO) << LOG_DESC("[#checkAndBuildGenesisBlock]")
                         << LOG_KV("shouldBuild", _shouldBuild);
    std::shared_ptr<Block> block = getBlockByNumber(0);
    if (block == nullptr && !_shouldBuild)
    {
        BLOCKCHAIN_LOG(FATAL) << "Can't find the genesis block";
    }
    else if (block == nullptr && _shouldBuild)
    {
        block = std::make_shared<Block>();
        /// modification 2019.3.20: set timestamp to block header
        block->setEmptyBlock(initParam.timeStamp);
        block->header().appendExtraDataArray(asBytes(initParam.groupMark));
        shared_ptr<TableFactory> mtb = getMemoryTableFactory();
        Table::Ptr tb = mtb->openTable(SYS_NUMBER_2_HASH, false);
        if (tb)
        {
            Entry::Ptr entry = std::make_shared<Entry>();
            entry->setField("number", lexical_cast<std::string>(block->blockHeader().number()));
            entry->setField(SYS_VALUE, block->blockHeader().hash().hex());
            tb->insert(lexical_cast<std::string>(block->blockHeader().number()), entry);
        }

        tb = mtb->openTable(SYS_CONFIG);
        if (tb)
        {
            // init for tx_count_limit
            initSystemConfig(tb, SYSTEM_KEY_TX_COUNT_LIMIT,
                boost::lexical_cast<std::string>(initParam.txCountLimit));

            // init for tx_gas_limit
            initSystemConfig(tb, SYSTEM_KEY_TX_GAS_LIMIT,
                boost::lexical_cast<std::string>(initParam.txGasLimit));
            // init configurations for RPBFT
            if (dev::stringCmpIgnoreCase(initParam.consensusType, "rpbft") == 0 ||
                dev::stringCmpIgnoreCase(initParam.consensusType, "vrf_rpbft") == 0)
            {
                // init rotating-epoch-size
                initSystemConfig(tb, SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM,
                    boost::lexical_cast<std::string>(initParam.rpbftEpochSize));
                initSystemConfig(tb, SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM,
                    boost::lexical_cast<std::string>(initParam.rpbftRotatingInterval));
            }
        }

        tb = mtb->openTable(SYS_CONSENSUS);
        if (tb)
        {
            initGensisConsensusInfoByNodeType(tb, NODE_TYPE_SEALER, initParam.sealerList);
            initGensisConsensusInfoByNodeType(tb, NODE_TYPE_OBSERVER, initParam.observerList);
            initGenesisWorkingSealers(tb, initParam);
        }

        tb = mtb->openTable(SYS_HASH_2_BLOCK, false);

        auto entry = std::make_shared<Entry>();
        writeBlockToField(*block, entry);

        tb->insert(block->blockHeader().hash().hex(), entry);

        tb = mtb->openTable(SYS_CURRENT_STATE, false);
        entry = tb->newEntry();
        entry->setField(SYS_VALUE, "0");
        entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
        tb->insert(SYS_KEY_CURRENT_NUMBER, entry);

        mtb->commitDB(block->blockHeader().hash(), block->blockHeader().number());
        {
            WriteGuard l(m_blockNumberMutex);
            m_blockNumber = 0;
        }
        BLOCKCHAIN_LOG(INFO) << LOG_DESC("[#checkAndBuildGenesisBlock]Insert the 0th block");
    }
    else
    {
        std::string extraData = asString(block->header().extraData(0));
        /// compare() return 0 means equal!
        /// If not equal, only print warning, willnot kill process.
        if (!initParam.groupMark.compare(extraData))
        {
            BLOCKCHAIN_LOG(INFO) << LOG_DESC(
                "[#checkAndBuildGenesisBlock]Already have the 0th block, 0th groupMark is "
                "equal to file groupMark.");
            return true;
        }
        else
        {
            BLOCKCHAIN_LOG(WARNING) << LOG_DESC(
                                           "[#checkAndBuildGenesisBlock]Already have the 0th "
                                           "block, 0th group mark is not equal to file groupMark")
                                    << LOG_KV("0thGroupMark:", extraData)
                                    << LOG_KV("fileGroupMark", initParam.groupMark);

            // maybe consensusType/storageType/stateType diff, then update config
            std::vector<std::string> s;
            try
            {
                boost::split(s, extraData, boost::is_any_of("-"), boost::token_compress_on);
                initParam.consensusType = s[2];
                if (g_BCOSConfig.version() <= RC2_VERSION)
                {
                    initParam.storageType = s[3];
                    initParam.stateType = s[4];
                }
                else
                {
                    initParam.stateType = s[3];
                }
                if (g_BCOSConfig.version() >= V2_4_0)
                {
                    initParam.evmFlags = boost::lexical_cast<VMFlagType>(s[4]);
                }
                BLOCKCHAIN_LOG(INFO) << LOG_BADGE("checkAndBuildGenesisBlock")
                                     << LOG_DESC("Load genesis config from extraData")
                                     << LOG_KV("stateType", initParam.stateType)
                                     << LOG_KV("evmFlags", initParam.evmFlags);
            }
            catch (std::exception& e)
            {
                BLOCKCHAIN_LOG(ERROR)
                    << LOG_DESC("[#checkAndBuildGenesisBlock]Parse groupMark faield")
                    << LOG_KV("EINFO", e.what());
            }
            return false;
        }
    }
    return true;
}

void BlockChainImp::initGensisConsensusInfoByNodeType(dev::storage::Table::Ptr _consTable,
    std::string const& _nodeType, dev::h512s const& _nodeList, int64_t _nodeNum, bool _update)
{
    int64_t initedNodeSize = _nodeNum;
    if (-1 == _nodeNum)
    {
        initedNodeSize = _nodeList.size();
    }
    for (int64_t i = 0; i < initedNodeSize; i++)
    {
        auto const& node = _nodeList[i];
        auto entry = std::make_shared<Entry>();
        entry->setField(PRI_COLUMN, PRI_KEY);
        entry->setField(NODE_TYPE, _nodeType);
        entry->setField(NODE_KEY_NODEID, dev::toHex(node));
        entry->setField(NODE_KEY_ENABLENUM, "0");
        if (_update)
        {
            auto condition = _consTable->newCondition();
            condition->EQ(NODE_KEY_NODEID, dev::toHex(node));
            _consTable->update(PRI_KEY, entry, condition);
        }
        else
        {
            _consTable->insert(PRI_KEY, entry);
        }
    }
}

// init system config
void BlockChainImp::initSystemConfig(
    Table::Ptr _tb, std::string const& _key, std::string const& _value)
{
    Entry::Ptr entry = std::make_shared<Entry>();
    entry->setField(SYSTEM_CONFIG_KEY, _key);
    entry->setField(SYSTEM_CONFIG_VALUE, _value);
    entry->setField(SYSTEM_CONFIG_ENABLENUM, "0");
    _tb->insert(_key, entry);
}

dev::h512s BlockChainImp::getNodeListByType(int64_t blockNumber, std::string const& type)
{
    dev::h512s list;
    try
    {
        Table::Ptr tb = getMemoryTableFactory()->openTable(storage::SYS_CONSENSUS);
        if (!tb)
        {
            BLOCKCHAIN_LOG(ERROR) << LOG_DESC("[#getNodeListByType]Open table error");
            return list;
        }
        list = dev::precompiled::getNodeListByType(tb, blockNumber, type);
    }
    catch (std::exception& e)
    {
        BLOCKCHAIN_LOG(ERROR) << LOG_DESC("[#getNodeListByType]Failed")
                              << LOG_KV("EINFO", boost::diagnostic_information(e));
    }

    std::stringstream s;
    s << "[#getNodeListByType] " << type << ":";
    for (dev::h512 node : list)
        s << toJS(node) << ",";
    BLOCKCHAIN_LOG(TRACE) << LOG_DESC(s.str());

    return list;
}

// return the working sealer
dev::h512s BlockChainImp::workingSealerList()
{
    return getNodeList(
        m_cacheNumByWorkingSealer, m_workingSealerList, m_nodeListMutex, NODE_TYPE_WORKING_SEALER);
}

dev::h512s BlockChainImp::pendingSealerList()
{
    return getNodeList(m_cacheNumBySealer, m_sealerList, m_nodeListMutex, NODE_TYPE_SEALER);
}

// Union of type=NODE_TYPE_WORKING_SEALER and type=NODE_TYPE_SEALER
dev::h512s BlockChainImp::sealerList()
{
    return (workingSealerList() + pendingSealerList());
}

dev::h512s BlockChainImp::observerList()
{
    return getNodeList(m_cacheNumByObserver, m_observerList, m_nodeListMutex, NODE_TYPE_OBSERVER);
}

// TODO: Use pointers as return values to reduce copy overhead
dev::h512s BlockChainImp::getNodeList(dev::eth::BlockNumber& _cachedNumber,
    dev::h512s& _cachedNodeList, SharedMutex& _mutex, std::string const& _nodeListType)
{
    auto blockNumber = number();
    UpgradableGuard l(_mutex);
    // hit the cache
    if (_cachedNumber == blockNumber)
    {
        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("getNodeList: hit the cache")
                              << LOG_KV("type", _nodeListType)
                              << LOG_KV("size", _cachedNodeList.size());
        return _cachedNodeList;
    }
    // miss the cache
    auto nodeList = getNodeListByType(blockNumber, _nodeListType);
    UpgradeGuard ul(l);
    _cachedNumber = blockNumber;
    _cachedNodeList = nodeList;
    return nodeList;
}

std::string BlockChainImp::getSystemConfigByKey(std::string const& key, int64_t num)
{
    return getSystemConfigInfoByKey(key, num).first;
}

std::pair<std::string, BlockNumber> BlockChainImp::getSystemConfigInfoByKey(
    std::string const& key, int64_t const& num)
{
    // Different keys can go into the function
    // -1 means that the parameter is invalid and to obtain current block height
    // The param was reset at height number(), and takes effect in next block.
    // So we query the status of number() + 1.
    int64_t blockNumber = (-1 == num) ? number() + 1 : num;
    UpgradableGuard l(m_systemConfigMutex);
    auto it = m_systemConfigRecord.find(key);
    if (it != m_systemConfigRecord.end() && it->second.curBlockNum == blockNumber)
    {
        // get value from cache
        return std::make_pair(it->second.value, it->second.enableNumber);
    }

    auto result = std::make_shared<std::pair<std::string, BlockNumber>>(std::make_pair("", -1));
    // cannot find the system config key or need to update the value with different block height
    // get value from db
    try
    {
        Table::Ptr tb = getMemoryTableFactory()->openTable(storage::SYS_CONFIG);
        if (!tb)
        {
            BLOCKCHAIN_LOG(ERROR) << LOG_DESC("[#getSystemConfigByKey]Open table error");
            return *result;
        }
        result = dev::precompiled::getSysteConfigByKey(tb, key, blockNumber);
    }
    catch (std::exception& e)
    {
        BLOCKCHAIN_LOG(ERROR) << LOG_DESC("[#getSystemConfigByKey]Failed")
                              << LOG_KV("EINFO", boost::diagnostic_information(e));
        return *result;
    }

    // update cache
    {
        UpgradeGuard ul(l);
        SystemConfigRecord systemConfigRecord(result->first, result->second, blockNumber);
        if (it != m_systemConfigRecord.end())
        {
            it->second = systemConfigRecord;
        }
        else
        {
            m_systemConfigRecord.insert(
                std::pair<std::string, SystemConfigRecord>(key, systemConfigRecord));
        }
    }

    BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getSystemConfigByKey]Data in db") << LOG_KV("key", key)
                          << LOG_KV("value", result->first);
    return *result;
}

std::shared_ptr<Block> BlockChainImp::getBlockByNumber(int64_t _i)
{
    /// return directly if the blocknumber is invalid
    if (_i > number())
    {
        return nullptr;
    }
    auto block = getBlock(_i);
    if (bool(block))
    {
        return block;
    }
    else
    {
        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getBlockByNumber]Can't find block, return nullptr")
                              << LOG_KV("blockNumber", _i);
        return nullptr;
    }
}

std::shared_ptr<bytes> BlockChainImp::getBlockRLPByNumber(int64_t _i)
{
    /// return directly if the blocknumber is invalid
    if (_i > number())
    {
        return nullptr;
    }
    auto block = getBlockRLP(_i);
    if (bool(block))
    {
        return block;
    }
    else
    {
        BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getBlockRLPByNumber]Can't find block, return nullptr");
        return nullptr;
    }
}

Transaction::Ptr BlockChainImp::getTxByHash(dev::h256 const& _txHash)
{
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_TX_HASH_2_BLOCK, false, true);
    if (tb)
    {
        string strblock = "";
        string txIndex = "";
        auto entries = tb->select(_txHash.hex(), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            strblock = entry->getField(SYS_VALUE);
            txIndex = entry->getField("index");
            std::shared_ptr<Block> pblock = getBlockByNumber(lexical_cast<int64_t>(strblock));
            if (!pblock)
            {
                return std::make_shared<Transaction>();
            }
            auto txs = pblock->transactions();
            if (txs->size() > lexical_cast<uint>(txIndex))
            {
                return (*txs)[lexical_cast<uint>(txIndex)];
            }
        }
    }
    BLOCKCHAIN_LOG(TRACE) << LOG_DESC("[#getTxByHash]Can't find tx, return empty tx");
    return std::make_shared<Transaction>();
}

LocalisedTransaction::Ptr BlockChainImp::getLocalisedTxByHash(dev::h256 const& _txHash)
{
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_TX_HASH_2_BLOCK, false, true);
    if (tb)
    {
        string strblockhash = "";
        string txIndex = "";
        auto entries = tb->select(_txHash.hex(), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            strblockhash = entry->getField(SYS_VALUE);
            txIndex = entry->getField("index");
            std::shared_ptr<Block> pblock = getBlockByNumber(lexical_cast<int64_t>(strblockhash));
            if (!pblock)
            {
                return std::make_shared<LocalisedTransaction>(Transaction(), h256(0), -1, -1);
            }
            auto txs = pblock->transactions();
            if (txs->size() > lexical_cast<uint>(txIndex))
            {
                return std::make_shared<LocalisedTransaction>(
                    *((*txs)[lexical_cast<uint>(txIndex)]), pblock->headerHash(),
                    lexical_cast<unsigned>(txIndex), pblock->blockHeader().number());
            }
        }
    }
    BLOCKCHAIN_LOG(TRACE) << LOG_DESC(
        "[#getLocalisedTxByHash]Can't find tx, return empty localised tx");
    return std::make_shared<LocalisedTransaction>(Transaction(), h256(0), -1, -1);
}


bool BlockChainImp::getBlockAndIndexByTxHash(const dev::h256& _txHash,
    std::pair<std::shared_ptr<dev::eth::Block>, std::string>& blockInfoWithTxIndex)
{
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_TX_HASH_2_BLOCK, false, true);
    if (!tb)
    {
        BLOCKCHAIN_LOG(ERROR) << LOG_DESC("open table failed ") << LOG_KV("_txHash", _txHash.hex());
        return false;
    }
    auto entries = tb->select(_txHash.hex(), tb->newCondition());
    if (entries->size() == 0)
    {
        BLOCKCHAIN_LOG(ERROR) << LOG_DESC("record not exist int tx_hash_2_block")
                              << LOG_KV("_txHash", _txHash.hex());
        return false;
    }
    auto entry = entries->get(0);
    auto blockNumber = entry->getField(SYS_VALUE);
    auto txIndex = entry->getField("index");
    std::shared_ptr<Block> blockInfo = getBlockByNumber(lexical_cast<int64_t>(blockNumber));
    if (!blockInfo)
    {
        BLOCKCHAIN_LOG(ERROR) << LOG_DESC("open table failed ") << LOG_KV("_txHash", _txHash.hex());
        return false;
    }
    blockInfoWithTxIndex = std::make_pair(blockInfo, txIndex);
    return true;
}

std::pair<LocalisedTransaction::Ptr,
    std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
BlockChainImp::getTransactionByHashWithProof(dev::h256 const& _txHash)
{
    if (g_BCOSConfig.version() < V2_2_0)
    {
        BLOCKCHAIN_LOG(ERROR) << "getTransactionByHashWithProof only support after by v2.2.0";
        BOOST_THROW_EXCEPTION(
            MethodNotSupport() << errinfo_comment("method not support in this version"));
    }
    auto tx = std::make_shared<dev::eth::LocalisedTransaction>();
    std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> merkleProof;
    std::pair<std::shared_ptr<dev::eth::Block>, std::string> blockInfoWithTxIndex;
    if (!getBlockAndIndexByTxHash(_txHash, blockInfoWithTxIndex))
    {
        BLOCKCHAIN_LOG(ERROR) << LOG_DESC("get block info  failed")
                              << LOG_KV("_txHash", _txHash.hex());
        return std::make_pair(tx, merkleProof);
    }

    auto txIndex = blockInfoWithTxIndex.second;
    auto blockInfo = blockInfoWithTxIndex.first;
    auto txs = blockInfo->transactions();
    if (txs->size() <= lexical_cast<uint>(txIndex))
    {
        BLOCKCHAIN_LOG(ERROR) << LOG_DESC("txindex is invalidate ") << LOG_KV("txIndex", txIndex);
        return std::make_pair(tx, merkleProof);
    }
    tx = std::make_shared<LocalisedTransaction>(*((*txs)[lexical_cast<uint>(txIndex)]),
        blockInfo->headerHash(), lexical_cast<unsigned>(txIndex),
        blockInfo->blockHeader().number());

    auto parent2ChildList = getParent2ChildListByTxsProofCache(blockInfo);

    auto child2Parent = getChild2ParentCacheByTransaction(parent2ChildList, blockInfo);
    // get merkle from  parent2ChildList and child2Parent
    bytes txData;
    tx->encode(txData);
    auto hashWithIndex = getHashNeed2Proof(tx->transactionIndex(), txData);
    this->getMerkleProof(hashWithIndex, *parent2ChildList, *child2Parent, merkleProof);
    return std::make_pair(tx, merkleProof);
}


dev::bytes BlockChainImp::getHashNeed2Proof(uint32_t index, const dev::bytes& data)
{
    RLPStream s;
    s << index;
    bytes bytesHash;
    bytesHash.insert(bytesHash.end(), s.out().begin(), s.out().end());
    dev::h256 dataHash = crypto::Hash(data);
    bytesHash.insert(bytesHash.end(), dataHash.begin(), dataHash.end());
#if 0
    dev::h256 hashWithIndex = Hash(bytesHash);
    BLOCKCHAIN_LOG(DEBUG) << "transactionindex:" << index << " data:" << toHex(data)
                          << " bytesHash:" << toHex(bytesHash)
                          << " hashWithIndex:" << hashWithIndex.hex();
#endif

    return bytesHash;
}

void BlockChainImp::parseMerkleMap(
    std::shared_ptr<std::map<std::string, std::vector<std::string>>> parent2ChildList,
    Child2ParentMap& child2Parent)
{
    // trans parent2ChildList into child2Parent concurrently
    tbb::parallel_for_each(parent2ChildList->begin(), parent2ChildList->end(),
        [&](std::pair<const std::string, std::vector<std::string>>& _childListIterator) {
            auto childList = _childListIterator.second;
            auto parent = _childListIterator.first;
            tbb::parallel_for(tbb::blocked_range<size_t>(0, childList.size()),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t i = range.begin(); i < range.end(); i++)
                    {
                        std::string child = childList[i];
                        if (!child.empty())
                        {
                            child2Parent[child] = parent;
                        }
                    }
                });
        });
}

void BlockChainImp::getMerkleProof(dev::bytes const& _txHash,
    const std::map<std::string, std::vector<std::string>>& parent2ChildList,
    const Child2ParentMap& child2Parent,
    std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>& merkleProof)
{
    std::string merkleNode = toHex(_txHash);
    auto itChild2Parent = child2Parent.find(merkleNode);
    while (itChild2Parent != child2Parent.end())
    {
        auto itParent2ChildList = parent2ChildList.find(itChild2Parent->second);
        if (itParent2ChildList == parent2ChildList.end())
        {
            break;
        }
        // get index from itParent2ChildList->second by merkleNode
        auto itChildlist = std::find(
            itParent2ChildList->second.begin(), itParent2ChildList->second.end(), merkleNode);
        if (itChildlist == itParent2ChildList->second.end())
        {
            break;
        }
        // copy to merkle proof path
        std::vector<std::string> leftpath;
        std::vector<std::string> rightpath;
        leftpath.insert(leftpath.end(), itParent2ChildList->second.begin(), itChildlist);
        rightpath.insert(rightpath.end(), std::next(itChildlist), itParent2ChildList->second.end());
        merkleProof.push_back(std::make_pair(std::move(leftpath), std::move(rightpath)));
        merkleNode = itChild2Parent->second;
        itChild2Parent = child2Parent.find(merkleNode);
    }
}


TransactionReceipt::Ptr BlockChainImp::getTransactionReceiptByHash(dev::h256 const& _txHash)
{
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_TX_HASH_2_BLOCK, false, true);
    if (tb)
    {
        string strblock = "";
        string txIndex = "";
        auto entries = tb->select(_txHash.hex(), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            strblock = entry->getField(SYS_VALUE);
            txIndex = entry->getField("index");
            std::shared_ptr<Block> pblock = getBlockByNumber(lexical_cast<int64_t>(strblock));
            if (!pblock)
            {
                return std::make_shared<TransactionReceipt>();
            }
            auto receipts = pblock->transactionReceipts();
            if (receipts->size() > lexical_cast<uint>(txIndex))
            {
                return (*receipts)[lexical_cast<uint>(txIndex)];
            }
        }
    }
    BLOCKCHAIN_LOG(TRACE) << LOG_DESC(
        "[#getTransactionReceiptByHash]Can't find tx, return empty localised tx receipt");
    return std::make_shared<TransactionReceipt>();
}


std::pair<dev::eth::LocalisedTransactionReceipt::Ptr,
    std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
BlockChainImp::getTransactionReceiptByHashWithProof(
    dev::h256 const& _txHash, dev::eth::LocalisedTransaction& transaction)
{
    std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> merkleProof;

    std::pair<std::shared_ptr<dev::eth::Block>, std::string> blockInfoWithTxIndex;

    if (g_BCOSConfig.version() < V2_2_0)
    {
        BLOCKCHAIN_LOG(ERROR)
            << "getTransactionReceiptByHashWithProof only support after by v2.2.0";
        BOOST_THROW_EXCEPTION(
            MethodNotSupport() << errinfo_comment("method not support in this version"));
    }

    if (!getBlockAndIndexByTxHash(_txHash, blockInfoWithTxIndex))
    {
        BLOCKCHAIN_LOG(ERROR) << LOG_DESC("get block info  failed")
                              << LOG_KV("_txHash", _txHash.hex());
        return std::make_pair(std::make_shared<dev::eth::LocalisedTransactionReceipt>(
                                  eth::TransactionException::None),
            merkleProof);
    }
    auto txIndex = blockInfoWithTxIndex.second;
    auto blockInfo = blockInfoWithTxIndex.first;

    auto receipts = blockInfo->transactionReceipts();
    auto txs = blockInfo->transactions();
    if ((receipts->size() <= lexical_cast<uint>(txIndex)) ||
        (txs->size() <= lexical_cast<uint>(txIndex)))
    {
        BLOCKCHAIN_LOG(ERROR) << LOG_DESC("txindex is invalidate ") << LOG_KV("txIndex", txIndex);
        return std::make_pair(std::make_shared<dev::eth::LocalisedTransactionReceipt>(
                                  eth::TransactionException::None),
            merkleProof);
    }

    auto receipt = (*receipts)[lexical_cast<uint>(txIndex)];
    transaction =
        LocalisedTransaction(*((*txs)[lexical_cast<uint>(txIndex)]), blockInfo->headerHash(),
            lexical_cast<unsigned>(txIndex), blockInfo->blockHeader().number());

    auto txReceipt = std::make_shared<LocalisedTransactionReceipt>(*receipt, _txHash,
        blockInfo->headerHash(), blockInfo->header().number(), transaction.from(), transaction.to(),
        lexical_cast<uint>(txIndex), receipt->gasUsed(), receipt->contractAddress());

    auto parent2ChildList = getParent2ChildListByReceiptProofCache(blockInfo);

    auto child2Parent = getChild2ParentCacheByReceipt(parent2ChildList, blockInfo);
    // get receipt hash with index
    bytes receiptData;
    txReceipt->encode(receiptData);
    auto hashWithIndex = getHashNeed2Proof(txReceipt->transactionIndex(), receiptData);
    // get merkle from  parent2ChildList and child2Parent
    this->getMerkleProof(hashWithIndex, *parent2ChildList, *child2Parent, merkleProof);
    return std::make_pair(txReceipt, merkleProof);
}

std::shared_ptr<Parent2ChildListMap> BlockChainImp::getParent2ChildListByReceiptProofCache(
    dev::eth::Block::Ptr _block)
{
    UpgradableGuard l(m_receiptWithProofMutex);
    // cache for the block parent2ChildList
    if (m_receiptWithProof.second && m_receiptWithProof.first == _block->blockHeader().number())
    {
        return m_receiptWithProof.second;
    }

    UpgradeGuard ul(l);
    // After preempting the write lock, judge again whether m_receiptWithProof has been updated
    // to prevent lock competition
    if (m_receiptWithProof.second && m_receiptWithProof.first == _block->blockHeader().number())
    {
        return m_receiptWithProof.second;
    }
    auto parent2ChildList = _block->getReceiptProof();
    m_receiptWithProof = std::make_pair(_block->blockHeader().number(), parent2ChildList);
    return parent2ChildList;
}

std::shared_ptr<Parent2ChildListMap> BlockChainImp::getParent2ChildListByTxsProofCache(
    dev::eth::Block::Ptr _block)
{
    UpgradableGuard l(m_transactionWithProofMutex);
    // cache for the block parent2ChildList
    if (m_transactionWithProof.second &&
        m_transactionWithProof.first == _block->blockHeader().number())
    {
        return m_transactionWithProof.second;
    }
    UpgradeGuard ul(l);
    // After preempting the write lock, judge again whether m_transactionWithProof has been
    // updated to prevent lock competition
    if (m_transactionWithProof.second &&
        m_transactionWithProof.first == _block->blockHeader().number())
    {
        return m_transactionWithProof.second;
    }
    std::shared_ptr<Parent2ChildListMap> parent2ChildList = _block->getTransactionProof();
    m_transactionWithProof = std::make_pair(_block->blockHeader().number(), parent2ChildList);
    return parent2ChildList;
}

std::shared_ptr<MerkleProofType> BlockChainImp::getTransactionReceiptProof(
    dev::eth::Block::Ptr _block, uint64_t const& _index)
{
    if (!_block || _block->transactionReceipts()->size() <= _index)
    {
        return nullptr;
    }
    auto txReceipt = (*(_block->transactionReceipts()))[_index];
    auto parent2ChildList = getParent2ChildListByReceiptProofCache(_block);
    bytes encodedData;
    txReceipt->encode(encodedData);

    // get merkle proof
    std::shared_ptr<MerkleProofType> merkleProof = std::make_shared<MerkleProofType>();
    auto child2Parent = getChild2ParentCacheByReceipt(parent2ChildList, _block);
    auto hashWithIndex = getHashNeed2Proof(_index, encodedData);
    this->getMerkleProof(hashWithIndex, *parent2ChildList, *child2Parent, *merkleProof);

    return merkleProof;
}

std::shared_ptr<MerkleProofType> BlockChainImp::getTransactionProof(
    dev::eth::Block::Ptr _block, uint64_t const& _index)
{
    if (!_block || _block->getTransactionSize() <= _index)
    {
        return nullptr;
    }
    auto tx = (*(_block->transactions()))[_index];
    auto parent2ChildList = getParent2ChildListByTxsProofCache(_block);
    bytes encodedData;
    tx->encode(encodedData);
    // get merkle proof
    std::shared_ptr<MerkleProofType> merkleProof = std::make_shared<MerkleProofType>();
    auto child2Parent = getChild2ParentCacheByTransaction(parent2ChildList, _block);

    auto hashWithIndex = getHashNeed2Proof(_index, encodedData);

    this->getMerkleProof(hashWithIndex, *parent2ChildList, *child2Parent, *merkleProof);
    return merkleProof;
}


LocalisedTransactionReceipt::Ptr BlockChainImp::getLocalisedTxReceiptByHash(
    dev::h256 const& _txHash)
{
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_TX_HASH_2_BLOCK, false, true);
    if (tb)
    {
        auto entries = tb->select(_txHash.hex(), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            auto blockNum = lexical_cast<int64_t>(entry->getField(SYS_VALUE));
            auto txIndex = lexical_cast<uint>(entry->getField("index"));

            std::shared_ptr<Block> pblock = getBlockByNumber(lexical_cast<int64_t>(blockNum));
            if (!pblock)
            {
                return std::make_shared<LocalisedTransactionReceipt>(
                    TransactionReceipt(), h256(0), h256(0), -1, Address(), Address(), -1, 0);
            }
            auto txs = pblock->transactions();
            auto receipts = pblock->transactionReceipts();
            if (receipts->size() > txIndex && txs->size() > txIndex)
            {
                auto tx = (*txs)[txIndex];
                auto receipt = (*receipts)[txIndex];

                return std::make_shared<LocalisedTransactionReceipt>(*receipt, _txHash,
                    pblock->headerHash(), pblock->header().number(), tx->from(), tx->to(), txIndex,
                    receipt->gasUsed(), receipt->contractAddress());
            }
        }
    }
    BLOCKCHAIN_LOG(TRACE) << LOG_DESC(
        "[#getLocalisedTxReceiptByHash]Can't find tx, return empty localised tx receipt");
    return std::make_shared<LocalisedTransactionReceipt>(
        TransactionReceipt(), h256(0), h256(0), -1, Address(), Address(), -1, 0);
}

void BlockChainImp::writeNumber(const Block& block, std::shared_ptr<ExecutiveContext> context)
{
    Table::Ptr tb = context->getMemoryTableFactory()->openTable(SYS_CURRENT_STATE, false);
    if (tb)
    {
        auto entry = tb->newEntry();
        entry->setField(SYS_VALUE, lexical_cast<std::string>(block.blockHeader().number()));
        tb->update(SYS_KEY_CURRENT_NUMBER, entry, tb->newCondition());
    }
    else
    {
        BOOST_THROW_EXCEPTION(OpenSysTableFailed() << errinfo_comment(SYS_CURRENT_STATE));
    }
}

void BlockChainImp::writeTotalTransactionCount(
    const Block& block, std::shared_ptr<ExecutiveContext> context)
{
    Table::Ptr tb = context->getMemoryTableFactory()->openTable(SYS_CURRENT_STATE, false);
    if (tb)
    {
        auto entries = tb->select(SYS_KEY_TOTAL_TRANSACTION_COUNT, tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            auto currentCount = lexical_cast<int64_t>(entry->getField(SYS_VALUE));
            currentCount += block.transactions()->size();

            auto updateEntry = tb->newEntry();
            updateEntry->setField(SYS_VALUE, lexical_cast<std::string>(currentCount));
            tb->update(SYS_KEY_TOTAL_TRANSACTION_COUNT, updateEntry, tb->newCondition());
        }
        else
        {
            auto entry = tb->newEntry();
            entry->setField(SYS_VALUE, lexical_cast<std::string>(block.transactions()->size()));
            tb->insert(SYS_KEY_TOTAL_TRANSACTION_COUNT, entry);
        }
        auto receipts = block.transactionReceipts();
        int32_t failedTransactions = 0;
        for (auto& receipt : *receipts)
        {
            if (receipt->status() != TransactionException::None)
            {
                ++failedTransactions;
            }
        }
        entries = tb->select(SYS_KEY_TOTAL_FAILED_TRANSACTION, tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            auto currentCount = lexical_cast<int64_t>(entry->getField(SYS_VALUE));
            currentCount += failedTransactions;
            auto updateEntry = tb->newEntry();
            updateEntry->setField(SYS_VALUE, lexical_cast<std::string>(currentCount));
            tb->update(SYS_KEY_TOTAL_FAILED_TRANSACTION, updateEntry, tb->newCondition());
        }
        else
        {
            auto entry = tb->newEntry();
            entry->setField(SYS_VALUE, lexical_cast<std::string>(failedTransactions));
            tb->insert(SYS_KEY_TOTAL_FAILED_TRANSACTION, entry);
        }
    }
    else
    {
        BOOST_THROW_EXCEPTION(OpenSysTableFailed() << errinfo_comment(SYS_CURRENT_STATE));
    }
}

void BlockChainImp::writeTxToBlock(const Block& block, std::shared_ptr<ExecutiveContext> context)
{
    auto start_time = utcTime();
    auto record_time = utcTime();
    Table::Ptr tb = context->getMemoryTableFactory()->openTable(SYS_TX_HASH_2_BLOCK, false, true);
    Table::Ptr tb_nonces = context->getMemoryTableFactory()->openTable(SYS_BLOCK_2_NONCES, false);
    auto openTable_time_cost = utcTime() - record_time;
    record_time = utcTime();

    if (tb && tb_nonces)
    {
        auto txs = block.transactions();
        auto constructVector_time_cost = utcTime() - record_time;
        record_time = utcTime();
        std::string blockNumberStr = lexical_cast<std::string>(block.blockHeader().number());
        tbb::parallel_invoke(
            [tb, txs, blockNumberStr]() {
                tbb::parallel_for(tbb::blocked_range<size_t>(0, txs->size()),
                    [&](const tbb::blocked_range<size_t>& _r) {
                        for (size_t i = _r.begin(); i != _r.end(); ++i)
                        {
                            Entry::Ptr entry = std::make_shared<Entry>();
                            entry->setField(SYS_VALUE, blockNumberStr);
                            entry->setField("index", lexical_cast<std::string>(i));
                            entry->setForce(true);

                            tb->insert((*txs)[i]->sha3().hex(), entry,
                                std::make_shared<dev::storage::AccessOptions>(), false);
                        }
                    });
            },
            [this, tb_nonces, txs, blockNumberStr]() {
                std::vector<dev::eth::NonceKeyType> nonce_vector(txs->size());
                for (size_t i = 0; i < txs->size(); i++)
                {
                    nonce_vector[i] = (*txs)[i]->nonce();
                }
                /// insert tb2Nonces
                RLPStream rs;
                rs.appendVector(nonce_vector);
                std::shared_ptr<bytes> nonceData = std::make_shared<bytes>();
                rs.swapOut(*nonceData);
                Entry::Ptr entry_tb2nonces = std::make_shared<Entry>();
                // store nonce directly >= v2.2.0
                // store the hex string < 2.2.0
                writeBytesToField(nonceData, entry_tb2nonces, SYS_VALUE);

                entry_tb2nonces->setForce(true);
                tb_nonces->insert(lexical_cast<std::string>(blockNumberStr), entry_tb2nonces);
                // Destruct the entry_tb2nonces in m_destructorThread
                HolderForDestructor<Entry> holder(std::move(entry_tb2nonces));
                m_destructorThread->enqueue(std::move(holder));
            });
        auto insertTable_time_cost = utcTime() - record_time;
        BLOCKCHAIN_LOG(DEBUG) << LOG_BADGE("WriteTxOnCommit")
                              << LOG_DESC("Write tx to block time record")
                              << LOG_KV("openTableTimeCost", openTable_time_cost)
                              << LOG_KV("constructVectorTimeCost", constructVector_time_cost)
                              << LOG_KV("insertTableTimeCost", insertTable_time_cost)
                              << LOG_KV("totalTimeCost", utcTime() - start_time);
    }
    else
    {
        BOOST_THROW_EXCEPTION(OpenSysTableFailed() << errinfo_comment(SYS_TX_HASH_2_BLOCK));
    }
}

void BlockChainImp::writeNumber2Hash(const Block& block, std::shared_ptr<ExecutiveContext> context)
{
    Table::Ptr tb = context->getMemoryTableFactory()->openTable(SYS_NUMBER_2_HASH, false);
    if (tb)
    {
        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField(SYS_VALUE, block.blockHeader().hash().hex());
        entry->setForce(true);
        tb->insert(lexical_cast<std::string>(block.blockHeader().number()), entry);
    }
    else
    {
        BOOST_THROW_EXCEPTION(OpenSysTableFailed() << errinfo_comment(SYS_NUMBER_2_HASH));
    }
}

void BlockChainImp::writeHash2Block(Block& block, std::shared_ptr<ExecutiveContext> context)
{
    Table::Ptr tb = context->getMemoryTableFactory()->openTable(SYS_HASH_2_BLOCK, false);
    if (tb)
    {
        Entry::Ptr entry = std::make_shared<Entry>();
        // use binary block data since v2.2.0, use toHex before v2.2.0
        writeBlockToField(block, entry);
        entry->setForce(true);
        tb->insert(block.blockHeader().hash().hex(), entry);
        // Block entry destructor is time-consuming, add it to the thread pool to destruct
        // asynchronously, Destruct the entry in m_destructorThread
        HolderForDestructor<Entry> holder(std::move(entry));
        m_destructorThread->enqueue(std::move(holder));
    }
    else
    {
        BOOST_THROW_EXCEPTION(OpenSysTableFailed() << errinfo_comment(SYS_HASH_2_BLOCK));
    }
}

bool BlockChainImp::isBlockShouldCommit(int64_t const& _blockNumber)
{
    if (_blockNumber != number() + 1)
    {
        BLOCKCHAIN_LOG(WARNING) << LOG_DESC(
                                       "[#commitBlock]Commit fail due to incorrect block number")
                                << LOG_KV("needNumber", number() + 1)
                                << LOG_KV("committedNumber", _blockNumber);
        return false;
    }
    return true;
}

CommitResult BlockChainImp::commitBlock(
    std::shared_ptr<Block> block, std::shared_ptr<ExecutiveContext> context)
{
    auto start_time = utcTime();
    auto record_time = utcTime();
    if (!isBlockShouldCommit(block->blockHeader().number()))
    {
        return CommitResult::ERROR_NUMBER;
    }

    h256 parentHash = numberHash(number());
    if (block->blockHeader().parentHash() != numberHash(number()))
    {
        BLOCKCHAIN_LOG(WARNING) << LOG_DESC(
                                       "[#commitBlock]Commit fail due to incorrect parent hash")
                                << LOG_KV("needParentHash", parentHash)
                                << LOG_KV("committedParentHash", block->blockHeader().parentHash());
        return CommitResult::ERROR_PARENT_HASH;
    }

    try
    {
        auto before_write_time_cost = utcTime() - record_time;
        record_time = utcTime();
        {
            std::lock_guard<std::mutex> l(commitMutex);
            if (!isBlockShouldCommit(block->blockHeader().number()))
            {
                return CommitResult::ERROR_PARENT_HASH;
            }
            auto write_record_time = utcTime();
            tbb::parallel_invoke([this, block, context]() { writeHash2Block(*block, context); },
                [this, block, context]() { writeNumber2Hash(*block, context); },
                [this, block, context]() { writeNumber(*block, context); },
                [this, block, context]() { writeTotalTransactionCount(*block, context); },
                [this, block, context]() { writeTxToBlock(*block, context); });
            auto write_table_time = utcTime() - write_record_time;

            write_record_time = utcTime();
            try
            {
                context->dbCommit(*block);
            }
            catch (std::exception& e)
            {
                BLOCKCHAIN_LOG(ERROR)
                    << LOG_DESC("Commit Block failed")
                    << LOG_KV("number", block->blockHeader().number()) << LOG_KV("what", e.what());
                return CommitResult::ERROR_COMMITTING;
            }
            auto dbCommit_time_cost = utcTime() - write_record_time;
            write_record_time = utcTime();
            {
                WriteGuard ll(m_blockNumberMutex);
                m_blockNumber = block->blockHeader().number();
            }
            auto updateBlockNumber_time_cost = utcTime() - write_record_time;
            BLOCKCHAIN_LOG(DEBUG) << LOG_BADGE("Commit")
                                  << LOG_DESC("Commit block time record(write)")
                                  << LOG_KV("writeTableTime", write_table_time)
                                  << LOG_KV("dbCommitTimeCost", dbCommit_time_cost)
                                  << LOG_KV(
                                         "updateBlockNumberTimeCost", updateBlockNumber_time_cost);
        }
        auto writeBlock_time_cost = utcTime() - record_time;
        record_time = utcTime();

        m_blockCache.add(block);
        auto addBlockCache_time_cost = utcTime() - record_time;
        record_time = utcTime();
        m_onReady(m_blockNumber);
        auto noteReady_time_cost = utcTime() - record_time;

        BLOCKCHAIN_LOG(DEBUG) << LOG_BADGE("Commit") << LOG_DESC("Commit block time record")
                              << LOG_KV("beforeTimeCost", before_write_time_cost)
                              << LOG_KV("writeBlockTimeCost", writeBlock_time_cost)
                              << LOG_KV("addBlockCacheTimeCost", addBlockCache_time_cost)
                              << LOG_KV("noteReadyTimeCost", noteReady_time_cost)
                              << LOG_KV("totalTimeCost", utcTime() - start_time);
    }
    catch (OpenSysTableFailed const& e)
    {
        BLOCKCHAIN_LOG(FATAL)
            << LOG_DESC("[commitBlock]System meets error when try to write block to storage")
            << LOG_KV("EINFO", boost::diagnostic_information(e));
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(
            OpenSysTableFailed() << errinfo_comment(" write block to storage failed."));
    }
    /// leveldb caused exception: database corruption or the disk has no space left
    catch (StorageException& e)
    {
        BLOCKCHAIN_LOG(FATAL) << LOG_BADGE("CommitBlock: storage exception")
                              << LOG_KV("EINFO", boost::diagnostic_information(e));
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(
            OpenSysTableFailed() << errinfo_comment(" write block to storage failed."));
    }
    return CommitResult::OK;
}

// decode the block from the block data fetched from system table
std::shared_ptr<Block> BlockChainImp::decodeBlock(dev::storage::Entry::ConstPtr _entry)
{
    std::shared_ptr<Block> block = nullptr;
    // >= v2.2.0
    if (!m_enableHexBlock)
    {
        auto bytesBlock = _entry->getFieldConst(SYS_VALUE);
        block = std::make_shared<Block>(bytesBlock, CheckTransaction::None);
    }
    else
    {
        // < v2.2.0 or use mysql, external
        auto strBlock = _entry->getField(SYS_VALUE);
        block = std::make_shared<Block>(fromHex(strBlock.c_str()), CheckTransaction::None);
    }

    return block;
}

// get data from the system table
std::shared_ptr<bytes> BlockChainImp::getDataBytes(
    dev::storage::Entry::ConstPtr _entry, std::string const& _fieldName)
{
    std::shared_ptr<bytes> dataRlp = nullptr;
    // >= v2.2.0
    if (!m_enableHexBlock)
    {
        auto dataBytes = _entry->getFieldConst(_fieldName);
        dataRlp = std::make_shared<bytes>(dataBytes.begin(), dataBytes.end());
    }
    else
    {
        // < v2.2.0 or use mysql, external
        auto dataStr = _entry->getField(_fieldName);
        dataRlp = std::make_shared<bytes>(fromHex(dataStr.c_str()));
    }

    return dataRlp;
}

// write block data into the system table
void BlockChainImp::writeBlockToField(
    dev::eth::Block const& _block, dev::storage::Entry::Ptr _entry)
{
    std::shared_ptr<bytes> out = std::make_shared<bytes>();
    _block.encode(*out);
    writeBytesToField(out, _entry, SYS_VALUE);
}

// write bytes to the SYS_VALUE field
void BlockChainImp::writeBytesToField(std::shared_ptr<dev::bytes> _data,
    dev::storage::Entry::Ptr _entry, std::string const& _fieldName)
{
    // >= v2.2.0
    if (!m_enableHexBlock)
    {
        _entry->setField(_fieldName, _data->data(), _data->size());
    }
    // < v2.2.0
    else
    {
        _entry->setField(_fieldName, toHexPrefixed(*_data));
    }
}

std::shared_ptr<Child2ParentMap> BlockChainImp::getChild2ParentCache(SharedMutex& _mutex,
    std::pair<dev::eth::BlockNumber, std::shared_ptr<Child2ParentMap>>& _cache,
    std::shared_ptr<Parent2ChildListMap> _parent2Child, dev::eth::Block::Ptr _block)
{
    UpgradableGuard l(_mutex);
    if (_cache.second && _cache.first == _block->blockHeader().number())
    {
        return _cache.second;
    }
    UpgradeGuard ul(l);
    // After preempting the write lock, judge again whether m_receiptWithProof has been updated
    // to prevent lock competition
    if (_cache.second && _cache.first == _block->blockHeader().number())
    {
        return _cache.second;
    }
    std::shared_ptr<Child2ParentMap> child2Parent = std::make_shared<Child2ParentMap>();
    parseMerkleMap(_parent2Child, *child2Parent);
    _cache = std::make_pair(_block->blockHeader().number(), child2Parent);
    return child2Parent;
}

std::shared_ptr<Child2ParentMap> BlockChainImp::getChild2ParentCacheByReceipt(
    std::shared_ptr<Parent2ChildListMap> _parent2ChildList, dev::eth::Block::Ptr _block)
{
    return getChild2ParentCache(
        x_receiptChild2ParentCache, m_receiptChild2ParentCache, _parent2ChildList, _block);
}

std::shared_ptr<Child2ParentMap> BlockChainImp::getChild2ParentCacheByTransaction(
    std::shared_ptr<Parent2ChildListMap> _parent2ChildList, dev::eth::Block::Ptr _block)
{
    return getChild2ParentCache(
        x_txsChild2ParentCache, m_txsChild2ParentCache, _parent2ChildList, _block);
}