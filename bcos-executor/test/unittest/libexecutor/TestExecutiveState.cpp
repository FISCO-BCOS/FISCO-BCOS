#include "../../../src/CallParameters.h"
#include "../../../src/executive/ExecutiveFactory.h"
#include "../../../src/executive/ExecutiveState.h"
#include "../../../src/executive/TransactionExecutive.h"
#include "../mock/MockExecutiveFactory.h"
#include "../mock/MockTransactionExecutive.h"

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
        auto input = std::make_unique<CallParameters>(CallParameters::Type::KEY_LOCK);
        input->staticCall = false;
        input->codeAddress = "aabbccddee";
        input->contextID = 1;
        input->seq = 1;
        executive = std::make_shared<MockTransactionExecutive>();
        executiveFactory = std::make_shared<MockExecutiveFactory>();
    };
    shared_ptr<MockTransactionExecutive> executive;
    shared_ptr<MockExecutiveFactory> executiveFactory;
    shared_ptr<ExecutiveState> executiveState;
    CallParameters::UniquePtr input;
};
BOOST_AUTO_TEST_SUITE(TestExecutiveState, ExecutiveStateFixture);
BOOST_AUTO_TEST_CASE(goTest)
{
    for (int8_t i = 0; i < 4; ++i)
    {
        auto callParameters = std::make_unique<CallParameters>();
        callParameters->staticCall = false;
        callParameters->codeAddress = "aabbccddee";
        callParameters->contextID = i;
        callParameters->seq = i;
        callParameters->type = i;
        auto executiveState = std::make_shared<ExecutiveState>(executiveFactory, callParameters);
        executiveState->go();
        if (i == 0)
        {
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RUN);
        }
        else if (i == 1)
        {
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::PAUSED);
        }
        else if (i == 2)
        {
            executiveState->setResumeParam(callParameters);
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RESUME);
        }
        else
        {
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::FINISHED);
        }
        executiveState->go();
    }
}

BOOST_AUTO_TEST_CASE(setResumeParamTest)
{
    ExecutiveState::Ptr executiveState = std::make_shared<ExecutiveState>(executiveFactory, input);
    BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RUN);
    executiveState->setResumeParam(input);
    BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RESUME);
}

BOOST_AUTO_TEST_CASE(appendKeyLocks)
{
    std::vector<std::string> keyLocks{"123", "134", "125"};
    for (int8_t i = 0; i < 4; ++i)
    {
        auto callParameters = std::make_unique<CallParameters>();
        callParameters->staticCall = false;
        callParameters->codeAddress = "aabbccddee";
        callParameters->contextID = i;
        callParameters->seq = i;
        callParameters->type = i;
        auto executiveState = std::make_shared<ExecutiveState>(executiveFactory, inputRevert);
        executiveState->go();
        if (i == 0)
        {
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RUN);
        }
        else if (i == 1)
        {
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::PAUSED);
        }
        else if (i == 2)
        {
            executiveState->setResumeParam(callParameters);
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RESUME);
        }
        else
        {
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::FINISHED);
        }
        executiveState->appendKeyLocks(keyLocks);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos