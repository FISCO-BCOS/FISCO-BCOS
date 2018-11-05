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
#include <libethcore/Transaction.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Table.h>
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace std;
using namespace dev::eth;
using namespace dev::blockchain;
using namespace dev::storage;
using namespace dev::blockverifier;
using boost::lexical_cast;


void BlockChainImp::setStateStorage(Storage::Ptr stateStorage)
{
    m_stateStorage = stateStorage;
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
    LOG(TRACE) << "BlockChainImp::number num=" << num;
    return num;
}

h256 BlockChainImp::numberHash(int64_t _i)
{
    LOG(TRACE) << "BlockChainImp::numberHash _i=" << _i;
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
    LOG(TRACE) << "BlockChainImp::numberHash numberHash=" << numberHash;
    return h256(numberHash);
}

std::shared_ptr<Block> BlockChainImp::getBlockByHash(h256 const& _blockHash)
{
    LOG(TRACE) << "BlockChainImp::getBlockByHash _blockHash=" << _blockHash
               << "_blockHash.hex()=" << _blockHash.hex();
    if (_blockHash == h256(0))
    {
        return std::make_shared<Block>();
    }

    string strblock = "";
    Table::Ptr tb = getMemoryTableFactory()->openTable(SYS_HASH_2_BLOCK);
    if (tb)
    {
        auto entries = tb->select(_blockHash.hex(), tb->newCondition());
        if (entries->size() > 0)
        {
            auto entry = entries->get(0);
            strblock = entry->getField(SYS_VALUE);
            LOG(TRACE) << "BlockChainImp::getBlockByHash strblock=" << strblock;
            return std::make_shared<Block>(fromHex(strblock.c_str()));
        }
    }

    return nullptr;
}

std::shared_ptr<Block> BlockChainImp::getBlockByNumber(int64_t _i)
{
    LOG(TRACE) << "BlockChainImp::getBlockByNumber _i=" << _i;
    if (_i == 0)
    {
        return std::make_shared<Block>();
    }
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
    return nullptr;
}

Transaction BlockChainImp::getTxByHash(dev::h256 const& _txHash)
{
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


    return Transaction();
}

LocalisedTransaction BlockChainImp::getLocalisedTxByHash(dev::h256 const& _txHash)
{
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
    return LocalisedTransaction(Transaction(), h256(0), -1);
}

TransactionReceipt BlockChainImp::getTransactionReceiptByHash(dev::h256 const& _txHash)
{
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
    return TransactionReceipt();
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

void BlockChainImp::commitBlock(Block& block, std::shared_ptr<ExecutiveContext> context)
{
    if (block.blockHeader().number() == number() + 1)
    {
        writeNumber(block, context);
        writeTxToBlock(block, context);
        writeBlockInfo(block, context);
        context->dbCommit();
        m_onReady();
    }
}