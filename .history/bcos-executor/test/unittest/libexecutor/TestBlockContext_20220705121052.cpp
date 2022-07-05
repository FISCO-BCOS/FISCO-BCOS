#pragma once
#include "../../../src/executive/BlockContext.h"
#include "../../../src/executive/ExecutiveFactory.h"
#include "../../../src/executive/ExecutiveFlowInterface.h"
#include "../mock/MockExecutiveFlow.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/ProtocolTypeDef.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-table/src/StateStorage.h"
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <functional>
#include <memory>
#include <stack>
#include <string_view>


using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace bcos
{
namespace test
{

struct BlockContextFixture
{
    BlockContextFixture()
    {
        blockContext = std::make_shared<BlockContext>(
            nullptr, nullptr, 0, h256(), 0, 0, FiscoBcosScheduleV4, false, false);
    }
    std::make_shared<bcos::executor::BlockContext> blockContext;
};

BOOST_AUTO_TEST_SUITE(TestBlockContext, BlockContextFixture)

BOOST_AUTO_TEST_CASE(BlockContextTest)
{
    auto codeAddressArr = [
        "addr0", "addr1", "addr2", "addr3", "addr4", "addr5", "addr6", "addr7", "addr8", "addr9"
    ];
    auto executiveFlowName = [
        "flow0", "flow1", "flow2", "flow3", "flow4", "flow5", "flow6", "flow7", "flow8", "flow9"
    ];
    // BlockContext::Ptr blockContext = std::make_shared<bcos::executor::BlockContext>(
    //     nullptr, nullptr, 0, h256(), 0, 0, FiscoBcosScheduleV4, false, false);

    BOOST_CHECK(blockContext->storage() == nullptr);
    BOOST_CHECK(blockContext->lastStorage() == nullptr);
    BOOST_CHECK(!blockContext->isWarm());
    BOOST_CHECK(!blockContext->isAuthCheck());
    // BOOST_CHECK(blockContext->hash() != nullptr);
    BOOST_CHECK_EQUAL(blockContext->number(), 0);
    BOOST_CHECK_EQUAL(blockContext->timestamp(), 0);
    BOOST_CHECK_EQUAL(blockContext->version(), 0);
    BOOST_CHECK_EQUAL(blockContext->gasLimit(), 3000000000);

    for (int i = 0; i < 10; ++i)
    {
        MockExecutiveFlow::Ptr Executiveflow =
            std::make_shared<MockExecutiveFlow>(executiveFlowName[i]);
        BlockContext->setExecutiveFlow(codeAddressArr[i], ExecutiveFlow);
    }
    int count = 0;
    for (int i = 0; i < 10; ++i)
    {
        auto executiveFlow = BlockContext->getExecutiveFlow(codeAddressArr[i]);
        if (ExecutiveFlow->getname() == executiveFlowName[i])
            ++count;
    }
    BOOST_CHECK_EQUAL(count, 10);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
}
