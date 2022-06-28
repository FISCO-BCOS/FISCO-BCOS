#include "Executive.h"
#include "ExecutivePool.h"
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
BOOST_FIXTURE_TEST_SUITE(TestExecutivePool, ExecutivePoolFixture);

{
    ExecutivePool::Ptr executivePool = std::make_shared<ExecutivePool>();
    BOOST_CHECK(executivePool->empty(ExecutivePool::MessageHint::NEED_PREPARE));
    BOOST_CHECK(executivePool->empty());
    for (int64_t i = 0; i < 50; ++i)
    {
        auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
        message->setStaticCall(bool(i % 2));
        message->setType(protocol::ExecutionMessage::Type(i % 6));
        message->setContextID(i);
        message->setSeq(i * i * ~i % (i + 1));
        message->setOrigin("aabbccdd");
        message->setFrom("eeffaabb");
        message->setTo("ccddeeff");

        auto executiveState = std::make_unique<bcos::scheduler::ExecutiveState>();
        executiveState->message = message;
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
        auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
        message->setStaticCall(bool(i % 2));
        message->setType(protocol::ExecutionMessage::Type(i % 6));
        message->setContextID(i);
        message->setSeq(i * i * ~i % (i + 1));
        message->setOrigin("aabbccdd");
        message->setFrom("eeffaabb");
        message->setTo("ccddeeff");

        auto executiveState = std::make_unique<bcos::scheduler::ExecutiveState>();
        executiveState->message = message;
        executiveState->contextID = i;
        executiveState->enableDAG = false;
        executiveState->id = i;
        executivePool->add(i, executiveState);
    }
    auto executiveState9 = executivePool->get(9);
    bool dag = executiveState9->enableDAG;
    BOOST_CHECK(!dag);

    auto message = std::make_unique<bcos::protocol::ExecutionMessage>();
    message->setStaticCall(true);
    message->setType(protocol::ExecutionMessage::Type(1));
    message->setContextID(9);
    message->setSeq(1000);
    message->setOrigin("aabbccdd");
    message->setFrom("eeffaabb");
    message->setTo("ccddeeff");
    auto executiveState = std::make_unique<bcos::scheduler::ExecutiveState>();
    executiveState->message = message;
    executiveState->contextID = 9;
    executiveState->enableDAG = true;
    executiveState->id = 9;
    executivePool->add(9, executiveState);
    auto state = executivePool->get(9);
    BOOST_CHECK_EQUAL(state->id, 9);
    BOOST_CHECK(state->enableDAG);
}


BOOST_AUTO_TEST_CASE(refreshTest)
{
    ExecutivePool::Ptr executivePool = std::make_shared<ExecutivePool>();
    BOOST_CHECK(executivePool->empty());
    for (int64_t i = 1; i <= 30; ++i)
    {
        executivePool->markAs(ExecutivePool::MessageHint::ALL, i);
        // executivePool->markAs(ExecutivePool::MessageHint::NEED_PREPARE, i);
        if (i % 3 == 0)
        {
            executivePool->markAs(m_messageHint::NEED_SCHEDULE_OUT, i);
        }
        if (i % 3 == 1)
        {
            executivePool->markAs(m_messageHint::LOCKED, i);
            executivePool->markAs(m_messageHint::NEED_SEND, i);
        }
        if (i % 3 == 2)
        {
            executivePool->markAs(m_messageHint::END, i);
        }
    }
    BOOST_CHECK(!executivePool->empty(m_messageHint::NEED_SECHDULE_OUT));
    BOOST_CHECK(!executivePool->empty(m_messageHint::END));
    BOOST_CHECK(!executivePool->empty(m_messageHint::LOCKED));
    BOOST_CHECK(!executivePool->empty(m_messageHint::NEED_SEND));
    executivePool->refresh();
    BOOST_CHECK(executivePool->empty(m_messageHint::NEED_SCHEDULE_OUT));
    BOOST_CHECK(executivePool->empty(m_messageHint::END));
    BOOST_CHECK(executivePool->empty(m_messageHint::LOCKED) &&
                !executivePool->empty(m_messageHint::NEED_SEND))
}

BOOST_AUTO_TEST_CASE(forEachTest)
{
    for (int64_t i = 1; i <= 10; ++i)
    {
        // generate between [1,100] random number
        m_executivePool.markAs(m_messageHint::NEED_PREPARE, (rand() % 100) + 1);
    }
    BOOST_CHECK(m_needPrepare->empty());
    int64_t sum = 0;
    m_executivePool.forEach(MessageHint::NEED_PREPARE,
        [this, messages](int64_t contextID, ExecutiveState::Ptr executiveState) {
            m_needPrepare->unsafe_erase(contextID);
            return true;
        });
    BOOST_CHECK(m_executivePool.empty(m_messageHint::NEED_PREPARE));
}

BOOST_AUTO_TEST_CASE(forEachAndClearTest)
{
    for (int64_t i = 1; i <= 10; ++i)
    {
        auto contextId = (rand() % 100) + 1;
        m_executivePool.markAs(m_messageHint::NEED_SEND, contextId);
        m_executivePool.markAs(m_messageHint::LOCKED, contextId);
    }


    BOOST_CHECK(!m_needSend->empty());
    BOOST_CHECK(!m_hasLocked->empty());
    m_executivePool.forEachAndClear(MessageHint::NEED_SEND,
        [this, messages](int64_t contextID, ExecutiveState::Ptr executiveState) {
            m_hasLocked->unsafe_erase(contextID);
            return true;
        });

    BOOST_CHECK(m_needSend->empty() && m_hasLocked->empty());
}
}  // namespace bcos::test
