#include "MemoryStorage.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Storage.h>
#include <libstorage/Table.h>
#include <libstorage/TableFactoryPrecompiled.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;

namespace test_TableFactoryPrecompiled
{
class MockMemoryTableFactory : public dev::storage::MemoryTableFactory
{
public:
    virtual ~MockMemoryTableFactory(){};

    virtual dev::storage::Table::Ptr openTable(h256 blockHash, int num, const std::string& table)
    {
        return dev::storage::Table::Ptr();
    }
};

class MockPrecompiledEngine : public dev::blockverifier::ExecutiveContext
{
public:
    virtual ~MockPrecompiledEngine() {}

    virtual Address registerPrecompiled(BlockInfo info, Precompiled::Ptr p) { return ++address; }

    h160 address;
};

struct TableFactoryPrecompiledFixture
{
    TableFactoryPrecompiledFixture()
    {
        context = std::make_shared<MockPrecompiledEngine>();
        BlockInfo blockInfo{h256(0x001), 1, h256(0x001)};
        context->setBlockInfo(blockInfo);
        tableFactoryPrecompiled = std::make_shared<dev::blockverifier::TableFactoryPrecompiled>();
        memStorage = std::make_shared<MemoryStorage>();
        auto mockMemoryTableFactory = std::make_shared<MockMemoryTableFactory>();
        mockMemoryTableFactory->setStateStorage(memStorage);
        tableFactoryPrecompiled->setMemoryTableFactory(mockMemoryTableFactory);
    }

    ~TableFactoryPrecompiledFixture() {}
    Storage::Ptr memStorage;
    dev::blockverifier::TableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    ExecutiveContext::Ptr context;
    int addressCount = 0x10000;
};

BOOST_FIXTURE_TEST_SUITE(TableFactoryPrecompiled, TableFactoryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(tableFactoryPrecompiled->toString(context) == "TableFactory");
}

BOOST_AUTO_TEST_CASE(call_afterBlock)
{
    // createTable
    dev::eth::ContractABI abi;
    bytes param =
        abi.abiIn("createTable(string,string,string)", "t_test", "id", "item_name,item_id");
    bytes out = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    Address addressOut;
    abi.abiOut(&out, addressOut);
    BOOST_TEST(addressOut == Address(0x1));

    // createTable exist
    param = abi.abiIn("createTable(string,string,string)", "t_test", "id", "item_name,item_id");
    out = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    abi.abiOut(&out, addressOut);
    BOOST_TEST(addressOut <= Address(0x2));

    // openTable not exist
    param.clear();
    out.clear();
    param = abi.abiIn("openTable(string)", "t_poor");
    out = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    addressOut.clear();
    abi.abiOut(&out, addressOut);
    BOOST_TEST(addressOut == Address(0x0));

    param.clear();
    out.clear();
    param = abi.abiIn("openTable(string)", "t_test");
    out = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    addressOut.clear();
    abi.abiOut(&out, addressOut);
    BOOST_TEST(addressOut == Address(++addressCount));

    auto tablePrecompiled =
        std::dynamic_pointer_cast<TablePrecompiled>(context->getPrecompiled(addressOut));
    auto memTable = tablePrecompiled->getTable();
    auto entry = memTable->newEntry();
    entry->setField("id", "张三");
    entry->setField("item_id", "4");
    entry->setField("item_name", "M5");
    entry->setStatus(Entry::Status::NORMAL);
    memTable->insert("张三", entry);
}

BOOST_AUTO_TEST_CASE(hash)
{
    h256 h = tableFactoryPrecompiled->hash();
    BOOST_TEST(h == h256());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_TableFactoryPrecompiled
