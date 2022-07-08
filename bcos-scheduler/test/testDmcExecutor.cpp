#pragma once
#include "../src/DmcExecutor.h"
#include "../src/DmcStepRecorder.h"
#include "../src/Executive.h"
#include "../src/ExecutivePool.h"
#include "../src/ExecutorManager.h"
#include "../src/GraphKeyLocks.h"
#include "MockDmcExecutorForDMC.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executive/ExecutiveState.h"
#include "bcos-executor/test/unittest/mock/MockExecutiveFactory.h"
#include <bcos-framework/interfaces/protocol/Block.h>
#include <bcos-framework/interfaces/protocol/TransactionFactory.h>
#include <bcos-framework/interfaces/protocol/TransactionReceiptFactory.h>
#include <tbb/concurrent_set.h>
#include <tbb/concurrent_unordered_map.h>
#include <string>


using namespace bcos::scheduler;

namespace bcos::test
{
struct DmcFlagStruct
{
    using Ptr = std::shared_ptr<DmcFlagStruct>;
    bool schedulerOutFlag = 0;
    bool callFlag = 0;
    bool DmcFlag = 0;
    bool switchFlag = 0;
    bool finishFlag = 0;
    bool lockedFlag = 0;
};

struct DmcExecutorFixture
{
    DmcExecutorFixture()
    {
        auto receiptFactory = std::make_shared<TransactionReceiptFactory>();
        auto transactionFactory = std::make_shared<TransactionFactory>();
        auto block = std::make_shared<Block>(transactionFactory, receiptFactory);
        executor = std::make_shared<MockDmcExecutorForDMC>("executor1");
        keyLocks = std::make_shared<GraphKeyLocks>();
        dmcRecorder = std::make_shared<DmcStepRecorder>();
    };
    std::shared_ptr<DmcExecutor> dmcExecutor;
    std::shared_ptr<bcos::protocol::Block::Ptr> block;
    std::shared_ptr<MockDmcExecutorForDMC> executor;
    std::shared_ptr<GraphKeyLocks> keyLocks;
    std::shared_ptr<DmcStepRecorder> dmcRecorder;
};

void createMessage(int contextID, int seq, int type, std::string toAddress, bool staticCall)
{
    auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
    message->setStaticCall(staticCall);
    message->setType(bcos::protocol::ExecutionMessage::Type(type));
    message->contextID(contextID);
    message->setSeq(seq);
    message->setFrom("0xeeffaabb");
    message->setTo(toAddress);
    return std::move(message);
}

BOOST_FIXTURE_TEST_SUITE(TestDmcExecutor, DmcExecutorFixture)

BOOST_AUTO_TEST_CASE(stateSwitchTest1)
{
    DmcFlagStruct dmcFlagStruct;

    DmcExecutor::Ptr dmcExecutor = std::make_shared<DmcExecutor>(
        "DmcExecutor1", "0xaabbccdd", block, executor, keyLocks, h256(6666), dmcRecorder);

    dmcExecutor->setSchedulerOutHandler([this, &dmcFlagStruct](ExecutiveState::Ptr executiveState) {
        dmcFlagStruct.schedulerOutFlag = true;
    });

    dmcExecutor->setOnTxFinishedHandler(
        [this, &dmcFlagStruct](bcos::protocol::ExecutionMessage::UniquePtr output) {
            auto outputBytes = output->data();
            std::string outputStr((char*)outputBytes.data(), outputBytes.size());
            if (outputStr == "Call Finished!")
            {
                dmcFlagStruct.callFlag = true;
                dmcFlagStruct.finishFlag = true;
            }
            else if (outputStr == "DMCExecuteTransaction Finish!")
            {
                dmcFlagStruct.dmcFlag = true;
                dmcFlagStruct.finishFlag = true;
            }
            else if (outputStr == "DMCExecuteTransaction Finish, I am keyLock!")
            {
                dmcFlagStruct.lockedFlag = true;
            }
            else
            {
                dmcFlagStruct.finishFlag = true;
            }
        });

    dmcExecutor->setOnNeedSwitchEventHandler(
        [this, &dmcFlagStruct]() { dmcFlagStruct.switchFlag = true; });


    // TXHASH = 0,  // Received an new transaction from scheduler
    // MESSAGE,     // Send/Receive an external call to/from another contract
    // FINISHED,    // Send a finish to another contract
    // KEY_LOCK,    // Send a wait key lock to scheduler, or release key lock
    // SEND_BACK,   // Send a dag refuse to scheduler
    // REVERT,      // Send/Receive a revert to/from previous external call

    // TXHASH  DMCEXECUTE
    auto message = createMessage(0, 0, 0, "0xaabbccdd", false);
    dmcExecutor->submit(std::move(message), false);
    auto need_scheduleOut = dmcExecutor->prepare();
    BOOST_CHECK(!need_scheduleOut);
    dmcExecutor->go();
    BOOST_CHECK(dmcFlagStruct.finishFlag && dmcFlagStruct.dmcFlag);

    // need scheduleOut
    auto message1 = createMessage(1, 0, 1, "0x0000000", false);
    dmcExecutor->submit(std::move(message1), false);
    auto need_scheduleOut = dmcExecutor->prepare();
    dmcExecutor->go();
    BOOST_CHECK(!need_scheduleOut && dmcFlagStruct.schedulerOutFlag);

    // MESSAGE  &&  Call
    auto message2 = createMessage(2, 1, 1, "0xaabbccdd", true);
    dmcExecutor->submit(std::move(message2), false);
    auto need_scheduleOut = dmcExecutor->prepare();
    BOOST_CHECK(!need_scheduleOut);
    dmcExecutor->go();
    BOOST_CHEEK(dmcFlagStruct.callFlag && dmcFlagStruct.finishFlag);

    // FINISHED
    auto message3 = createMessage(3, 1, 2, "0xaabbccdd", false);
    dmcExecutor->submit(std::move(message3), false);
    dmcExecutor->prepare();
    BOOST_CHEEK(dmcFlagStruct.finishFlag = true;);

    // REVERT
    auto message4 = createMessage(4, 0, 5, "0xaabbccdd", true);
    dmcExecutor->submit(std::move(message4), false);
    dmcExecutor->prepare();
    BOOST_CHEEK(dmcFlagStruct.finishFlag = true;);

    // SEND_BACK
    auto message5 = createMessage(5, 0, 4, "0xaabbccdd", false);
    dmcExecutor->submit(std::move(message5), false);
    dmcExecutor->prepare();
    dmcExecutor->go();
    BOOST_CHEEK(dmcFlagStruct.callFlag && dmcFlagStruct.finishFlag);


    // KEY_LOCK
    auto message6 = createMessage(6, 0, 3, "0xddccbbaa", false);
    dmcExecutor->submit(std::move(message6), false);
    auto need_scheduleOut = dmcExecutor->prepare();
    dmcExecutor->go();
    BOOST_CHEEK(dmcFlagStruct.lockedFlag);

    // Error Call
    auto message7 = createMessage(7, 0, 3, "0xddccbbaa", true);
    dmcExecutor->submit(std::move(message6), false);
    auto need_scheduleOut = dmcExecutor->prepare();
    dmcExecutor->go();
    auto message8 = createMessage(8, 0, 3, "0xddccbbaa", false);
}
// BOOST_AUTO_TEST_CASE(errorTest)
// BOOST_AUTO_TEST_CASE(deadLockTest)
BOOST_AUTO_TEST_CASE_END()

// BOOST_AUTO_TEST_CASE(prepareTest1)
// {
//     DmcExecutor::Ptr dmcExecutor = std::make_shared<DmcExecutor>(
//         "DmcExecutor1", "0xaabbccdd", block, executor, keyLocks, h256(6666), dmcRecorder);
//     for (int i = 0; i < 50; ++i)
//     {
//         auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
//         message->setStaticCall(bool(id % 2));
//         message->setType(bcos::protocol::ExecutionMessage::Type(id % 6));
//         message->contextID(id);
//         message->setSeq(0);
//         message->setFrom("eeffaabb");
//         if (i % 10 == 0)
//         {
//             message->setCreate(true);
//             message->setTo("0xeeffgg");
//         }
//         else
//         {
//             message->setTo("0xaabbccdd");
//         }

//         dmcExecutor->submit(std::move(message), false);
//     }
//     auto need_scheduleOut = dmcExecutor->prepare();
//     BOOST_CHECK(need_scheduleOut);
// }

// BOOST_AUTO_TEST_CASE(prepareTest2)
// {
//     DmcExecutor::Ptr dmcExecutor = std::make_shared<DmcExecutor>(
//         "DmcExecutor1", "0xaabbcc", block, executor, keyLocks, h256(6666), dmcRecorder);
//     for (int i = 0; i < 100; ++i)
//     {
//         auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
//         message->setStaticCall(bool(id % 2));
//         message->setType(bcos::protocol::ExecutionMessage::Type(id % 6));
//         message->contextID(id);
//         message->setSeq(id * id * ~id % (id + 1));
//         message->setFrom("0xeeffaabb");
//         message->setTo("0xaabbcc");
//         dmcExecutor->submit(std::move(message), false);
//     }
//     auto need_scheduleOut = dmcExecutor->prepare();
//     BOOST_CHECK(!need_scheduleOut);
//     dmcExecutor->unlockPrepare();
// }

// BOOST_AUTO_TEST_CASE(goTest)
// {
//     DmcExecutor::Ptr dmcExecutor = std::make_shared<DmcExecutor>(
//         "DmcExecutor1", "0xaabbcc", block, executor, keyLocks, h256(6666), dmcRecorder);
//     for (int i = 0; i < 100; ++i)
//     {
//         auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
//         message->setStaticCall(bool(id % 2));
//         message->setType(bcos::protocol::ExecutionMessage::Type(id % 6));
//         message->contextID(id);
//         message->setSeq(id * id * ~id % (id + 1));
//         message->setFrom("0xeeffaabb");
//         message->setTo("0xaabbcc");
//         dmcExecutor->submit(std::move(message), false);
//     }
//     dmcExecutor->prepare();
//     dmcExecutor->go();
// }

// BOOST_AUTO_TEST_CASE(keyLockTest) {}


// BOOST_AUTO_TEST_CASE(scheduleInTest)
// {
//     std::shared_ptr<BlockContext> blockContext = std::make_shared<BlockContext>(
//         nullptr, nullptr, 0, h256(), 0, 0, FiscoBcosScheduleV4, false, false);

//     auto executiveFactory =
//         std::make_shared<MockExecutiveFactory>(blockContext, nullptr, nullptr, nullptr, nullptr);

//     auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
//     callParameters->staticCall = false;
//     callParameters->codeAddress = "aabbccddee";
//     callParameters->contextID = i;
//     callParameters->seq = i;
//     ExecutiveState::Prt executiveState =
//         std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
//     DmcExecutor::Ptr dmcExecutor = std::make_shared<DmcExecutor>(
//         "DmcExecutor1", "0xaabbcc", block, executor, keyLocks, h256(6666), dmcRecorder);
//     dmcExecutor->scheduleIn(executiveState);
// }

};  // namespace bcos::test