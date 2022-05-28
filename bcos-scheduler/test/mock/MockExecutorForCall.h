#pragma once

#include "Common.h"
#include "MockExecutor.h"
#include "bcos-framework//executor/ExecutionMessage.h"
#include <bcos-framework//executor/PrecompiledTypeDef.h>
#include <boost/lexical_cast.hpp>
#include <tuple>

namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockParallelExecutorForCall : public MockParallelExecutor
{
public:
    MockParallelExecutorForCall(const std::string& name) : MockParallelExecutor(name) {}

    ~MockParallelExecutorForCall() override {}

    void nextBlockHeader(const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr)> callback) override
    {
        BOOST_FAIL("Unexpected nextBlockHeader!");
    }

    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        BOOST_FAIL("Unexpected execute!");
    }

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        if (input->to() == precompiled::AUTH_COMMITTEE_ADDRESS)
        {
            if (input->create())
            {
                callback(BCOS_ERROR_UNIQUE_PTR(-1, "deploy sys contract!"), nullptr);
                return;
            }
            input->setType(protocol::ExecutionMessage::FINISHED);
            std::string data = "Hello world! response";
            input->setData(bcos::bytes(data.begin(), data.end()));
            input->setStatus(0);
            input->setGasAvailable(123456);
            callback(nullptr, std::move(input));
            return;
        }

        BOOST_CHECK_EQUAL(input->type(), protocol::ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(input->to(), "address_to");
        // BOOST_CHECK_EQUAL(input->from().size(), 40);

        auto inputBytes = input->data();
        std::string inputStr((char*)inputBytes.data(), inputBytes.size());

        BOOST_CHECK_EQUAL(inputStr, "Hello world! request");

        input->setType(protocol::ExecutionMessage::FINISHED);

        std::string data = "Hello world! response";
        input->setData(bcos::bytes(data.begin(), data.end()));
        input->setStatus(0);
        input->setGasAvailable(123456);
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
};
#pragma GCC diagnostic pop
}  // namespace bcos::test