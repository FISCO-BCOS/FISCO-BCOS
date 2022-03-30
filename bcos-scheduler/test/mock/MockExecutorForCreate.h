#pragma once

#include "Common.h"
#include "MockExecutor.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include <boost/lexical_cast.hpp>
#include <tuple>

namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockParallelExecutorForCreate : public MockParallelExecutor
{
public:
    MockParallelExecutorForCreate(const std::string& name) : MockParallelExecutor(name) {}

    ~MockParallelExecutorForCreate() override {}

    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        SCHEDULER_LOG(TRACE) << "Execute, type: " << input->type()
                             << " contextID: " << input->contextID() << " seq: " << input->seq();

        if (count == 0)
        {
            BOOST_CHECK_EQUAL(input->type(), protocol::ExecutionMessage::TXHASH);
        }
        else if (count == 1)
        {
            BOOST_CHECK_EQUAL(input->type(), protocol::ExecutionMessage::MESSAGE);
        }
        else
        {
            BOOST_CHECK_EQUAL(input->type(), protocol::ExecutionMessage::FINISHED);
        }
        BOOST_CHECK_GE(input->contextID(), 0);

        SCHEDULER_LOG(TRACE) << "contract address: " << input->to();
        BOOST_CHECK(input->to().find("contract") == std::string::npos);
        BOOST_CHECK_EQUAL(input->to().size(), 40);

        // Always success
        SCHEDULER_LOG(TRACE) << "Input, hash:" << input->transactionHash().hex()
                             << " contract:" << input.get() << " to:" << input->to();
        BOOST_CHECK(input);
        BOOST_CHECK(!input->to().empty());
        BOOST_CHECK_EQUAL(input->depth(), 0);
        BOOST_CHECK_EQUAL(input->gasAvailable(), gasLimit);

        if (count == 0)
        {
            input->setType(bcos::protocol::ExecutionMessage::MESSAGE);
            input->setTo("");
        }
        else
        {
            input->setType(bcos::protocol::ExecutionMessage::FINISHED);
        }
        input->setStatus(0);
        input->setMessage("");

        std::string data = "Hello world!";

        input->setData(bcos::bytes(data.begin(), data.end()));

        ++count;

        callback(nullptr, std::move(input));
    }


    void getHash(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback) override
    {
        BOOST_CHECK_GT(number, 0);
        callback(nullptr, h256(255));
    }

    std::map<std::string, std::function<void()>> batchContracts;
    std::set<bcos::h256> txHashes;
    std::set<int64_t> contextIDs;
    int count = 0;
    size_t gasLimit = 300000000;
};
#pragma GCC diagnostic pop
}  // namespace bcos::test