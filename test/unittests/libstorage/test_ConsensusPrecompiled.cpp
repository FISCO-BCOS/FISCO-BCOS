// "Copyright [2018] <fisco-dev>"
#include "../libstorage/MemoryStorage.h"
#include "Common.h"
#include "MemoryStorage2.h"
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/ConsensusPrecompiled.h>
#include <libstorage/CachedStorage.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactoryFactory2.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::storage;
using namespace bcos::storagestate;
using namespace bcos::precompiled;

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
        // auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory2>();
        auto memStorage = std::make_shared<MemoryStorage2>();
        cachedStorage = std::make_shared<CachedStorage>();
        cachedStorage->setBackend(memStorage);
        tableFactoryFactory->setStorage(cachedStorage);

        // factory.setStateStorage(storage);
        factory.setStateStorage(cachedStorage);
        factory.setStateFactory(storageStateFactory);
        factory.setTableFactoryFactory(tableFactoryFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        consensusPrecompiled = std::make_shared<ConsensusPrecompiled>();
        memoryTableFactory = context->getMemoryTableFactory();

        auto precompiledGasFactory = std::make_shared<bcos::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<bcos::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        consensusPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~ConsensusPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    CachedStorage::Ptr cachedStorage;
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
    auto callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    auto table = memoryTableFactory->openTable(SYS_CONSENSUS);
    auto condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID1);
    auto entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);

    BOOST_TEST(entries->get(0)->getField(NODE_TYPE) == NODE_TYPE_SEALER);

    LOG(INFO) << "Add the same node to sealer";
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
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
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    condition = table->newCondition();
    condition->EQ(NODE_TYPE, NODE_TYPE_SEALER);
    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 2u);

    LOG(INFO) << "Add the second node to observer";
    in = abi.abiIn("addObserver(string)", nodeID2);
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
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
    auto table = memoryTableFactory->openTable(SYS_CURRENT_STATE);
    auto entry = table->newEntry();
    entry->setField(SYS_VALUE, "0");
    entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
    table->insert(SYS_KEY_CURRENT_NUMBER, entry);
    memoryTableFactory->commitDB(h256(0x100), 0);
    eth::ContractABI abi;
    std::string nodeID1(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aae");

    LOG(INFO) << "Add a new node to sealer";
    bytes in = abi.abiIn("addSealer(string)", nodeID1);
    auto callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    std::string nodeID2(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aad");

    LOG(INFO) << "Add a second node to sealer";
    in = abi.abiIn("addSealer(string)", nodeID2);
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    table = memoryTableFactory->openTable(SYS_CONSENSUS);
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

    table = memoryTableFactory->openTable(SYS_CURRENT_STATE);
    entry = table->newEntry();
    entry->setField(SYS_VALUE, "0");
    entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
    table->insert(SYS_KEY_CURRENT_NUMBER, entry);

    memoryTableFactory->commitDB(h256(0x122), 998);

    LOG(INFO) << "Remove the sealer node";
    in = abi.abiIn("remove(string)", nodeID1);
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    table = memoryTableFactory->openTable(SYS_CURRENT_STATE);
    entry = table->newEntry();
    entry->setField(SYS_VALUE, "0");
    entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
    table->insert(SYS_KEY_CURRENT_NUMBER, entry);
    memoryTableFactory->commitDB(h256(0x123), 999);

    table = memoryTableFactory->openTable(SYS_CONSENSUS);
    condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID1);
    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 0u);

    LOG(INFO) << "Remove the sealer node again";
    in = abi.abiIn("remove(string)", nodeID1);
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 0u);

    LOG(INFO) << "Add the node again";
    in = abi.abiIn("addSealer(string)", nodeID1);
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    entries = table->select(PRI_KEY, condition);
    BOOST_TEST(entries->size() == 1u);
    table = memoryTableFactory->openTable(SYS_CURRENT_STATE);
    entry = table->newEntry();
    entry->setField(SYS_VALUE, "0");
    entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
    table->insert(SYS_KEY_CURRENT_NUMBER, entry);
    memoryTableFactory->commitDB(h256(0x124), 1000);
}

BOOST_AUTO_TEST_CASE(TestMultiAddAndRemove)
{
    auto table = memoryTableFactory->openTable(SYS_CURRENT_STATE);
    auto entry = table->newEntry();
    entry->setField(SYS_VALUE, "0");
    entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
    table->insert(SYS_KEY_CURRENT_NUMBER, entry);
    memoryTableFactory->commitDB(h256(0x100), 0);
    eth::ContractABI abi;
    std::string nodeID1(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aae");

    LOG(INFO) << "Add a new node to sealer";
    bytes in = abi.abiIn("addSealer(string)", nodeID1);
    auto callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);
    table = memoryTableFactory->openTable(SYS_CURRENT_STATE);
    entry = table->newEntry();
    entry->setField(SYS_VALUE, "0");
    entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
    table->insert(SYS_KEY_CURRENT_NUMBER, entry);
    memoryTableFactory->commitDB(h256(0x1), 1);

    std::string nodeID2(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aad");

    LOG(INFO) << "Add a second node to sealer";
    in = abi.abiIn("addSealer(string)", nodeID2);
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);
    table = memoryTableFactory->openTable(SYS_CURRENT_STATE);
    entry = table->newEntry();
    entry->setField(SYS_VALUE, "0");
    entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
    table->insert(SYS_KEY_CURRENT_NUMBER, entry);
    memoryTableFactory->commitDB(h256(0x2), 2);

    for (size_t i = 3; i < 1030; ++i)
    {
        in = abi.abiIn("addSealer(string)", nodeID2);
        callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
        count = 0;
        abi.abiOut(bytesConstRef(&out), count);
        BOOST_TEST(count == 1u);
        auto table = memoryTableFactory->openTable(SYS_CURRENT_STATE);
        auto entry = table->newEntry();
        entry->setField(SYS_VALUE, "0");
        entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
        table->insert(SYS_KEY_CURRENT_NUMBER, entry);
        memoryTableFactory->commitDB(h256(i), i);
    }

    for (size_t i = 1030; i < 2030; ++i)
    {
        in = abi.abiIn("remove(string)", nodeID2);
        callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
        count = 0;
        abi.abiOut(bytesConstRef(&out), count);
        auto table = memoryTableFactory->openTable(SYS_CURRENT_STATE);
        auto entry = table->newEntry();
        entry->setField(SYS_VALUE, "0");
        entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
        table->insert(SYS_KEY_CURRENT_NUMBER, entry);
        memoryTableFactory->commitDB(h256(i), i);
    }
}

BOOST_AUTO_TEST_CASE(TestErrorNodeID)
{
    eth::ContractABI abi;
    std::string nodeID("12345678");
    bytes in = abi.abiIn("addSealer(string)", nodeID);
    auto callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_TEST(count == CODE_INVALID_NODEID);
    }
    else
    {
        BOOST_TEST(count == -CODE_INVALID_NODEID);
    }
    in = abi.abiIn("addObserver(string)", nodeID);
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_TEST(count == CODE_INVALID_NODEID);
    }
    else
    {
        BOOST_TEST(count == -CODE_INVALID_NODEID);
    }
    in = abi.abiIn("remove(string)", nodeID);
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_TEST(count == CODE_INVALID_NODEID);
    }
    else
    {
        BOOST_TEST(count == -CODE_INVALID_NODEID);
    }
}

BOOST_AUTO_TEST_CASE(TestRemoveLastSealer)
{
    LOG(INFO) << "Add a sealer node and a observer node";
    eth::ContractABI abi;
    std::string nodeID1(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aae");

    bytes in = abi.abiIn("addSealer(string)", nodeID1);
    auto callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    std::string nodeID2(
        "10bf7d8cdeff9b0e85a035b9138e06c6cab68e21767872b2ebbdb14701464c53a4d435b5648bedb18c7bb1ae68"
        "fb6b32df4cf4fbadbccf7123b4dce271157aab");
    in = abi.abiIn("addObserver(string)", nodeID2);
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    LOG(INFO) << "Try to remove the sealer node";
    in = abi.abiIn("remove(string)", nodeID1);
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_TEST(count == CODE_LAST_SEALER);
    }
    else
    {
        BOOST_TEST(count == -CODE_LAST_SEALER);
    }
    in = abi.abiIn("addObserver(string)", nodeID1);
    callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_TEST(count == CODE_LAST_SEALER);
    }
    else
    {
        BOOST_TEST(count == -CODE_LAST_SEALER);
    }
}

BOOST_AUTO_TEST_CASE(errFunc)
{
    eth::ContractABI abi;
    bytes in = abi.abiIn("add(string)", std::string("test"));
    auto callResult = consensusPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_ConsensusPrecompiled
