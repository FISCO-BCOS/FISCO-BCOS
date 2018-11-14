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
#include <libblockchain/BlockChainImp.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/CommonData.h>
#include <libethcore/Block.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/Transaction.h>
#include <libstorage/Common.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstoragestate/StorageState.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libethcore/FakeBlock.h>
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

class MockTable : public dev::storage::MemoryTable
{
public:
    typedef std::shared_ptr<MockTable> Ptr;

    MockTable(std::shared_ptr<FakeBlock> _fakeBlock)
    {
        m_fakeStorage[SYS_CURRENT_STATE] = std::unordered_map<std::string, Entry::Ptr>();
        m_fakeStorage[SYS_NUMBER_2_HASH] = std::unordered_map<std::string, Entry::Ptr>();
        m_fakeStorage[SYS_HASH_2_BLOCK] = std::unordered_map<std::string, Entry::Ptr>();
        m_fakeStorage[SYS_TX_HASH_2_BLOCK] = std::unordered_map<std::string, Entry::Ptr>();

        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField(
            "value", "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
        m_fakeStorage[SYS_NUMBER_2_HASH]["0"] = entry;

        entry = std::make_shared<Entry>();
        entry->setField("value", toHexPrefixed(_fakeBlock->getBlockData()));
        m_fakeStorage[SYS_HASH_2_BLOCK]
                     ["067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e"] = entry;

        entry = std::make_shared<Entry>();
        entry->setField("value", "0");
        entry->setField("index", "0");
        m_fakeStorage[SYS_TX_HASH_2_BLOCK]
                     ["067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e"] = entry;
    }

    virtual Entries::Ptr select(const std::string& key, Condition::Ptr condition) override
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

    virtual size_t insert(const std::string& key, Entry::Ptr entry) override
    {
        m_fakeStorage[m_table].insert(std::make_pair(key, entry));
        return 0;
    }

    virtual size_t update(const std::string& key, Entry::Ptr entry, Condition::Ptr condition)
    {
        m_fakeStorage[m_table][key] = entry;
        return 0;
    }

    std::string m_table;
    std::unordered_map<std::string, std::unordered_map<std::string, Entry::Ptr>> m_fakeStorage;
};

class MockMemoryTableFactory : public dev::storage::MemoryTableFactory
{
public:
    MockMemoryTableFactory(std::shared_ptr<MockTable> _mockTable) { m_mockTable = _mockTable; }

    Table::Ptr openTable(const std::string& _table)
    {
        m_mockTable->m_table = _table;
        return m_mockTable;
    }

    std::shared_ptr<MockTable> m_mockTable;
};

class MockBlockChainImp : public BlockChainImp
{
public:
    std::shared_ptr<dev::storage::MemoryTableFactory> getMemoryTableFactory() override
    {
        return m_memoryTableFactory;
    }

    void setMemoryTableFactory(std::shared_ptr<dev::storage::MemoryTableFactory> _m)
    {
        m_memoryTableFactory = _m;
    }
    std::shared_ptr<dev::storage::MemoryTableFactory> m_memoryTableFactory;
};

class MockState : public StorageState
{
public:
    MockState() : StorageState(u256(0)) {}

    void dbCommit(h256 const& _blockHash, int64_t _blockNumber) override {}
};

struct MemoryTableFactoryFixture
{
    MemoryTableFactoryFixture()
    {
        m_executiveContext = std::make_shared<ExecutiveContext>();
        m_blockChainImp = std::make_shared<MockBlockChainImp>();
        m_fakeBlock = std::make_shared<FakeBlock>(5);
        m_mockTable = std::make_shared<MockTable>(m_fakeBlock);
        mockMemoryTableFactory = std::make_shared<MockMemoryTableFactory>(m_mockTable);

        m_blockChainImp->setMemoryTableFactory(mockMemoryTableFactory);
        m_executiveContext->setMemoryTableFactory(mockMemoryTableFactory);
        m_executiveContext->setState(std::make_shared<MockState>());
    }

    std::shared_ptr<MockMemoryTableFactory> mockMemoryTableFactory;
    std::shared_ptr<MockBlockChainImp> m_blockChainImp;
    std::shared_ptr<FakeBlock> m_fakeBlock;
    std::shared_ptr<MockTable> m_mockTable;
    std::shared_ptr<ExecutiveContext> m_executiveContext;
};

BOOST_FIXTURE_TEST_SUITE(BlockChainImpl, MemoryTableFactoryFixture);

BOOST_AUTO_TEST_CASE(number2hash)
{
    // int64_t number() const override;
    BOOST_CHECK_EQUAL(m_blockChainImp->numberHash(0),
        h256("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e"));
}

BOOST_AUTO_TEST_CASE(getBlockByHash)
{
    std::shared_ptr<dev::eth::Block> bptr = m_blockChainImp->getBlockByHash(
        h256("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e"));

    BOOST_CHECK_EQUAL(bptr->getTransactionSize(), m_fakeBlock->getBlock().getTransactionSize());
    BOOST_CHECK_EQUAL(bptr->getTransactionSize(), 5);
}

BOOST_AUTO_TEST_CASE(getLocalisedTxByHash)
{
    Transaction tx = m_blockChainImp->getLocalisedTxByHash(
        h256("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e"));
    BOOST_CHECK_EQUAL(tx.sha3(), m_fakeBlock->m_transaction[0].sha3());
}

BOOST_AUTO_TEST_CASE(getTxByHash)
{
    Transaction tx = m_blockChainImp->getTxByHash(
        h256("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e"));
    BOOST_CHECK_EQUAL(tx.sha3(), m_fakeBlock->m_transaction[0].sha3());
}

BOOST_AUTO_TEST_CASE(getTransactionReceiptByHash)
{
    auto txReceipt = m_blockChainImp->getTransactionReceiptByHash(
        h256("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e"));

    BOOST_CHECK_EQUAL(sha3(txReceipt.rlp()), sha3(m_fakeBlock->m_transactionReceipt[0].rlp()));
}

BOOST_AUTO_TEST_CASE(commitBlock)
{
    BOOST_CHECK_EQUAL(m_blockChainImp->number(), 0);
    BOOST_CHECK_EQUAL(m_blockChainImp->totalTransactionCount(), 0);

    auto fakeBlock2 = std::make_shared<FakeBlock>(10);
    fakeBlock2->getBlock().header().setNumber(m_blockChainImp->number());
    auto commitResult = m_blockChainImp->commitBlock(fakeBlock2->getBlock(), m_executiveContext);
    BOOST_CHECK(commitResult == CommitResult::ERROR_NUMBER);

    fakeBlock2->getBlock().header().setNumber(m_blockChainImp->number() + 1);
    commitResult = m_blockChainImp->commitBlock(fakeBlock2->getBlock(), m_executiveContext);
    BOOST_CHECK(commitResult == CommitResult::ERROR_PARENT_HASH);

    fakeBlock2->getBlock().header().setParentHash(
        m_blockChainImp->numberHash(m_blockChainImp->number()));
    commitResult = m_blockChainImp->commitBlock(fakeBlock2->getBlock(), m_executiveContext);
    BOOST_CHECK(commitResult == CommitResult::OK);
    BOOST_CHECK_EQUAL(m_blockChainImp->number(), 1);
    BOOST_CHECK_EQUAL(m_blockChainImp->totalTransactionCount(), 10);

    auto fakeBlock3 = std::make_shared<FakeBlock>(15);
    fakeBlock3->getBlock().header().setNumber(m_blockChainImp->number() + 1);
    fakeBlock3->getBlock().header().setParentHash(
        m_blockChainImp->numberHash(m_blockChainImp->number()));
    commitResult = m_blockChainImp->commitBlock(fakeBlock3->getBlock(), m_executiveContext);
    BOOST_CHECK(commitResult == CommitResult::OK);
    BOOST_CHECK_EQUAL(m_blockChainImp->number(), 2);
    BOOST_CHECK_EQUAL(m_blockChainImp->totalTransactionCount(), 25);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
