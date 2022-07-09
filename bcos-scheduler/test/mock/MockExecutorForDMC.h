#pragma once
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-scheduler/src/DmcExecutor.h"
#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::scheduler;
using namespace bcos::test
{
    class MockDmcExecutor : public bcos::executor::ParallelTransactionExecutorInterface
    {
    public:
        using executorStatus = bcos::scheduler::DmcExecutor::Status;
        using Ptr = std::shared_ptr<bcos::test::MockDmcExecutor>();
        MockDmcExecutor(const std::string& name) : m_name(name) {}

        virtual ~MockDmcExecutor() {}
        // const std::string& name() const { return m_name; }

        void status(
            std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutorStatus::UniquePtr)>
                callback) override
        {
            auto status = std::make_unique<bcos::protocol::ExecutorStatus>();
            status->setSeq(0);
            callback(nullptr, std::move(status));
        }

        void call(bcos::protocol::ExecutionMessage::UniquePtr input,
            std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
                callback) override
        {
            auto output = std::make_unique<bcos::protocol::ExecutionMessage>();
            output = std::move(input);
            if (output->to() == "0xaabbccdd")
            {
                output->setType(bcos::protocol::ExecutionMessage::FINISHED);
                std::string str = "Call Finished!";
                output->setData(bcos::bytes(str.begin(), str.end()));
                callback(nullptr, executorStatus::PAUSED);
                return;
            }
            else
            {
                callback(nullptr, executorStatus::ERROR);
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
                results.at(i) = std::move(inputs[i]);
                if (results.at(i)->transactionHash() == h256(10086))
                {
                    callback(nullptr, executorStatus::ERROR);
                    return;
                }

                if (results[i]->type == bcos::protocol::ExecutionMessage::KEY_LOCK)
                {
                    std::string str = "DMCExecuteTransaction Finish, I am keyLock!";
                    results[i]->setData(bcos::bytes(str.begin(), str.end()));
                    callback(nullptr, executorStatus::PAUSE);
                }
                else
                {
                    results[i]->setType(bcos::protocol::ExecutionMessage::FINISHED);
                    std::string str = "DMCExecuteTransaction Finish!";
                    results[i]->setData(bcos::bytes(str.begin(), str.end()));
                    callback(nullptr, executorStatus::FINISHED);
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
        // void start() override() {}
        // void stop() override() {}

    private:
        std::string m_name;
    };
}  // namespace bcos::test