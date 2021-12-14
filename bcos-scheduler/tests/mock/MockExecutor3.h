#pragma once
#include "../bcos-scheduler/Common.h"
#include "MockExecutor.h"
#include <boost/lexical_cast.hpp>
#include <tuple>

namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockParallelExecutor3 : public MockParallelExecutor
{
public:
    MockParallelExecutor3(const std::string& name) : MockParallelExecutor(name) {}

    ~MockParallelExecutor3() override {}

    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        BOOST_CHECK_EQUAL(input->type(), protocol::ExecutionMessage::TXHASH);
        BOOST_CHECK_GE(input->contextID(), 0);

        // if (input->transactionHash() == h256(9))
        // {
        //     BOOST_CHECK(input->to().find("contract") == std::string::npos);
        // }

        // Always success
        SCHEDULER_LOG(TRACE) << "Input, hash:" << input->transactionHash().hex()
                             << " contract:" << input.get() << " to:" << input->to();
        BOOST_CHECK(input);
        BOOST_CHECK(!input->to().empty());
        BOOST_CHECK_EQUAL(input->depth(), 0);
        BOOST_CHECK_EQUAL(input->gasAvailable(), scheduler::TRANSACTION_GAS);

        input->setType(bcos::protocol::ExecutionMessage::FINISHED);
        input->setStatus(0);
        input->setMessage("");

        std::string data = "Hello world!";
        input->setData(bcos::bytes(data.begin(), data.end()));

        auto [it, inserted] = txHashes.emplace(input->transactionHash());
        (void)it;
        if (!inserted)
        {
            BOOST_FAIL("Duplicate hash: " + input->transactionHash().hex());
        }

        auto [it3, inserted3] = contextIDs.emplace(input->contextID());
        (void)it3;
        if (!inserted3)
        {
            BOOST_FAIL(
                "Duplicate contextID: " + boost::lexical_cast<std::string>(input->contextID()));
        }

        auto inputShared =
            std::make_shared<bcos::protocol::ExecutionMessage::UniquePtr>(std::move(input));

        auto [it2, inserted2] = batchContracts.emplace(std::string((*inputShared)->to()),
            [callback, inputShared]() { callback(nullptr, std::move(*inputShared)); });

        (void)it2;
        if (!inserted2)
        {
            BOOST_FAIL("Duplicate contract: " + std::string((*inputShared)->to()));
        }

        if (batchContracts.size() == 3)
        {
            // current batch is finished
            std::map<std::string, std::function<void()>> results;
            results.swap(batchContracts);

            for (auto& it : results)
            {
                (it.second)();
            }
        }
    }

    void prepare(const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback) override
    {
        BOOST_CHECK_EQUAL(params.number, 100);
        callback(nullptr);
    }

    void commit(const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback) override
    {
        BOOST_CHECK_EQUAL(params.number, 100);
        callback(nullptr);
    }

    void rollback(
        const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback) override
    {
        BOOST_CHECK_EQUAL(params.number, 100);
        callback(nullptr);
    }

    void getHash(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback) override
    {
        BOOST_CHECK_EQUAL(number, 100);
        callback(nullptr, h256(255));
    }

    std::map<std::string, std::function<void()>> batchContracts;
    std::set<bcos::h256> txHashes;
    std::set<int64_t> contextIDs;
};
#pragma GCC diagnostic pop
}  // namespace bcos::test