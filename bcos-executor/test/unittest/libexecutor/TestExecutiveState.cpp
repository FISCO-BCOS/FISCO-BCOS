#include "../../../src/CallParameters.h"
#include "ExecutiveFactory.h"
#include "ExecutiveState.h"
#include "TransactionExecutive.h"
using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace bcos
{
namespace test
{
struct ExecutiveStateFixture
{
    ExecutiveStateFixture()
    {
        auto input = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
        input->codeAddress = "aabbccddee";
        input->contextID = 1;
        input->seq = 1;
    };
    std::shared_ptr<TransactionExecutive> m_executive;
    std::shared_ptr<ExecutiveState> executiveState;
    CallParameters::UniquePtr m_input;
    ExecutiveFactory::Ptr m_executiveFactory;
};
BOOST_AUTO_TEST_SUITE(TestExecutiveState, ExecutiveStateFixture);
// BOOST_AUTO_TEST_CASE(goTest)
// {
//     ExecutiveFactory::Ptr executiveFactory = std::make_shared<ExecutiveFactory>();
//     ExecutiveState::Ptr executiveState = std::make_shared<ExecutiveState>(executiveFactory,
//     input); executiveState->go();
// }
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos