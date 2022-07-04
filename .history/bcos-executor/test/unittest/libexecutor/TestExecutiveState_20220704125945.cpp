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
        std::shared_ptr<BlockContext> blockContext = std::make_shared<BlockContext>(
            nullptr, nullptr, 0, h256(), 0, 0, FiscoBcosScheduleV4, false, false);
        executiveFactory = std::make_shared<MockExecutiveFactory>(
            blockContext, nullptr, nullptr, nullptr, nullptr);
    }
    std::shared_ptr<MockTransactionExecutive> executive;
    std::shared_ptr<MockExecutiveFactory> executiveFactory;
    std::shared_ptr<ExecutiveState> executiveState;
    CallParameters::UniquePtr input;
};

BOOST_FIXTURE_TEST_SUITE(TestExecutiveState, ExecutiveStateFixture)

BOOST_AUTO_TEST_CASE(goTest)
{
    for (int8_t i = 0; i < 4; ++i)
    {
        if (i == 0)
        {
            auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RUN);
        }
        else if (i == 1)
        {
            auto callParameters = std::make_unique<CallParameters>(CallParameters::KEY_LOCK);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::PAUSED);
        }
        else if (i == 2)
        {
            auto callParameters = std::make_unique<CallParameters>(CallParameters::FINISHED);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            executiveState->setResumeParam(std::move(callParameters));
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RESUME);
        }
        else
        {
            auto callParameters = std::make_unique<CallParameters>(CallParameters::REVERT);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::FINISHED);
        }
        executiveState->go();
    }
}

BOOST_AUTO_TEST_CASE(setResumeParamTest)
{
    ExecutiveState::Ptr executiveState =
        std::make_shared<ExecutiveState>(executiveFactory, std::move(input));
    BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RUN);
    executiveState->setResumeParam(std::move(input));
    BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RESUME);
}

BOOST_AUTO_TEST_CASE(appendKeyLocks)
{
    std::vector<std::string> keyLocks{"123", "134", "125"};
    for (int8_t i = 0; i < 4; ++i)
    {
        if (i == 0)
        {
            auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RUN);
        }
        else if (i == 1)
        {
            auto callParameters = std::make_unique<CallParameters>(CallParameters::KEY_LOCK);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::PAUSED);
        }
        else if (i == 2)
        {
            auto callParameters = std::make_unique<CallParameters>(CallParameters::FINISHED);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            executiveState->setResumeParam(std::move(callParameters));
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RESUME);
        }
        else
        {
            auto callParameters = std::make_unique<CallParameters>(CallParameters::REVERT);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::FINISHED);
        }
        executiveState->appendKeyLocks(keyLocks);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
