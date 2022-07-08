#pragma once
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h"
#include <boost/test/unit_test.hpp>


using namespace bcos;
using namespace bcos::executor;
using namespace bcos::protocol;
namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

class MockDmcExecutor : public bcos::executor::ParallelTransactionExecutorInterface
{
public:
    using Ptr = std::shared_ptr<bcos::test::MockDmcExecutor>();
    MockDmcExecutor(const std::string& name) : m_name(name) {}

    virtual ~MockDmcExecutor() {}

    // const std::string& name() const { return m_name; }

    void status(
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutorStatus::UniquePtr)>
            callback) override
    {
        callback(nullptr, std::move());
    }

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        BOOST_CHECK_EQUAL(output->type(), bcos::protocol::ExecutionMessage::MESSAGE);
        if (input->to() == "0xaabbccdd")
        {
            auto output = std::move(input);
            output->setType(bcos::protocol::ExecutionMessage::FINISHED);
            std::string str = "Call Finished!";
            output->setData(bcos::bytes(data.begin(), data.end()));
            callback(nullptr, std::move(output));
            return;
        }
        else
        {
            callback(BCOS_ERROR_UNIQUE_PTR(-1, "i am an error!!!!"), nullptr)
        }
        // BOOST_CHECK_EQUAL(input->from().size(), 40);
    }


    void dmcExecuteTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        std::vector<bcos::protocol::ExecutionMessage::UniquePtr> results(inputs.size());
        for (auto i = 0; i < inputs.size(); i++)
        {
            result.at(i) = std::move(inputs[i]);
            BOOST_CHECK(input->seq);
            if (result.at(i)->transactionHash() == h256(10086))
            {
                callback(BCOS_ERROR_UNIQUE_PTR(-1, "i am an error!!!!"), nullptr);
                return;
            }


            if (result[i]->type == bcos::protocol::ExecutionMessage::KEY_LOCK)
            {
                result[i]->setType(bcos::protocol::ExecutionMessage::LOCKED);
                std::string str = "DMCExecuteTransaction Finish, I am keyLock!";
                result[i]->setData(bcos::bytes(str.begin(), str.end()));
            }
            else
            {
                result[i]->setType(bcos::protocol::ExecutionMessage::FINISHED);
                std::string str = "DMCExecuteTransaction Finish!";
                result[i]->setData(bcos::bytes(str.begin(), str.end()));
            }
        }
        callback(nullptr, std::move(results));
    };

    void nextBlockHeader(int64_t schedulerTermId, const bcos::protocol::BlockHeader::ConstPtr&,
        std::function<void(bcos::Error::UniquePtr)> callback) override
    {
        callback(nullptr);
    }


    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        // Always success
        callback(nullptr, std::move(input));
    }

    void dagExecuteTransactions(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        callback(nullptr, std::move(inputs));
    }

    void getHash(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback) override
    {
        callback(nullptr, h256(12345));
    }

    void prepare(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        callback(nullptr);
    }

    void commit(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        callback(nullptr);
    }

    void rollback(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        callback(nullptr);
    }

    void getCode(std::string_view contract,
        std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        callback(nullptr, {});
    }
    void getABI(std::string_view contract,
        std::function<void(bcos::Error::Ptr, std::string)> callback) override
    {
        callback(nullptr, {});
    }
    void reset(std::function<void(bcos::Error::Ptr)> callback) override { callback(nullptr); }
    void start() override() {}
    void stop() override() {}


    std::string m_name;
};
#pragma GCC diagnostic pop
}  // namespace bcos::test