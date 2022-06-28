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
        auto executiveState = std::make_shared<bcos::scheduler::ExecutiveState>();
        executiveState->contextID = i;
        executiveState->enableDAG = false;
        executiveState->id = i;
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

        ExecutiveState::Ptr executiveState = std::make_shared<ExecutiveState>();
        executiveState->message = std::move(message);
        executiveState->contextID = i;
        executiveState->enableDAG = false;
        executiveState->id = i;
        executivePool->add(i, executiveState);
    }


    auto message = std::make_unique<bcos::executor::NativeExecutionMessage>();
    message->setStaticCall(bool(i % 2));
    message->setType(protocol::ExecutionMessage::Type(i % 6));
    message->setContextID(i);
    message->setSeq(i * i * ~i % (i + 1));
    message->setOrigin("aabbccdd");
    message->setFrom("eeffaabb");
    message->setTo("ccddeeff");
    auto executiveState = std::make_shared<bcos::scheduler::ExecutiveState>();
    executiveState->message = std::move(message);
    executiveState->contextID = 9;
    executiveState->enableDAG = true;
    executiveState->id = 9;
    executivePool->add(9, executiveState);
    auto state = executivePool->get(9);
    BOOST_CHECK_EQUAL(state->message->seq(), 1000);
    BOOST_CHECK(state->enableDAG);
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
    for (int64_t i = 1; i <= 10; ++i)
    {
        // generate between [1,100] random number
        needPrepare.insert((rand() % 100) + 1);
    }
    for (auto i : needPrepare)
    {
        executivePool->markAs(i, ExecutivePool::MessageHint::NEED_PREPARE);
    }
    BOOST_CHECK(!executivePool->empty(ExecutivePool::MessageHint::NEED_PREPARE));

    executivePool->forEach(ExecutivePool::MessageHint::NEED_PREPARE,
        [this, &needPrepare](int64_t contextID, ExecutiveState::Ptr) {
            needPrepare.erase(contextID);
            return true;
        });
    BOOST_CHECK(needPrepare.empty());
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test

// BOOST_AUTO_TEST_CASE(forEachAndClearTest)
// {
//     ExecutivePool::Ptr executivePool = std::make_shared<ExecutivePool>();
//     for (int64_t i = 1; i <= 10; ++i)
//     {
//         auto contextId = (rand() % 100) + 1;
//         executivePool->markAs(contextId, ExecutivePool::MessageHint::NEED_SEND);
//         executivePool->markAs(contextId, ExecutivePool::MessageHint::LOCKED);
//     }


//     BOOST_CHECK(!m_needSend->empty());
//     BOOST_CHECK(!m_hasLocked->empty());
//     auto messages = std::make_shared<std::vector<protocol::ExecutionMessage::UniquePtr>>();
//     executivePool->forEachAndClear(ExecutivePool::MessageHint::NEED_SEND,
//         [this, messages](int64_t contextID, ExecutiveState::Ptr executiveState) {
//             executivePool->m_hasLocked->unsafe_erase(contextID);
//             return true;
//         });

//     BOOST_CHECK(executivePool->m_needSend->empty() && executivePool->m_hasLocked->empty());
// }
