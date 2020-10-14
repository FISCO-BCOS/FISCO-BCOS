/**
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
 *
 * @brief
 *
 * @file BlockChainImp.cpp
 * @author:
 * @date 2018-09-21
 */
#include "libdevcrypto/CryptoInterface.h"
#include <libblockchain/BlockChainImp.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/CommonData.h>
#include <libethcore/Block.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/Transaction.h>
#include <libstorage/Common.h>
#include <libstorage/MemoryTable.h>
#include <libstoragestate/StorageState.h>
#include <libstoragestate/StorageStateFactory.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libethcore/FakeBlock.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <unordered_map>

using namespace dev;
using namespace dev::eth;
using namespace dev::storage;
using namespace dev::blockchain;
using namespace dev::blockverifier;
using namespace dev::storagestate;
namespace dev
{
namespace test
{
//   const std::string m_extraDbName_currentState = "currentState";
//   const std::string SYS_KEY_CURRENT_NUMBER = "currentNumber";
//   const std::string SYS_VALUE = "value";
//   const std::string SYS_VALUE = "value";
//   const std::string m_txHash2Block = "txHash2Block";
//   const std::string m_number2hash = "number2hash";
//   const std::string m_hash2Block = "hash2Block";

#if 0
static std::string const c_commonHash =
    "067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
#endif
// TODO: compatibility support
static std::string const c_commonHash =
    "0b9702045ea4b58d1bdae2f802f57ac22c6b63ce571777da8586337c69834ad3";
static std::string const c_commonHashPrefix = std::string("0x").append(c_commonHash);

class MockTable : public dev::storage::MemoryTable<Serial>
{
public:
    typedef std::shared_ptr<MockTable> Ptr;

    MockTable()
    {
        m_fakeStorage[SYS_CURRENT_STATE] = std::unordered_map<std::string, Entry::Ptr>();
        m_fakeStorage[SYS_NUMBER_2_HASH] = std::unordered_map<std::string, Entry::Ptr>();
        m_fakeStorage[SYS_HASH_2_BLOCK] = std::unordered_map<std::string, Entry::Ptr>();
        m_fakeStorage[SYS_TX_HASH_2_BLOCK] = std::unordered_map<std::string, Entry::Ptr>();
    }

    Entries::ConstPtr select(const std::string& key, Condition::Ptr) override
    {
        Entries::Ptr entries = std::make_shared<Entries>();

        if (m_fakeStorage.find(m_table) != m_fakeStorage.end() &&
            m_fakeStorage[m_table].find(key) != m_fakeStorage[m_table].end())
        {
            auto entry = m_fakeStorage[m_table][key];
            entries->addEntry(entry);
        }
        return entries;
    }

    int insert(const std::string& key, Entry::Ptr entry, AccessOptions::Ptr, bool) override
    {
        m_fakeStorage[m_table].insert(std::make_pair(key, entry));
        return 0;
    }

    int update(
        const std::string& key, Entry::Ptr entry, Condition::Ptr, AccessOptions::Ptr) override
    {
        m_fakeStorage[m_table][key] = entry;
        return 0;
    }

    void commitDB(int64_t _blockNumber)
    {
        for (auto pmainKey : m_fakeStorage)
        {
            for (auto psubKey : pmainKey.second)
            {
                auto entry = psubKey.second;
                entry->setNum(_blockNumber);
                entry->setField("_num_", std::to_string(_blockNumber));
            }
        }
    }

    std::string m_table;
    std::unordered_map<std::string, std::unordered_map<std::string, Entry::Ptr>> m_fakeStorage;
};

class MockMemoryTableFactory : public dev::storage::MemoryTableFactory
{
public:
    MockMemoryTableFactory() {}

    Table::Ptr openTable(const std::string& _table, bool = true, bool = false) override
    {
        UpgradableGuard l(x_name2Table);
        if (m_name2Table.count(_table))
        {
            return m_name2Table[_table];
        }
        UpgradeGuard ul(l);
        auto table = std::make_shared<MockTable>();
        table->m_table = _table;
        m_name2Table[_table] = table;
        return table;
    }

    void commitDB(h256 const&, int64_t _blockNumber) override
    {
        ReadGuard l(x_name2Table);
        for (auto it : m_name2Table)
        {
            it.second->commitDB(_blockNumber);
        }
    }

    std::map<std::string, std::shared_ptr<MockTable>> m_name2Table;
    mutable SharedMutex x_name2Table;
};

class MockBlockChainImp : public BlockChainImp
{
public:
    std::shared_ptr<dev::storage::TableFactory> getMemoryTableFactory(int64_t) override
    {
        return m_memoryTableFactory;
    }

    void setMemoryTableFactory(std::shared_ptr<dev::storage::TableFactory> _m)
    {
        m_memoryTableFactory = _m;
    }
    std::shared_ptr<dev::storage::TableFactory> m_memoryTableFactory;
};

class MockState : public StorageState
{
public:
    MockState() : StorageState(u256(0)) {}

    void dbCommit(h256 const&, int64_t) override {}
};

struct EmptyFixture
{
    EmptyFixture()
    {
        m_executiveContext = std::make_shared<ExecutiveContext>();
        m_blockChainImp = std::make_shared<MockBlockChainImp>();
        mockMemoryTableFactory = std::make_shared<MockMemoryTableFactory>();
        m_storageStateFactory = std::make_shared<StorageStateFactory>(0x0);

        m_blockChainImp->setMemoryTableFactory(mockMemoryTableFactory);
        m_blockChainImp->setStateFactory(m_storageStateFactory);
        m_executiveContext->setMemoryTableFactory(mockMemoryTableFactory);
        m_executiveContext->setState(std::make_shared<MockState>());

        auto initParam = std::make_shared<dev::ledger::LedgerParam>();
        initParam->mutableGenesisMark() = "";
        initParam->mutableConsensusParam().sealerList = dev::h512s();
        initParam->mutableConsensusParam().observerList = dev::h512s();
        initParam->mutableConsensusParam().consensusType = "";
        initParam->mutableStorageParam().type = "";
        initParam->mutableStateParam().type = "";
        initParam->mutableConsensusParam().maxTransactions = 0;
        initParam->mutableTxParam().txGasLimit = 0;
        initParam->mutableGenesisParam().timeStamp = 0;
        m_blockChainImp->checkAndBuildGenesisBlock(initParam);
    }

    std::shared_ptr<MockMemoryTableFactory> mockMemoryTableFactory;
    std::shared_ptr<MockBlockChainImp> m_blockChainImp;
    std::shared_ptr<ExecutiveContext> m_executiveContext;
    std::shared_ptr<StorageStateFactory> m_storageStateFactory;
};

struct MemoryTableFactoryFixture : EmptyFixture
{
    MemoryTableFactoryFixture()
    {
        m_version = g_BCOSConfig.version();
        m_supportedVersion = g_BCOSConfig.supportedVersion();

        g_BCOSConfig.setSupportedVersion("2.2.0", V2_2_0);
        m_fakeBlock = std::make_shared<FakeBlock>(5);
        insertGenesisBlock(m_fakeBlock);
    }

    void insertGenesisBlock(std::shared_ptr<FakeBlock> _fakeBlock)
    {
        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField("value", "0");
        auto table = mockMemoryTableFactory->openTable(SYS_CURRENT_STATE);
        table->update(SYS_KEY_CURRENT_NUMBER, entry, nullptr, nullptr);

        entry = std::make_shared<Entry>();
        entry->setField("value",
            boost::lexical_cast<std::string>(_fakeBlock->getBlock()->transactions()->size()));
        table = mockMemoryTableFactory->openTable(SYS_CURRENT_STATE);
        table->update(SYS_KEY_TOTAL_TRANSACTION_COUNT, entry, nullptr, nullptr);

        entry = std::make_shared<Entry>();
        entry->setField("value", c_commonHashPrefix);
        table = mockMemoryTableFactory->openTable(SYS_NUMBER_2_HASH);
        table->update("0", entry, nullptr, nullptr);

        entry = std::make_shared<Entry>();
        bytes encodedData;
        _fakeBlock->m_block->encode(encodedData);
        entry->setField("value", encodedData.data(), encodedData.size());
        table = mockMemoryTableFactory->openTable(SYS_HASH_2_BLOCK);
        table->update(c_commonHash, entry, nullptr, nullptr);

        entry = std::make_shared<Entry>();
        entry->setField("value", "0");
        entry->setField("index", "0");
        table = mockMemoryTableFactory->openTable(SYS_TX_HASH_2_BLOCK);
        table->update(c_commonHash, entry, nullptr, nullptr);
    }

    ~MemoryTableFactoryFixture()
    {
        g_BCOSConfig.setSupportedVersion(m_supportedVersion, m_version);
    }

    std::shared_ptr<FakeBlock> m_fakeBlock;
    dev::VERSION m_version;
    std::string m_supportedVersion;
};

BOOST_FIXTURE_TEST_SUITE(BlockChainImpl, MemoryTableFactoryFixture)

BOOST_AUTO_TEST_CASE(emptyChain)
{
    // special case
    EmptyFixture empty;

    BOOST_CHECK_EQUAL(empty.m_blockChainImp->number(), 0);
    BOOST_CHECK_EQUAL(empty.m_blockChainImp->totalTransactionCount().first, 0);
    BOOST_CHECK_EQUAL(empty.m_blockChainImp->totalTransactionCount().second, 0);
    BOOST_CHECK_NO_THROW(empty.m_blockChainImp->getCode(Address(0x0)));

    /// modify the hash of the empty block since "timestamp" has been added into groupMark
    BOOST_CHECK_EQUAL(empty.m_blockChainImp->numberHash(0),
        h256("0x2d1c730a81f92c9888f508a9c1a4cdeed7063b831f1a21d61a4d6d97584fc260"));

    BOOST_CHECK_EQUAL(
        empty.m_blockChainImp->getBlockByHash(h256(c_commonHashPrefix)), std::shared_ptr<Block>());
    BOOST_CHECK_EQUAL(*(empty.m_blockChainImp->getLocalisedTxByHash(h256(c_commonHashPrefix))),
        LocalisedTransaction(Transaction(), h256(0), -1));
    BOOST_CHECK_EQUAL(
        *(empty.m_blockChainImp->getTxByHash(h256(c_commonHashPrefix))), Transaction());
    BOOST_CHECK_EQUAL(
        crypto::Hash(
            empty.m_blockChainImp->getTransactionReceiptByHash(h256(c_commonHashPrefix))->rlp()),
        crypto::Hash(TransactionReceipt().rlp()));
    BOOST_CHECK_EQUAL(
        empty.m_blockChainImp->getLocalisedTxReceiptByHash(h256(c_commonHashPrefix))->hash(),
        h256(0));
}

BOOST_AUTO_TEST_CASE(number2hash)
{
    BOOST_CHECK_EQUAL(m_blockChainImp->numberHash(0), h256(c_commonHashPrefix));
}

BOOST_AUTO_TEST_CASE(getCode)
{
    auto code = m_blockChainImp->getCode(Address(0x0));
    BOOST_CHECK_EQUAL(code.size(), 0);
}

BOOST_AUTO_TEST_CASE(getBlockByHash)
{
    std::shared_ptr<dev::eth::Block> bptr =
        m_blockChainImp->getBlockByHash(h256(c_commonHashPrefix));

    BOOST_CHECK_EQUAL(bptr->getTransactionSize(), m_fakeBlock->getBlock()->getTransactionSize());
    BOOST_CHECK_EQUAL(bptr->getTransactionSize(), 5);
}

BOOST_AUTO_TEST_CASE(getBlockRLPByNumber)
{
    std::shared_ptr<bytes> bRLPptr = m_blockChainImp->getBlockRLPByNumber(0);

    std::shared_ptr<dev::eth::Block> bptr =
        m_blockChainImp->getBlockByHash(h256(c_commonHashPrefix));
    BOOST_CHECK(bptr->rlp() == *bRLPptr);
}

BOOST_AUTO_TEST_CASE(getLocalisedTxByHash)
{
    Transaction::Ptr tx = m_blockChainImp->getLocalisedTxByHash(h256(c_commonHashPrefix));
    BOOST_CHECK_EQUAL(tx->hash(), (*m_fakeBlock->m_transaction)[0]->hash());
}

BOOST_AUTO_TEST_CASE(getTxByHash)
{
    Transaction::Ptr tx = m_blockChainImp->getTxByHash(h256(c_commonHashPrefix));
    BOOST_CHECK_EQUAL(tx->hash(), (*m_fakeBlock->m_transaction)[0]->hash());
}

BOOST_AUTO_TEST_CASE(getTransactionReceiptByHash)
{
    auto txReceipt = m_blockChainImp->getTransactionReceiptByHash(h256(c_commonHashPrefix));

    BOOST_CHECK_EQUAL(crypto::Hash(txReceipt->rlp()),
        crypto::Hash((*m_fakeBlock->m_transactionReceipt)[0]->rlp()));
}

BOOST_AUTO_TEST_CASE(getLocalisedTxReceiptByHash)
{
    auto localisedTxReceipt =
        m_blockChainImp->getLocalisedTxReceiptByHash(h256(c_commonHashPrefix));

    BOOST_CHECK_EQUAL(localisedTxReceipt->hash(), h256(c_commonHashPrefix));
}

BOOST_AUTO_TEST_CASE(commitBlock)
{
    auto fakeBlock2 = std::make_shared<FakeBlock>(10);
    fakeBlock2->getBlock()->header().setNumber(m_blockChainImp->number());
    auto commitResult = m_blockChainImp->commitBlock(fakeBlock2->getBlock(), m_executiveContext);
    BOOST_CHECK(commitResult == CommitResult::ERROR_NUMBER);

    fakeBlock2->getBlock()->header().setNumber(m_blockChainImp->number() + 1);
    commitResult = m_blockChainImp->commitBlock(fakeBlock2->getBlock(), m_executiveContext);
    BOOST_CHECK(commitResult == CommitResult::ERROR_PARENT_HASH);

    fakeBlock2->getBlock()->header().setParentHash(
        m_blockChainImp->numberHash(m_blockChainImp->number()));
    commitResult = m_blockChainImp->commitBlock(fakeBlock2->getBlock(), m_executiveContext);
    BOOST_CHECK(commitResult == CommitResult::OK);
    BOOST_CHECK_EQUAL(m_blockChainImp->number(), 1);
    BOOST_CHECK_EQUAL(m_blockChainImp->totalTransactionCount().first, 15);
    BOOST_CHECK_EQUAL(m_blockChainImp->totalTransactionCount().second, 1);

    auto fakeBlock3 = std::make_shared<FakeBlock>(15);
    fakeBlock3->getBlock()->header().setNumber(m_blockChainImp->number() + 1);
    fakeBlock3->getBlock()->header().setParentHash(
        m_blockChainImp->numberHash(m_blockChainImp->number()));
    commitResult = m_blockChainImp->commitBlock(fakeBlock3->getBlock(), m_executiveContext);
    BOOST_CHECK(commitResult == CommitResult::OK);
    BOOST_CHECK_EQUAL(m_blockChainImp->number(), 2);
    BOOST_CHECK_EQUAL(m_blockChainImp->totalTransactionCount().first, 30);
    BOOST_CHECK_EQUAL(m_blockChainImp->totalTransactionCount().second, 2);
}

BOOST_AUTO_TEST_CASE(query)
{
    dev::h512s sealerList = m_blockChainImp->sealerList();
    BOOST_CHECK_EQUAL(sealerList.size(), 0);
    dev::h512s observerList = m_blockChainImp->observerList();
    BOOST_CHECK_EQUAL(observerList.size(), 0);
}

BOOST_FIXTURE_TEST_CASE(SM_emptyChain, SM_CryptoTestFixture)
{
    // special case
    EmptyFixture empty;

    BOOST_CHECK_EQUAL(empty.m_blockChainImp->number(), 0);
    BOOST_CHECK_EQUAL(empty.m_blockChainImp->totalTransactionCount().first, 0);
    BOOST_CHECK_EQUAL(empty.m_blockChainImp->totalTransactionCount().second, 0);
    BOOST_CHECK_NO_THROW(empty.m_blockChainImp->getCode(Address(0x0)));

    BOOST_CHECK_EQUAL(empty.m_blockChainImp->numberHash(0),
        h256("39b4e98c07189c1a1cc53d8159b294c6b58753e660aa52d3a25c5241cc6225f9"));

    BOOST_CHECK_EQUAL(
        empty.m_blockChainImp->getBlockByHash(h256(c_commonHashPrefix)), std::shared_ptr<Block>());
    BOOST_CHECK_EQUAL(*(empty.m_blockChainImp->getLocalisedTxByHash(h256(c_commonHashPrefix))),
        LocalisedTransaction(Transaction(), h256(0), -1));
    BOOST_CHECK_EQUAL(
        *(empty.m_blockChainImp->getTxByHash(h256(c_commonHashPrefix))), Transaction());
    BOOST_CHECK_EQUAL(
        crypto::Hash(
            empty.m_blockChainImp->getTransactionReceiptByHash(h256(c_commonHashPrefix))->rlp()),
        crypto::Hash(TransactionReceipt().rlp()));
    BOOST_CHECK_EQUAL(
        empty.m_blockChainImp->getLocalisedTxReceiptByHash(h256(c_commonHashPrefix))->hash(),
        h256(0));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
