#pragma once

#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/interfaces/protocol/ProtocolTypeDef.h"
#include "bcos-scheduler/src/Common.h"
#include <boost/core/ignore_unused.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::executor;
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
        if (input->to() == "0xaabbccdd")
        {
            if (input->type() == 0)
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

        // bytes to string
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
            BOOST_CHECK(input);
            if (result.at(i)->transactionHash() == h256(10086))
            {
                callback(BCOS_ERROR_UNIQUE_PTR(-1, "i am an error!!!!"), nullptr);
                return;
            }

            if (result[i]->type() == bcos::protocol::ExecutionMessage::TXHASH)
            {
                result[i]->setType(protocol::ExecutionMessage::FINISHED);
                std::string str = "OK!";
                result[i]->setData(bcos::bytes(str.begin(), str.end()));
            }
            if (result[i]->type == bcos::protocol::ExecutionMessage::KEY_LOCK)
            {
                result[i]->setType(protocol::ExecutionMessage::SEND_BACK);
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
        BOOST_CHECK(input);
        input->setStatus(0);
        input->setMessage("");
        std::string data = "Hello world!";
        input->setData(bcos::bytes(data.begin(), data.end()));
        input->setType(bcos::protocol::ExecutionMessage::FINISHED);
        callback(nullptr, std::move(input));
    }

    void dagExecuteTransactions(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        BOOST_CHECK_EQUAL(inputs.size(), 100);

        std::vector<bcos::protocol::ExecutionMessage::UniquePtr> messages(inputs.size());
        for (decltype(inputs)::index_type i = 0; i < inputs.size(); ++i)
        {
            auto [it, inserted] = m_dagHashes.emplace(inputs[i]->transactionHash());
            boost::ignore_unused(it);
            BOOST_TEST(inserted);

            // SCHEDULER_LOG(TRACE) << "Executing: " << inputs[i].get();
            BOOST_TEST(inputs[i].get());
            BOOST_CHECK_EQUAL(inputs[i]->type(), protocol::ExecutionMessage::TXHASH);
            messages.at(i) = std::move(inputs[i]);
            if (i < 50)
            {
                messages[i]->setType(protocol::ExecutionMessage::SEND_BACK);
            }
            else
            {
                messages[i]->setType(protocol::ExecutionMessage::FINISHED);

                std::string result = "OK!";
                messages[i]->setData(bcos::bytes(result.begin(), result.end()));
            }
        }
        callback(nullptr, std::move(messages));
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