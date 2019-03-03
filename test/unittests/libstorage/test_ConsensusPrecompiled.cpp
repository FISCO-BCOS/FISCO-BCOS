// "Copyright [2018] <fisco-dev>"
#include "../libstorage/MemoryStorage.h"
#include "Common.h"
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/ConsensusPrecompiled.h>
#include <libstorage/MemoryTable.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;
using namespace dev::precompiled;

namespace test_ConsensusPrecompiled
{
struct ConsensusPrecompiledFixture
{
    ConsensusPrecompiledFixture()
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
        consensusPrecompiled = std::make_shared<ConsensusPrecompiled>();
        memoryTableFactory = context->getMemoryTableFactory();
    }

    ~ConsensusPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    MemoryTableFactory::Ptr memoryTableFactory;
    ConsensusPrecompiled::Ptr consensusPrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(ConsensusPrecompiled, ConsensusPrecompiledFixture)

BOOST_AUTO_TEST_CASE(TestAddNode)
{
    eth::ContractABI abi;
    std::string nodeID1(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aae");

    LOG(INFO) << "Add a new node to sealer";
    bytes in = abi.abiIn("addSealer(string)", nodeID1);
    bytes out = consensusPrecompiled->call(context, bytesConstRef(&in));
    u256 count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    auto table = memoryTableFactory->openTable(SYS_CONSENSUS);
    auto condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID1);
    auto entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);

    BOOST_TEST(entries->get(0)->getField(NODE_TYPE) == NODE_TYPE_SEALER);

    LOG(INFO) << "Add the same node to sealer";
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);

    LOG(INFO) << "Add another node to sealer";
    std::string nodeID2(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aa2");
    in = abi.abiIn("addSealer(string)", nodeID2);
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    condition = table->newCondition();
    condition->EQ(NODE_TYPE, NODE_TYPE_SEALER);
    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 2u);

    LOG(INFO) << "Add the second node to observer";
    in = abi.abiIn("addObserver(string)", nodeID2);
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID2);
    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);
    BOOST_TEST(entries->get(0)->getField(NODE_TYPE) == NODE_TYPE_OBSERVER);

    condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID1);
    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);
    BOOST_TEST(entries->get(0)->getField(NODE_TYPE) == NODE_TYPE_SEALER);
}

BOOST_AUTO_TEST_CASE(TestRemoveNode)
{
    eth::ContractABI abi;
    std::string nodeID1(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aae");

    LOG(INFO) << "Add a new node to sealer";
    bytes in = abi.abiIn("addSealer(string)", nodeID1);
    bytes out = consensusPrecompiled->call(context, bytesConstRef(&in));
    u256 count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    std::string nodeID2(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aad");

    LOG(INFO) << "Add a second node to sealer";
    in = abi.abiIn("addSealer(string)", nodeID2);
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    auto table = memoryTableFactory->openTable(SYS_CONSENSUS);
    auto condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID1);
    auto entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);
    BOOST_TEST(entries->get(0)->getField(NODE_TYPE) == NODE_TYPE_SEALER);

    condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID2);
    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);
    BOOST_TEST(entries->get(0)->getField(NODE_TYPE) == NODE_TYPE_SEALER);

    LOG(INFO) << "Remove the sealer node";
    in = abi.abiIn("remove(string)", nodeID1);
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID1);
    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 0u);

    LOG(INFO) << "Remove the sealer node again";
    in = abi.abiIn("remove(string)", nodeID1);
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 0u);

    LOG(INFO) << "Add the node again";
    in = abi.abiIn("addSealer(string)", nodeID1);
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);
}

BOOST_AUTO_TEST_CASE(TestErrorNodeID)
{
    eth::ContractABI abi;
    std::string nodeID("12345678");
    bytes in = abi.abiIn("addSealer(string)", nodeID);
    bytes out = consensusPrecompiled->call(context, bytesConstRef(&in));
    u256 count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 156u);
    in = abi.abiIn("addObserver(string)", nodeID);
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 156u);
    in = abi.abiIn("remove(string)", nodeID);
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 156u);
}

BOOST_AUTO_TEST_CASE(TestRemoveLastSealer)
{
    LOG(INFO) << "Add a sealer node and a observer node";
    eth::ContractABI abi;
    std::string nodeID1(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aae");

    bytes in = abi.abiIn("addSealer(string)", nodeID1);
    bytes out = consensusPrecompiled->call(context, bytesConstRef(&in));
    u256 count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    std::string nodeID2(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aab");
    in = abi.abiIn("addObserver(string)", nodeID2);
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    LOG(INFO) << "Try to remove the sealer node";
    in = abi.abiIn("remove(string)", nodeID1);
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 157u);

    in = abi.abiIn("addObserver(string)", nodeID1);
    out = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 157u);
}

BOOST_AUTO_TEST_CASE(errFunc)
{
    eth::ContractABI abi;
    bytes in = abi.abiIn("add(string)", "test");
    bytes out = consensusPrecompiled->call(context, bytesConstRef(&in));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_ConsensusPrecompiled
