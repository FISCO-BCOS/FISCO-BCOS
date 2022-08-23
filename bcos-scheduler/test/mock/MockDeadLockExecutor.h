#pragma once
#include "MockExecutor.h"
#include "bcos-framework/executor/ExecutionMessage.h"

namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockDeadLockParallelExecutor : public MockParallelExecutor
{
public:
    using MockParallelExecutor::MockParallelExecutor;

    ~MockDeadLockParallelExecutor() override {}

    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        std::set<std::string, std::less<>> contracts = {"contract1", "contract2"};
        BOOST_CHECK(contracts.count(input->to()) == 1);
        BOOST_CHECK(input->contextID() == 0 || input->contextID() == 1);

        if (input->type() == protocol::ExecutionMessage::REVERT)
        {
            if (input->contextID() == 1)
            {
                if (input->seq() == 1)
                {
                    BOOST_CHECK_EQUAL(input->to(), "contract1");
                    BOOST_CHECK_EQUAL(input->from(), "contract1");
                }
                else if (input->seq() == 0)
                {
                    BOOST_CHECK_EQUAL(input->to(), "contract1");
                    BOOST_CHECK_EQUAL(input->from(), "contract1");
                }
                else
                {
                    BOOST_FAIL("Unexecuted seq");
                }
            }
            else
            {
                if (input->seq() == 1)
                {
                    BOOST_CHECK_EQUAL(input->to(), "contract2");
                    BOOST_CHECK_EQUAL(input->from(), "contract2");
                }
                else if (input->seq() == 0)
                {
                    BOOST_CHECK_EQUAL(input->to(), "contract2");
                    BOOST_CHECK_EQUAL(input->from(), "contract2");
                }
                else
                {
                    BOOST_FAIL("Unexecuted seq");
                }
            }
        }
        else if (input->type() == protocol::ExecutionMessage::TXHASH)
        {
            BOOST_CHECK_EQUAL(input->seq(), 0);
            input->setType(protocol::ExecutionMessage::MESSAGE);
            if (input->to() == "contract1")
            {
                input->setFrom("contract1");
                input->setTo("contract2");
                input->setKeyLocks({"key1"});
            }
            else
            {
                input->setFrom("contract2");
                input->setTo("contract1");
                input->setKeyLocks({"key2"});
            }
        }
        else if (input->type() == protocol::ExecutionMessage::MESSAGE)
        {
            BOOST_CHECK_GT(input->seq(), 0);
            input->setType(protocol::ExecutionMessage::KEY_LOCK);

            if (input->contextID() == 1)
            {
                BOOST_CHECK_EQUAL(input->keyLocks()[0], "key1");
                input->setFrom(std::string(input->to()));
                input->setKeyLocks({});
                input->setKeyLockAcquired("key1");
            }
            else
            {
                BOOST_CHECK_EQUAL(input->keyLocks()[0], "key2");
                input->setFrom(std::string(input->to()));
                input->setKeyLocks({});
                input->setKeyLockAcquired("key2");
            }
        }
        else if (input->type() == protocol::ExecutionMessage::KEY_LOCK)
        {
            std::set<std::string, std::less<>> contracts = {"contract1", "contract2"};
            BOOST_CHECK_EQUAL(contracts.count(input->to()), 1);
            input->setType(protocol::ExecutionMessage::FINISHED);
        }
        else if (input->type() == protocol::ExecutionMessage::FINISHED)
        {
            std::set<std::string, std::less<>> contracts = {"contract1", "contract2"};
            BOOST_CHECK_EQUAL(contracts.count(input->to()), 1);
        }

        callback(nullptr, std::move(input));
    }

    std::string m_name;
    bcos::protocol::BlockNumber m_blockNumber = 0;
};
#pragma GCC diagnostic pop
}  // namespace bcos::test