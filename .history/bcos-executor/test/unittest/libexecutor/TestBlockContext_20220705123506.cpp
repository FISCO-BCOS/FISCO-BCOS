#include "../../../src/executive/BlockContext.h"
#include "../../../src/executive/ExecutiveFactory.h"
#include "../../../src/executive/ExecutiveFlowInterface.h"
#include "../mock/MockExecutiveFlow.h"
#include "bcos-table/src/StateStorage.h"
#include <tbb/concurrent_unordered_map.h>
#include <boost/test/unit_test.hpp>


using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace bcos
{
namespace test
{

// struct BlockContextFixture
// {
//     BlockContextFixture()
//     {
//         blockContext = std::make_shared<BlockContext>(
//             nullptr, nullptr, 0, h256(), 0, 0, FiscoBcosScheduleV4, false, false);
//     }
//     std::make_shared<bcos::executor::BlockContext> blockContext;
// };

BOOST_AUTO_TEST_SUITE(TestBlockContext)

BOOST_AUTO_TEST_CASE(BlockContextTest)
{
    auto codeAddressArr = std::vector<std::string>{
        "addr0", "addr1", "addr2", "addr3", "addr4", "addr5", "addr6", "addr7", "addr8", "addr9"};
    auto executiveFlowName = std::vector<std::string>{
        "flow0", "flow1", "flow2", "flow3", "flow4", "flow5", "flow6", "flow7", "flow8", "flow9"};
    BlockContext::Ptr blockContext = std::make_shared<bcos::executor::BlockContext>(
        nullptr, nullptr, 0, h256(), 0, 0, FiscoBcosScheduleV4, false, false);

    BOOST_CHECK(blockContext->storage() == nullptr);
    BOOST_CHECK(blockContext->lastStorage() == nullptr);
    BOOST_CHECK(!blockContext->isWasm());
    BOOST_CHECK(!blockContext->isAuthCheck());
    // BOOST_CHECK(blockContext->hash() != nullptr);
    BOOST_CHECK_EQUAL(blockContext->number(), 0);
    BOOST_CHECK_EQUAL(blockContext->timestamp(), 0);
    BOOST_CHECK_EQUAL(blockContext->blockVersion(), 0);
    BOOST_CHECK_EQUAL(blockContext->gasLimit(), 3000000000);

    for (int i = 0; i < 10; ++i)
    {
        MockExecutiveFlow::Ptr Executiveflow =
            std::make_shared<MockExecutiveFlow>(executiveFlowName[i]);
        blockContext->setExecutiveFlow(codeAddressArr[i], ExecutiveFlow);
    }
    int count = 0;
    for (int i = 0; i < 10; ++i)
    {
        auto executiveFlow = blockContext->getExecutiveFlow(codeAddressArr[i]);
        if (executiveFlow->getname() == executiveFlowName[i])
            ++count;
    }
    BOOST_CHECK_EQUAL(count, 10);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
