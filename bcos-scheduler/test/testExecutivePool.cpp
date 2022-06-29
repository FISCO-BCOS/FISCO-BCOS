#include "Executive.h"
#include "ExecutivePool.h"
#include <bcos-framework/interfaces/executor/NativeExecutionMessage.h>
#include <bcos-utilities/Common.h>
#include <stdlib.h>
#include <tbb/concurrent_set.h>
#include <tbb/concurrent_unordered_map.h>
#include <boost/format.hpp>
#include <boost/test/unit_test.hpp>
#include <string>


using namespace bcos::scheduler;

namespace bcos::test
{
struct ExecutivePoolFixture
{
    ExecutivePoolFixture(){};

    scheduler::ExecutivePool m_executivePool;
    scheduler::ExecutivePool::MessageHint m_messageHint;
};
BOOST_FIXTURE_TEST_SUITE(TestExecutivePool, ExecutivePoolFixture)

BOOST_AUTO_TEST_CASE(addAndgetTest1)
{
    ExecutivePool::Ptr executivePool = std::make_shared<ExecutivePool>();
    BOOST_CHECK(executivePool->empty(ExecutivePool::MessageHint::NEED_PREPARE));
    BOOST_CHECK(executivePool->empty());
    for (int64_t i = 0; i < 50; ++i)
    {
        auto executiveState = std::make_shared<bcos::scheduler::ExecutiveState>(i, nullptr, false);
        executivePool->add(i, executiveState);
    }
    int64_t count = 0;
    while (executivePool->get(count) != nullptr)
    {
        ++count;
    }
    BOOST_CHECK(count == 50);
    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::NEED_PREPARE));
}

BOOST_AUTO_TEST_CASE(addAndgetTest2)
{
    ExecutivePool::Ptr executivePool = std::make_shared<ExecutivePool>();
    BOOST_CHECK(executivePool->empty(ExecutivePool::MessageHint::NEED_PREPARE));
    BOOST_CHECK(executivePool->empty());
    for (int64_t i = 0; i < 10; ++i)
    {
        auto message = std::make_unique<bcos::executor::NativeExecutionMessage>();
        message->setStaticCall(bool(i % 2));
        message->setType(protocol::ExecutionMessage::Type(i % 6));
        message->setContextID(i);
        message->setSeq(i * i * ~i % (i + 1));
        message->setOrigin("aabbccdd");
        message->setFrom("eeffaabb");
        message->setTo("ccddeeff");
        ExecutiveState::Ptr executiveState =
            std::make_shared<ExecutiveState>(i, std::move(message), false);
        executivePool->add(i, executiveState);
    }


    auto message = std::make_unique<bcos::executor::NativeExecutionMessage>();
    message->setStaticCall(true);
    message->setType(protocol::ExecutionMessage::Type(6));
    message->setContextID(9);
    message->setSeq(1000);
    message->setOrigin("aabbccdd");
    message->setFrom("cccccccc");
    message->setTo("dddd");
    auto executiveState =
        std::make_shared<bcos::scheduler::ExecutiveState>(9, std::move(message), true);
    bool success = executivePool->add(9, executiveState);
    BOOST_CHECK(!success);
    auto state = executivePool->get(9);
    SCHEDULER_LOG(DEBUG) << state->message->to();
    BOOST_CHECK(std::string(state->message->to()) == "ccddeeff");
}


BOOST_AUTO_TEST_CASE(refreshTest)
{
    ExecutivePool::Ptr executivePool = std::make_shared<ExecutivePool>();
    BOOST_CHECK(executivePool->empty());
    for (int64_t i = 1; i <= 30; ++i)
    {
        executivePool->markAs(i, ExecutivePool::MessageHint::ALL);
        // executivePool->markAs(ExecutivePool::MessageHint::NEED_PREPARE, i);
        if (i % 3 == 0)
        {
            executivePool->markAs(i, ExecutivePool::MessageHint::NEED_SCHEDULE_OUT);
        }
        if (i % 3 == 1)
        {
            executivePool->markAs(i, ExecutivePool::MessageHint::LOCKED);
            executivePool->markAs(i, ExecutivePool::MessageHint::NEED_SEND);
        }
        if (i % 3 == 2)
        {
            executivePool->markAs(i, ExecutivePool::MessageHint::END);
        }
    }
    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::NEED_SCHEDULE_OUT));
    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::END));
    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::LOCKED));
    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::NEED_SEND));
    executivePool->refresh();
    BOOST_CHECK(executivePool->empty(ExecutivePool::MessageHint::NEED_SCHEDULE_OUT));
    BOOST_CHECK(executivePool->empty(ExecutivePool::MessageHint::END));
    BOOST_CHECK(executivePool->empty(ExecutivePool::MessageHint::LOCKED) &&
                !executivePool->empty(ExecutivePool::MessageHint::NEED_SEND));
}

BOOST_AUTO_TEST_CASE(forEachTest)
{
    ExecutivePool::Ptr executivePool = std::make_shared<ExecutivePool>();
    std::set<int64_t> needPrepare;
    std::set<int64_t> needSchedule;
    std::set<int64_t> needRemove;
    int64_t id;
    for (int64_t i = 1; i <= 10; ++i)
    {
        // generate between  random number
        id = (rand() % 10000) + 1;
        BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_KV("needPrepare", needPrepare.size())
                        << LOG_KV("ID", id);
        needPrepare.insert(id);
        needSchedule.insert(++id);
        needRemove.insert(id + 2);
    }
    BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_KV("needPrepare", needPrepare.size())
                    << LOG_KV("needSchedule", needSchedule.size())
                    << LOG_KV("needRemove", needRemove.size());

    executivePool->forEach(ExecutivePool::MessageHint::ALL, [](int64_t, ExecutiveState::Ptr) {
        // do nothing
        BOOST_CHECK(false);
        return true;
    });
    BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_DESC("before add");
    for (auto i : needPrepare)
    {
        BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_KV("needPrepare", needPrepare.size())
                        << LOG_KV("ID", i);
        auto executiveState = std::make_shared<bcos::scheduler::ExecutiveState>(i, nullptr, false);
        executivePool->add(i, executiveState);
        executivePool->markAs(i, ExecutivePool::MessageHint::NEED_PREPARE);
        executivePool->markAs(i, ExecutivePool::MessageHint::ALL);
    }
    for (auto i : needSchedule)
    {
        BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_KV("needPrepare", needSchedule.size())
                        << LOG_KV("ID", i);
        auto executiveState = std::make_shared<bcos::scheduler::ExecutiveState>(i, nullptr, false);
        executivePool->add(i, executiveState);
        executivePool->markAs(i, ExecutivePool::MessageHint::NEED_SCHEDULE_OUT);
    }
    for (auto i : needRemove)
    {
        BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_KV("needPrepare", needRemove.size())
                        << LOG_KV("ID", i);
        auto executiveState = std::make_shared<bcos::scheduler::ExecutiveState>(i, nullptr, false);
        executivePool->add(i, executiveState);
        executivePool->markAs(i, ExecutivePool::MessageHint::END);
    }
    BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_DESC("behind add");
    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::ALL));
    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::NEED_PREPARE));
    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::END));
    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::NEED_SCHEDULE_OUT));
    BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_DESC("markasprepare before");
    executivePool->forEach(ExecutivePool::MessageHint::NEED_PREPARE,
        [this, &needPrepare](int64_t contextID, ExecutiveState::Ptr) {
            BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_KV("contextID", contextID);
            auto iter = needPrepare.find(contextID);
            needPrepare.erase(iter);
            return true;
        });
    BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_DESC("markasNEED_PREPARE behind");
    executivePool->forEach(ExecutivePool::MessageHint::NEED_SCHEDULE_OUT,
        [this, &needSchedule](int64_t contextID, ExecutiveState::Ptr) {
            auto iter = needSchedule.find(contextID);
            needSchedule.erase(iter);
            return true;
        });
    BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_DESC("markaNEED_SCHEDULE_OUT behind");
    executivePool->forEach(ExecutivePool::MessageHint::END,
        [this, &needRemove](int64_t contextID, ExecutiveState::Ptr) {
            auto iter = needRemove.find(contextID);
            needRemove.erase(iter);
            return true;
        });
    BCOS_LOG(DEBUG) << LOG_BADGE("scheduel_test") << LOG_DESC("markasEND behind");
    executivePool->forEach(
        ExecutivePool::MessageHint::ALL, [](int64_t, ExecutiveState::Ptr executiveState) {
            // do nothing
            BCOS_LOG(DEBUG) << " 1.PendingMsg: \t\t [--] " << executiveState->toString();
            return true;
        });
    BOOST_CHECK(needPrepare.empty());
    BOOST_CHECK(needSchedule.empty());
    BOOST_CHECK(needRemove.empty());
}

BOOST_AUTO_TEST_CASE(forEachAndClearTest)
{
    ExecutivePool::Ptr executivePool = std::make_shared<ExecutivePool>();
    std::set<int64_t> needSend;
    std::set<int64_t> locked;
    for (int64_t i = 1; i <= 10; ++i)
    {
        auto id = (rand() % 10000) + 1;
        needSend.insert(id);
        locked.insert(id + 1);
    }
    for (auto i : needSend)
    {
        auto executiveState = std::make_shared<bcos::scheduler::ExecutiveState>(i, nullptr, false);
        executivePool->add(i, executiveState);
        executivePool->markAs(i, ExecutivePool::MessageHint::NEED_SEND);
    }
    for (auto i : locked)
    {
        auto executiveState = std::make_shared<bcos::scheduler::ExecutiveState>(i, nullptr, false);
        executivePool->add(i, executiveState);
        executivePool->markAs(i, ExecutivePool::MessageHint::LOCKED);
    }

    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::NEED_SEND));
    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::LOCKED));

    executivePool->forEachAndClear(ExecutivePool::MessageHint::NEED_SEND,
        [this, &needSend](int64_t contextID, ExecutiveState::Ptr) {
            auto iter = needSend.find(contextID);
            needSend.erase(iter);
            BCOS_LOG(DEBUG) << LOG_BADGE("SCHEDULE") << LOG_DESC("set length is")
                            << LOG_KV("needSend", needSend.size());

            return true;
        });
    executivePool->forEachAndClear(ExecutivePool::MessageHint::LOCKED,
        [this, &locked](int64_t contextID, ExecutiveState::Ptr) {
            auto iter = locked.find(contextID);
            locked.erase(iter);
            return true;
        });
    BOOST_CHECK(needSend.empty());
    BOOST_CHECK(locked.empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
