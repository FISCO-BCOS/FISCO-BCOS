// "Copyright [2018] <fisco-dev>"
#include "../libstorage/MemoryStorage.h"
#include "Common.h"
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MinerPrecompiled.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;

namespace test_MinerPrecompiled
{
struct MinerPrecompiledFixture
{
    MinerPrecompiledFixture()
    {
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        context = std::make_shared<ExecutiveContext>();
        ExecutiveContextFactory factory;
        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        minerPrecompiled = std::make_shared<MinerPrecompiled>();
        memoryTableFactory = context->getMemoryTableFactory();
    }

    ~MinerPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    MemoryTableFactory::Ptr memoryTableFactory;
    MinerPrecompiled::Ptr minerPrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(MinerPrecompiled, MinerPrecompiledFixture)

BOOST_AUTO_TEST_CASE(call_add)
{
    eth::ContractABI abi;
    std::string nodeID(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aae");
    bytes in = abi.abiIn("add(string)", nodeID);
    bytes out = minerPrecompiled->call(context, bytesConstRef(&in));
    auto table = memoryTableFactory->openTable("_sys_miners_");
    auto condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID);
    auto entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);
    in = abi.abiIn("remove(string)", nodeID);
    out = minerPrecompiled->call(context, bytesConstRef(&in));
    condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID);
    condition->EQ(NODE_TYPE, NODE_TYPE_OBSERVER);
    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);
}

BOOST_AUTO_TEST_CASE(call_remove)
{
    eth::ContractABI abi;
    std::string nodeID(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aae");
    bytes in = abi.abiIn("remove(string)", nodeID);
    bytes out = minerPrecompiled->call(context, bytesConstRef(&in));
    auto table = memoryTableFactory->openTable("_sys_miners_");
    auto condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID);
    condition->EQ(NODE_TYPE, NODE_TYPE_MINER);
    auto entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 0u);
    condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID);
    condition->EQ(NODE_TYPE, NODE_TYPE_OBSERVER);
    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);
    in = abi.abiIn("add(string)", nodeID);
    out = minerPrecompiled->call(context, bytesConstRef(&in));
    condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID);
    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);
}

BOOST_AUTO_TEST_CASE(wrong_nodeid)
{
    eth::ContractABI abi;
    std::string nodeID(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aa");
    bytes in = abi.abiIn("remove(string)", nodeID);
    bytes out = minerPrecompiled->call(context, bytesConstRef(&in));
    in = abi.abiIn("add(string)", nodeID);
    out = minerPrecompiled->call(context, bytesConstRef(&in));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_MinerPrecompiled
