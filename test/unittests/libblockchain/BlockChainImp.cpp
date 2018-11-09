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

    Entries::Ptr select(const std::string& key, Condition::Ptr condition) override
    {
        // std::cout << "key " << key << " table " << m_table << std::endl;
        Entries::Ptr entries = std::make_shared<Entries>();
        Entry::Ptr entry = std::make_shared<Entry>();

        if (m_table == "_sys_current_state_" && key == "current_number")
        {
            entry->setField("value", "1");
        }
        else if (m_table == "_sys_number_2_hash_" && key == "1")
        {
            entry->setField(
                "value", "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
        }
        else if (m_table == "_sys_hash_2_block_" &&
                 key == "067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e")
        {
            entry->setField("value", toHexPrefixed(m_fakeBlock->getBlockData()));
        }
        else if (m_table == "_sys_tx_hash_2_block_" &&
                 key == "067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e")
        {
            entry->setField("value", "1");
            entry->setField("index", "0");
        }
        entries->addEntry(entry);
        return entries;
    }

    virtual size_t insert(const std::string& key, Entry::Ptr entry) override { return 0; }
    virtual size_t update(const std::string& key, Entry::Ptr entry, Condition::Ptr condition)
    {
        return 0;
    }

    void setFakeBlock(std::shared_ptr<FakeBlock> fakeBlock) { m_fakeBlock = fakeBlock; }


    std::string m_table;
    std::shared_ptr<FakeBlock> m_fakeBlock;
};

class MockMemoryTableFactory : public dev::storage::MemoryTableFactory
{
public:
    virtual ~MockMemoryTableFactory() {}

    Table::Ptr openTable(const std::string& table)
    {
        MockTable::Ptr tableptr = std::make_shared<MockTable>();
        tableptr->m_table = table;
        tableptr->setFakeBlock(m_fakeBlock);
        return tableptr;
    }

    void setFakeBlock(std::shared_ptr<FakeBlock> fakeBlock) { m_fakeBlock = fakeBlock; }

    std::shared_ptr<FakeBlock> m_fakeBlock;
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
        mockMemoryTableFactory = std::make_shared<MockMemoryTableFactory>();
        mockMemoryTableFactory->setFakeBlock(m_fakeBlock);
        m_blockChainImp->setMemoryTableFactory(mockMemoryTableFactory);
        m_executiveContext->setMemoryTableFactory(mockMemoryTableFactory);
        m_executiveContext->setState(std::make_shared<MockState>());
        m_executiveContext->dbCommit();
        // std::cout << "blockChainImp " << m_blockChainImp << std::endl;
    }

    std::shared_ptr<MockMemoryTableFactory> mockMemoryTableFactory;
    std::shared_ptr<MockBlockChainImp> m_blockChainImp;
    std::shared_ptr<FakeBlock> m_fakeBlock;
    std::shared_ptr<ExecutiveContext> m_executiveContext;
};

BOOST_FIXTURE_TEST_SUITE(BlockChainImpl, MemoryTableFactoryFixture);

BOOST_AUTO_TEST_CASE(testnumber)
{
    // int64_t number() const override;
    // std::cout << "testnumber " << m_blockChainImp << std::endl;
    BOOST_CHECK_EQUAL(m_blockChainImp->number(), 1);
}

BOOST_AUTO_TEST_CASE(number2hash)
{
    // int64_t number() const override;
    BOOST_CHECK_EQUAL(m_blockChainImp->numberHash(1),
        h256("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e"));
}

BOOST_AUTO_TEST_CASE(getBlockByHash)
{
    std::shared_ptr<dev::eth::Block> bptr = m_blockChainImp->getBlockByHash(
        h256("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e"));
    // std::cout << " h256 "
    //          << h256("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e").hex()
    //          << std::endl;
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
    m_fakeBlock->getBlock().header().setNumber(m_blockChainImp->number());
    auto commitResult = m_blockChainImp->commitBlock(m_fakeBlock->getBlock(), m_executiveContext);
    BOOST_CHECK(commitResult == CommitResult::ERROR_NUMBER);

    m_fakeBlock->getBlock().header().setNumber(m_blockChainImp->number() + 1);
    commitResult = m_blockChainImp->commitBlock(m_fakeBlock->getBlock(), m_executiveContext);
    BOOST_CHECK(commitResult == CommitResult::ERROR_PARENT_HASH);

    m_fakeBlock->getBlock().header().setParentHash(
        h256("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e"));
    commitResult = m_blockChainImp->commitBlock(m_fakeBlock->getBlock(), m_executiveContext);
    BOOST_CHECK(commitResult == CommitResult::OK);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
