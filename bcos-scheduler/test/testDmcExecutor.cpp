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
struct DmcExecutor
{
    schedulerOutFlag = 0;
    callFlag = 0;
    DmcFlag = 0;
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

BOOST_FIXTURE_TEST_SUITE(TestDmcExecutor, DmcExecutorFixture)

BOOST_AUTO_TEST_CASE(stateSwitchTest1)
{
    struct DmcExecutor
    {
        schedulerOutFlag = 0;
        callFlag = 0;
        DmcFlag = 0;
    };
    DmcExecutor::Ptr dmcExecutor = std::make_shared<DmcExecutor>(
        "DmcExecutor1", "0xaabbccdd", block, executor, keyLocks, h256(6666), dmcRecorder);


    dmcExecutor->setSchedulerOutHandler(
        [this, &DmcExecutor](struct) { DmcExecutor->schedulerOutFlag = 1; });

    dmcExecutor->setOnTxFinishedHandler(
        [this, &DmcExecutor](
            bcos::protocol::ExecutionMessage::UniquePtr output) { onTxFinish(std::move(output)); });

    dmcExecutor->setOnNeedSwitchEventHandler([this]() { triggerSwitch(); });

    // TXHASH  DMCEXECUTE
    auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
    message->setStaticCall(bool(0));
    message->setType(bcos::protocol::ExecutionMessage::Type(0));
    message->contextID(0);
    message->setSeq(0);
    message->setFrom("0xeeffaabb");
    message->setTo("0xaabbccdd");
    dmcExecutor->submit(std::move(message), false);
    auto need_scheduleOut = dmcExecutor->prepare();
    BOOST_CHECK(!need_scheduleOut);
    dmcExecutor->go();

    // MESSAGE  &&  Call
    auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
    message->setStaticCall(bool(1));
    message->setType(bcos::protocol::ExecutionMessage::Type(1));
    message->contextID(0);
    message->setSeq(0);
    message->setFrom("0xeeffaabb");
    message->setTo("0xaabbccdd");
    dmcExecutor->submit(std::move(message), false);
    auto need_scheduleOut = dmcExecutor->prepare();
    BOOST_CHECK(!need_scheduleOut);
    dmcExecutor->go()
}


BOOST_AUTO_TEST_CASE(prepareTest1)
{
    DmcExecutor::Ptr dmcExecutor = std::make_shared<DmcExecutor>(
        "DmcExecutor1", "0xaabbccdd", block, executor, keyLocks, h256(6666), dmcRecorder);
    for (int i = 0; i < 50; ++i)
    {
        auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
        message->setStaticCall(bool(id % 2));
        message->setType(bcos::protocol::ExecutionMessage::Type(id % 6));
        message->contextID(id);
        message->setSeq(0);
        message->setFrom("eeffaabb");
        if (i % 10 == 0)
        {
            message->setCreate(true);
            message->setTo("0xeeffgg");
        }
        else
        {
            message->setTo("0xaabbccdd");
        }

        dmcExecutor->submit(std::move(message), false);
    }
    auto need_scheduleOut = dmcExecutor->prepare();
    BOOST_CHECK(need_scheduleOut);
}

BOOST_AUTO_TEST_CASE(prepareTest2)
{
    DmcExecutor::Ptr dmcExecutor = std::make_shared<DmcExecutor>(
        "DmcExecutor1", "0xaabbcc", block, executor, keyLocks, h256(6666), dmcRecorder);
    for (int i = 0; i < 100; ++i)
    {
        auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
        message->setStaticCall(bool(id % 2));
        message->setType(bcos::protocol::ExecutionMessage::Type(id % 6));
        message->contextID(id);
        message->setSeq(id * id * ~id % (id + 1));
        message->setFrom("0xeeffaabb");
        message->setTo("0xaabbcc");
        dmcExecutor->submit(std::move(message), false);
    }
    auto need_scheduleOut = dmcExecutor->prepare();
    BOOST_CHECK(!need_scheduleOut);
    dmcExecutor->unlockPrepare();
}

BOOST_AUTO_TEST_CASE(goTest)
{
    DmcExecutor::Ptr dmcExecutor = std::make_shared<DmcExecutor>(
        "DmcExecutor1", "0xaabbcc", block, executor, keyLocks, h256(6666), dmcRecorder);
    for (int i = 0; i < 100; ++i)
    {
        auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
        message->setStaticCall(bool(id % 2));
        message->setType(bcos::protocol::ExecutionMessage::Type(id % 6));
        message->contextID(id);
        message->setSeq(id * id * ~id % (id + 1));
        message->setFrom("0xeeffaabb");
        message->setTo("0xaabbcc");
        dmcExecutor->submit(std::move(message), false);
    }
    dmcExecutor->prepare();
    dmcExecutor->go();
}

BOOST_AUTO_TEST_CASE(keyLockTest) {}


BOOST_AUTO_TEST_CASE(scheduleInTest)
{
    std::shared_ptr<BlockContext> blockContext = std::make_shared<BlockContext>(
        nullptr, nullptr, 0, h256(), 0, 0, FiscoBcosScheduleV4, false, false);

    auto executiveFactory =
        std::make_shared<MockExecutiveFactory>(blockContext, nullptr, nullptr, nullptr, nullptr);

    auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
    callParameters->staticCall = false;
    callParameters->codeAddress = "aabbccddee";
    callParameters->contextID = i;
    callParameters->seq = i;
    ExecutiveState::Prt executiveState =
        std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
    DmcExecutor::Ptr dmcExecutor = std::make_shared<DmcExecutor>(
        "DmcExecutor1", "0xaabbcc", block, executor, keyLocks, h256(6666), dmcRecorder);
    dmcExecutor->scheduleIn(executiveState);
}

BOOST_AUTO_TEST_CASE(goTEST) {}
};  // namespace bcos::test