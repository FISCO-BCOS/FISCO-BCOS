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
#include <libethcore/Block.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/Transaction.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactory.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::eth;
using namespace dev::storage;
using namespace dev::blockchain;
namespace dev
{
namespace test
{
//   const std::string m_extraDbName_currentState = "currentState";
//   const std::string m_keyValue_currentNumber = "currentNumber";
//   const std::string m_ValueName_currentNumber = "value";
//   const std::string m_ValueName = "value";
//   const std::string m_txHash2Block = "txHash2Block";
//   const std::string m_number2hash = "number2hash";
//   const std::string m_hash2Block = "hash2Block";

class MockTable : public dev::storage::MemoryTable
{
public:
    typedef std::shared_ptr<MockTable> Ptr;

    Entries::Ptr select(const std::string& key, Condition::Ptr condition) override
    {
        Entries::Ptr entries = std::make_shared<Entries>();
        Entry::Ptr entry = std::make_shared<Entry>();
        if (m_table == "currentState" && key == "currentNumber")
        {
            entry->setField("value", "1");
        }
        else if (m_table == "number2hash" && key == "1")
        {
            entry->setField("value", "0xaaabbbccc");
        }
        else if (m_table == "hash2Block" && key == "currentNumber")
        {
        }
        else if (m_table == "txHash2Block" && key == "currentNumber")
        {
        }
        entries->addEntry(entry);
        return entries;
    }


    std::string m_table;
};

class MockMemoryTableFactory : public dev::storage::MemoryTableFactory
{
public:
    virtual ~MockMemoryTableFactory() {}

    Table::Ptr openTable(h256 blockHash, int64_t num, const std::string& table)
    {
        MockTable::Ptr tableptr = std::make_shared<MockTable>();
        tableptr->m_table = table;
        return tableptr;
    }
};

struct MemoryTableFactoryFixture
{
    MemoryTableFactoryFixture()
    {
        m_blockChainImp = std::make_shared<BlockChainImp>();
        std::shared_ptr<MockMemoryTableFactory> mockMemoryTableFactory =
            std::make_shared<MockMemoryTableFactory>();
        m_blockChainImp->setMemoryTableFactory(mockMemoryTableFactory);
        std::cout << "blockChainImp " << m_blockChainImp << std::endl;
    }
    std::shared_ptr<BlockChainImp> m_blockChainImp;
};

BOOST_FIXTURE_TEST_SUITE(BlockChainImp1, MemoryTableFactoryFixture);


/// test constructors and operators
BOOST_AUTO_TEST_CASE(testnumber)
{
    // int64_t number() const override;
    std::cout << "testnumber " << m_blockChainImp << std::endl;
    BOOST_CHECK_EQUAL(m_blockChainImp->number(), 1);
}


/// test constructors and operators
BOOST_AUTO_TEST_CASE(number2hash)
{
    // int64_t number() const override;
    BOOST_CHECK_EQUAL(m_blockChainImp->numberHash(1), h256("0xaaabbbccc"));
}

BOOST_AUTO_TEST_CASE(number2hash)
{
    // int64_t number() const override;
    // std::shared_ptr<dev::eth::Block> getBlockByHash(dev::h256 const& _blockHash) override;
    // dev::eth::Transaction getTxByHash(dev::h256 const& _txHash) override;
    BOOST_CHECK_EQUAL(m_blockChainImp->numberHash(1), h256("0xaaabbbccc"));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
