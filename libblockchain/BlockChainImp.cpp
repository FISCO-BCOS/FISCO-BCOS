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
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/easylog.h>
#include <libethcore/Block.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <libstorage/ConsensusPrecompiled.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Table.h>
#include <boost/lexical_cast.hpp>
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
using boost::lexical_cast;


void BlockChainImp::setStateStorage(Storage::Ptr stateStorage)
{
    m_stateStorage = stateStorage;
}

void BlockChainImp::setStateFactory(StateFactoryInterface::Ptr _stateFactory)
{
    m_stateFactory = _stateFactory;
}

shared_ptr<MemoryTableFactory> BlockChainImp::getMemoryTableFactory()
{
    dev::storage::MemoryTableFactory::Ptr memoryTableFactory =
        std::make_shared<dev::storage::MemoryTableFactory>();
    memoryTableFactory->setStateStorage(m_stateStorage);
    return memoryTableFactory;
}

int64_t BlockChainImp::number()
{
    int64_t num = 0;
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_CURRENT_STATE);
    if (tb)
    {
        auto entries = tb->select(SYS_KEY_CURRENT_NUMBER, tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            std::string currentNumber = entry->getField(SYS_VALUE);
            num = lexical_cast<int64_t>(currentNumber.c_str());
        }
    }
    BLOCKCHAIN_LOG(TRACE) << "[#number] [currentNum]: "
                          << "[" << num << "]";
    return num;
}

std::pair<int64_t, int64_t> BlockChainImp::totalTransactionCount()
{
    int64_t count = 0;
    int64_t number = 0;
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_CURRENT_STATE);
    if (tb)
    {
        auto entries = tb->select(SYS_KEY_TOTAL_TRANSACTION_COUNT, tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            std::string strCount = entry->getField(SYS_VALUE);
            count = lexical_cast<int64_t>(strCount);
            std::string strNumber = entry->getField("_num_");
            number = lexical_cast<int64_t>(strNumber);
        }
    }
    BLOCKCHAIN_LOG(TRACE) << "[#totalTransactionCount] [txCount/currentNum]: "
                          << "[" << count << "/" << number << "]";
    return std::make_pair(count, number);
}

bytes BlockChainImp::getCode(Address _address)
{
    BLOCKCHAIN_LOG(TRACE) << "[#getCode] [address]: "
                          << "[" << toHex(_address) << "]";
    bytes ret;
    int64_t num = number();
    auto block = getBlockByNumber(num);

    if (!block)
    {
        BLOCKCHAIN_LOG(TRACE) << "[#getCode] Can't find the block, return empty code";
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
    BLOCKCHAIN_LOG(TRACE) << "[#number] [num]: "
                          << "[" << _i << "]";
    string numberHash = "";
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_NUMBER_2_HASH);
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

std::shared_ptr<Block> BlockChainImp::getBlockByHash(h256 const& _blockHash)
{
    BLOCKCHAIN_LOG(TRACE) << "[#getBlockByHash] [blockHash]: "
                          << "[" << toHex(_blockHash) << "]";
    string strblock = "";
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_HASH_2_BLOCK);
    if (tb)
    {
        auto entries = tb->select(_blockHash.hex(), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            strblock = entry->getField(SYS_VALUE);
            return std::make_shared<Block>(fromHex(strblock.c_str()));
        }
    }
    BLOCKCHAIN_LOG(TRACE) << "[#getBlockByHash] Can't find block, return nullptr";
    return nullptr;
}

void BlockChainImp::checkAndBuildGenesisBlock(GenesisBlockParam const& initParam)
{
    std::shared_ptr<Block> block = getBlockByNumber(0);
    if (block == nullptr)
    {
        block = std::make_shared<Block>();
        block->setEmptyBlock();
        block->header().appendExtraDataArray(asBytes(initParam.groupMark));
        shared_ptr<MemoryTableFactory> mtb = getMemoryTableFactory();
        Table::Ptr tb = mtb->openTable(SYS_NUMBER_2_HASH);
        if (tb)
        {
            Entry::Ptr entry = std::make_shared<Entry>();
            entry->setField(SYS_VALUE, block->blockHeader().hash().hex());
            tb->insert(lexical_cast<std::string>(block->blockHeader().number()), entry);
        }

        tb = mtb->openTable(SYS_MINERS);
        if (tb)
        {
            for (dev::h512 node : initParam.minerList)
            {
                Entry::Ptr entry = std::make_shared<Entry>();
                entry->setField(PRI_COLUMN, PRI_KEY);
                entry->setField(NODE_TYPE, NODE_TYPE_MINER);
                entry->setField(NODE_KEY_NODEID, dev::toHex(node));
                entry->setField(NODE_KEY_ENABLENUM, "0");
                tb->insert(PRI_KEY, entry);
            }

            for (dev::h512 node : initParam.observerList)
            {
                Entry::Ptr entry = std::make_shared<Entry>();
                entry->setField(PRI_COLUMN, PRI_KEY);
                entry->setField(NODE_TYPE, NODE_TYPE_OBSERVER);
                entry->setField(NODE_KEY_NODEID, dev::toHex(node));
                entry->setField(NODE_KEY_ENABLENUM, "0");
                tb->insert(PRI_KEY, entry);
            }
        }

        tb = mtb->openTable(SYS_HASH_2_BLOCK);
        if (tb)
        {
            Entry::Ptr entry = std::make_shared<Entry>();
            bytes out;
            block->encode(out);
            entry->setField(SYS_VALUE, toHexPrefixed(out));
            tb->insert(block->blockHeader().hash().hex(), entry);
        }

        mtb->commitDB(block->blockHeader().hash(), block->blockHeader().number());
        BLOCKCHAIN_LOG(INFO) << "[#setGroupMark] Insert the 0th block";
    }
    else
    {
        /// compare() return 0 means equal!
        /// If not equal, only print warning, willnot kill process.
        if (!initParam.groupMark.compare(asString(block->header().extraData(0))))
        {
            BLOCKCHAIN_LOG(INFO) << "[#setGroupMark] Already have the 0th block, 0th groupMark is "
                                    "equal to file groupMark.";
        }
        else
        {
            BLOCKCHAIN_LOG(WARNING)
                << "[#setGroupMark] Already have the 0th block, 0th groupMark:"
                << asString(block->header().extraData(0))
                << " is not equal to file groupMark:" << initParam.groupMark << " !";
        }
    }
}

dev::h512s BlockChainImp::getNodeListByType(int64_t blockNumber, std::string const& type)
{
    LOG(TRACE) << "BlockChainImp::getNodeListByType " << type << " at " << blockNumber;

    dev::h512s list;
    try
    {
        auto nodes = m_stateStorage->select(
            numberHash(blockNumber), blockNumber, storage::SYS_MINERS, blockverifier::PRI_KEY);
        if (!nodes)
            return list;

        for (size_t i = 0; i < nodes->size(); i++)
        {
            auto node = nodes->get(i);
            if (!node)
                return list;

            if ((node->getField(blockverifier::NODE_TYPE) == type) &&
                (boost::lexical_cast<int>(node->getField(blockverifier::NODE_KEY_ENABLENUM)) <=
                    blockNumber))
            {
                h512 nodeID = h512(node->getField(blockverifier::NODE_KEY_NODEID));
                list.push_back(nodeID);
                LOG(TRACE) << "Add nodeID [nodeID/idx]: " << toHex(nodeID) << "/" << i << std::endl;
            }
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "BlockChainImp::getNodeListByType failed [EINFO]: "
                   << boost::diagnostic_information(e);
    }

    std::stringstream s;
    s << "BlockChainImp::getNodeListByType " << type << ":";
    for (dev::h512 node : list)
        s << toJS(node) << ",";
    LOG(TRACE) << s.str();

    return list;
}

dev::h512s BlockChainImp::minerList()
{
    int64_t blockNumber = number();
    UpgradableGuard l(m_nodeListMutex);
    if (m_cacheNumByMiner == blockNumber)
    {
        LOG(TRACE) << "BlockChainImp::minerList by cache, size:" << m_minerList.size();
        return m_minerList;
    }
    dev::h512s list = getNodeListByType(blockNumber, blockverifier::NODE_TYPE_MINER);
    UpgradeGuard ul(l);
    m_cacheNumByMiner = blockNumber;
    m_minerList = list;

    return list;
}

dev::h512s BlockChainImp::observerList()
{
    int64_t blockNumber = number();
    UpgradableGuard l(m_nodeListMutex);
    if (m_cacheNumByObserver == blockNumber)
    {
        LOG(TRACE) << "BlockChainImp::observerList by cache, size:" << m_observerList.size();
        return m_observerList;
    }
    dev::h512s list = getNodeListByType(blockNumber, blockverifier::NODE_TYPE_OBSERVER);
    UpgradeGuard ul(l);
    m_cacheNumByObserver = blockNumber;
    m_observerList = list;

    return list;
}

std::shared_ptr<Block> BlockChainImp::getBlockByNumber(int64_t _i)
{
    BLOCKCHAIN_LOG(TRACE) << "[#getBlockByNumber] [num]: "
                          << "[" << _i << "]";
    string numberHash = "";
    string strblock = "";
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_NUMBER_2_HASH);
    if (tb)
    {
        auto entries = tb->select(lexical_cast<std::string>(_i), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            numberHash = entry->getField(SYS_VALUE);
            return getBlockByHash(h256(numberHash));
        }
    }
    BLOCKCHAIN_LOG(TRACE) << "[#getBlockByNumber] Can't find block, return nullptr";
    return nullptr;
}

Transaction BlockChainImp::getTxByHash(dev::h256 const& _txHash)
{
    BLOCKCHAIN_LOG(TRACE) << "[#getTxByHash] [txHash]: "
                          << "[" << _txHash << "]";
    string strblock = "";
    string txIndex = "";
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_TX_HASH_2_BLOCK);
    if (tb)
    {
        auto entries = tb->select(_txHash.hex(), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            strblock = entry->getField(SYS_VALUE);
            txIndex = entry->getField("index");
            std::shared_ptr<Block> pblock = getBlockByNumber(lexical_cast<int64_t>(strblock));
            std::vector<Transaction> txs = pblock->transactions();
            if (txs.size() > lexical_cast<uint>(txIndex))
            {
                return txs[lexical_cast<uint>(txIndex)];
            }
        }
    }
    BLOCKCHAIN_LOG(TRACE) << "[#getTxByHash] Can't find tx, return empty tx";
    return Transaction();
}

LocalisedTransaction BlockChainImp::getLocalisedTxByHash(dev::h256 const& _txHash)
{
    BLOCKCHAIN_LOG(TRACE) << "[#getLocalisedTxByHash] [txHash]: "
                          << "[" << _txHash << "]";
    string strblockhash = "";
    string txIndex = "";
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_TX_HASH_2_BLOCK);
    if (tb)
    {
        auto entries = tb->select(_txHash.hex(), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            strblockhash = entry->getField(SYS_VALUE);
            txIndex = entry->getField("index");
            std::shared_ptr<Block> pblock = getBlockByNumber(lexical_cast<int64_t>(strblockhash));
            std::vector<Transaction> txs = pblock->transactions();
            if (txs.size() > lexical_cast<uint>(txIndex))
            {
                return LocalisedTransaction(txs[lexical_cast<uint>(txIndex)], pblock->headerHash(),
                    lexical_cast<unsigned>(txIndex), pblock->blockHeader().number());
            }
        }
    }
    BLOCKCHAIN_LOG(TRACE) << "[#getLocalisedTxByHash] Can't find tx, return empty localised tx";
    return LocalisedTransaction(Transaction(), h256(0), -1, -1);
}

TransactionReceipt BlockChainImp::getTransactionReceiptByHash(dev::h256 const& _txHash)
{
    BLOCKCHAIN_LOG(TRACE) << "[#getTransactionReceiptByHash] [txHash]: "
                          << "[" << _txHash << "]";
    string strblock = "";
    string txIndex = "";
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_TX_HASH_2_BLOCK);
    if (tb)
    {
        auto entries = tb->select(_txHash.hex(), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            strblock = entry->getField(SYS_VALUE);
            txIndex = entry->getField("index");
            std::shared_ptr<Block> pblock = getBlockByNumber(lexical_cast<int64_t>(strblock));
            std::vector<TransactionReceipt> receipts = pblock->transactionReceipts();
            if (receipts.size() > lexical_cast<uint>(txIndex))
            {
                return receipts[lexical_cast<uint>(txIndex)];
            }
        }
    }
    BLOCKCHAIN_LOG(TRACE)
        << "[#getTransactionReceiptByHash] Can't find tx, return empty localised tx receipt";
    return TransactionReceipt();
}

LocalisedTransactionReceipt BlockChainImp::getLocalisedTxReceiptByHash(dev::h256 const& _txHash)
{
    BLOCKCHAIN_LOG(TRACE) << "[#getLocalisedTxReceiptByHash] [txHash]: "
                          << "[" << _txHash << "]";
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_TX_HASH_2_BLOCK);
    if (tb)
    {
        auto entries = tb->select(_txHash.hex(), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            auto blockNum = lexical_cast<int64_t>(entry->getField(SYS_VALUE));
            auto txIndex = lexical_cast<uint>(entry->getField("index"));

            std::shared_ptr<Block> pblock = getBlockByNumber(lexical_cast<int64_t>(blockNum));
            const Transactions& txs = pblock->transactions();
            const TransactionReceipts& receipts = pblock->transactionReceipts();
            if (receipts.size() > txIndex && txs.size() > txIndex)
            {
                auto& tx = txs[txIndex];
                auto& receipt = receipts[txIndex];

                return LocalisedTransactionReceipt(receipt, _txHash, pblock->headerHash(),
                    pblock->header().number(), tx.from(), tx.to(), txIndex, receipt.gasUsed(),
                    receipt.contractAddress());
            }
        }
    }
    BLOCKCHAIN_LOG(TRACE)
        << "[#getLocalisedTxReceiptByHash] Can't find tx, return empty localised tx receipt";
    return LocalisedTransactionReceipt(
        TransactionReceipt(), h256(0), h256(0), -1, Address(), Address(), -1, 0);
}

void BlockChainImp::writeNumber(const Block& block, std::shared_ptr<ExecutiveContext> context)
{
    Table::Ptr tb = context->getMemoryTableFactory()->openTable(SYS_CURRENT_STATE);
    if (tb)
    {
        auto entries = tb->select(SYS_KEY_CURRENT_NUMBER, tb->newCondition());
        auto entry = tb->newEntry();
        entry->setField(SYS_VALUE, lexical_cast<std::string>(block.blockHeader().number()));
        if (entries->size() > 0)
        {
            tb->update(SYS_KEY_CURRENT_NUMBER, entry, tb->newCondition());
        }
        else
        {
            tb->insert(SYS_KEY_CURRENT_NUMBER, entry);
        }
    }
}

void BlockChainImp::writeTotalTransactionCount(
    const Block& block, std::shared_ptr<ExecutiveContext> context)
{
    Table::Ptr tb = context->getMemoryTableFactory()->openTable(SYS_CURRENT_STATE);
    if (tb)
    {
        auto entries = tb->select(SYS_KEY_TOTAL_TRANSACTION_COUNT, tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            auto currentCount = lexical_cast<int64_t>(entry->getField(SYS_VALUE));
            currentCount += block.transactions().size();

            entry->setField(SYS_VALUE, lexical_cast<std::string>(currentCount));
            tb->update(SYS_KEY_TOTAL_TRANSACTION_COUNT, entry, tb->newCondition());
        }
        else
        {
            auto entry = tb->newEntry();
            entry->setField(SYS_VALUE, lexical_cast<std::string>(block.transactions().size()));
            tb->insert(SYS_KEY_TOTAL_TRANSACTION_COUNT, entry);
        }
    }
}

void BlockChainImp::writeTxToBlock(const Block& block, std::shared_ptr<ExecutiveContext> context)
{
    Table::Ptr tb = context->getMemoryTableFactory()->openTable(SYS_TX_HASH_2_BLOCK);
    if (tb)
    {
        std::vector<Transaction> txs = block.transactions();
        for (uint i = 0; i < txs.size(); i++)
        {
            Entry::Ptr entry = std::make_shared<Entry>();
            entry->setField(SYS_VALUE, lexical_cast<std::string>(block.blockHeader().number()));
            entry->setField("index", lexical_cast<std::string>(i));
            tb->insert(txs[i].sha3().hex(), entry);
        }
    }
}

void BlockChainImp::writeNumber2Hash(const Block& block, std::shared_ptr<ExecutiveContext> context)
{
    Table::Ptr tb = context->getMemoryTableFactory()->openTable(SYS_NUMBER_2_HASH);
    if (tb)
    {
        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField(SYS_VALUE, block.blockHeader().hash().hex());
        tb->insert(lexical_cast<std::string>(block.blockHeader().number()), entry);
    }
}

void BlockChainImp::writeHash2Block(Block& block, std::shared_ptr<ExecutiveContext> context)
{
    Table::Ptr tb = context->getMemoryTableFactory()->openTable(SYS_HASH_2_BLOCK);
    if (tb)
    {
        Entry::Ptr entry = std::make_shared<Entry>();
        bytes out;
        block.encode(out);
        entry->setField(SYS_VALUE, toHexPrefixed(out));
        tb->insert(block.blockHeader().hash().hex(), entry);
    }
}

void BlockChainImp::writeBlockInfo(Block& block, std::shared_ptr<ExecutiveContext> context)
{
    writeNumber2Hash(block, context);
    writeHash2Block(block, context);
}

CommitResult BlockChainImp::commitBlock(Block& block, std::shared_ptr<ExecutiveContext> context)
{
    int64_t num = number();
    if ((block.blockHeader().number() != num + 1))
    {
        BLOCKCHAIN_LOG(WARNING) << "[#commitBlock] Commit fail [needNumber/committedNumber]: "
                                << "[" << (num + 1) << "/" << block.blockHeader().number() << "]";
        return CommitResult::ERROR_NUMBER;
    }

    h256 parentHash = numberHash(number());
    if (block.blockHeader().parentHash() != numberHash(number()))
    {
        BLOCKCHAIN_LOG(WARNING)
            << "[#commitBlock] Commit fail [needParentHash/committedParentHash]: "
            << "[" << parentHash << "/" << block.blockHeader().parentHash() << "]";
        return CommitResult::ERROR_PARENT_HASH;
    }
    if (commitMutex.try_lock())
    {
        writeNumber(block, context);
        writeTotalTransactionCount(block, context);
        writeTxToBlock(block, context);
        writeBlockInfo(block, context);
        context->dbCommit(block);
        commitMutex.unlock();
        m_onReady();
        return CommitResult::OK;
    }
    else
    {
        BLOCKCHAIN_LOG(INFO)
            << "[#commitBlock] Try lock commitMutex fail [blockNumber/blockParentHash/parentHash]"
            << "[" << block.blockHeader().number() << "/" << block.blockHeader().parentHash() << "/"
            << parentHash << "]";
        return CommitResult::ERROR_COMMITTING;
    }
}
