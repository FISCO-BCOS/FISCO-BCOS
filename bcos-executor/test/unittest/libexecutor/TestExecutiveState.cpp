#include "../../../src/CallParameters.h"
#include "../../../src/executive/BlockContext.h"
#include "../../../src/executive/ExecutiveFactory.h"
#include "../../../src/executive/ExecutiveState.h"
#include "../../../src/executive/TransactionExecutive.h"
#include "../mock/MockExecutiveFactory.h"
#include "../mock/MockTransactionExecutive.h"
#include <boost/test/unit_test.hpp>

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
    CallParameters::UniquePtr output;
    for (int8_t i = 0; i < 4; ++i)
    {
        EXECUTOR_LOG(DEBUG) << "goTest begin";
        if (i == 0)
        {
            EXECUTOR_LOG(DEBUG) << "i == 0 begin";
            auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            // EXECUTOR_LOG(DEBUG) << executiveState->getStatus();
            auto output = executiveState->go();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::PAUSED);
            executiveState->setResumeParam(std::move(input));
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RESUME);
            executiveState->go();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::PAUSED);
            EXECUTOR_LOG(DEBUG) << "i == 0  end!";
        }

        else if (i == 1)
        {
            EXECUTOR_LOG(DEBUG) << "i == 1 begin";
            auto callParameters = std::make_unique<CallParameters>(CallParameters::KEY_LOCK);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            executiveState->go();
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::FINISHED);
            // executiveState->go();
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            EXECUTOR_LOG(DEBUG) << "i == 1  end!";
        }

        else if (i == 2)
        {
            EXECUTOR_LOG(DEBUG) << "i == 2 begin";
            auto callParameters = std::make_unique<CallParameters>(CallParameters::FINISHED);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            executiveState->go();
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::FINISHED);
            // executiveState->go();
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            EXECUTOR_LOG(DEBUG) << "i == 2  end!";
        }
        else
        {
            EXECUTOR_LOG(DEBUG) << "i == 3 begin";
            auto callParameters = std::make_unique<CallParameters>(CallParameters::REVERT);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            executiveState->go();
        }
    }
}

BOOST_AUTO_TEST_CASE(appendKeyLocksTest)
{
    EXECUTOR_LOG(DEBUG) << "appendKeyLocks begin";
    std::vector<std::string> keyLocks{"123", "134", "125"};
    for (int i = 0; i < 3; ++i)
    {
        if (i == 0)
        {
            auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            callParameters->keyLocks = {"987"};
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::NEED_RUN);
            executiveState->appendKeyLocks(keyLocks);
            EXECUTOR_LOG(DEBUG) << "i == 1 end ! status is :" << executiveState->getStatus();
        }
        else if (i == 1)
        {
            EXECUTOR_LOG(DEBUG) << "i == 2 begin";
            auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            auto input1 = std::make_unique<CallParameters>(CallParameters::MESSAGE);
            input1->staticCall = false;
            input1->codeAddress = "aabbccddee";
            input1->contextID = i;
            input1->seq = i;
            input1->keyLocks = {"987"};
            executiveState->setResumeParam(std::move(input1));
            executiveState->appendKeyLocks(keyLocks);
            EXECUTOR_LOG(DEBUG) << "i == 2 end ! status is :" << executiveState->getStatus();
        }
        else
        {
            EXECUTOR_LOG(DEBUG) << "i == 3 begin";
            auto callParameters = std::make_unique<CallParameters>(CallParameters::REVERT);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::Status::FINISHED);
            executiveState->appendKeyLocks(keyLocks);
            EXECUTOR_LOG(DEBUG) << "i == 3 end ! status is :" << executiveState->getStatus();
        }
    }
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
