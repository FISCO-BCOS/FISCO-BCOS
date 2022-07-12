#pragma once
#include "Common.h"
#include "bcos-framework/interfaces/executor/ExecuteError.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/executor/NativeExecutionMessage.h"
#include "bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-scheduler/src/DmcExecutor.h"
#include <boost/test/unit_test.hpp>


using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::scheduler;
namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockDmcExecutor : public bcos::executor::ParallelTransactionExecutorInterface
{
public:
    using executorStatus = bcos::scheduler::DmcExecutor::Status;
    using Ptr = std::shared_ptr<MockDmcExecutor>();
    MockDmcExecutor(const std::string& name) : m_name(name) {}

    virtual ~MockDmcExecutor() {}
    // const std::string& name() const { return m_name; }

    void status(
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutorStatus::UniquePtr)>)
        override
    {}

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        // auto output = std::make_unique<bcos::protocol::ExecutionMessage>();
        // output = std::move(input);
        SCHEDULER_LOG(DEBUG) << LOG_KV(",input type ", input->type());
        if (input->to() == "0xaabbccdd")
        {
            std::string str = "Call Finished!";
            input->setType(protocol::ExecutionMessage::FINISHED);
            input->setData(bcos::bytes(str.begin(), str.end()));
            input->setGasAvailable(123456);
            SCHEDULER_LOG(DEBUG) << LOG_KV("call  finished, input type is ", input->type());
            callback(nullptr, std::move(input));
        }
        if (input->type() == bcos::protocol::ExecutionMessage::TXHASH)
        {
            std::string str = "Call error, Need Switch!";
            input->setData(bcos::bytes(str.begin(), str.end()));
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::SCHEDULER_TERM_ID_ERROR, "Call is error"),
                std::move(input));
            SCHEDULER_LOG(DEBUG) << LOG_KV("call  error, input type is ", input->type());
            return;
        }
    }


    void dmcExecuteTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        SCHEDULER_LOG(DEBUG) << LOG_KV("inputs size ", inputs.size());
        std::vector<bcos::protocol::ExecutionMessage::UniquePtr> results(inputs.size());
        for (decltype(inputs)::index_type i = 0; i < inputs.size(); i++)
        {
            SCHEDULER_LOG(DEBUG) << "begin  dmcExecute" << LOG_KV("input type ", inputs[i]->type());
            results.at(i) = std::move(inputs[i]);
            if (results.at(i)->transactionHash() == h256(10086))
            {
                callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR, "execute is error"),
                    std::move(results));
                return;
            }

            if (results[i]->type() == bcos::protocol::ExecutionMessage::KEY_LOCK)
            {
                SCHEDULER_LOG(DEBUG) << "setData, "
                                     << "type is keyLocks";
                std::string str = "DMCExecuteTransaction Finish, I am keyLock!";
                results[i]->setData(bcos::bytes(str.begin(), str.end()));
            }
            else
            {
                SCHEDULER_LOG(DEBUG) << "setData, "
                                     << "type is not keyLocks";
                results[i]->setType(bcos::protocol::ExecutionMessage::FINISHED);
                std::string str = "DMCExecuteTransaction Finish!";
                results[i]->setData(bcos::bytes(str.begin(), str.end()));
            }
        }
        SCHEDULER_LOG(DEBUG) << LOG_KV("results size ", results.size());
        callback(nullptr, std::move(results));
    };

    void nextBlockHeader(int64_t, const bcos::protocol::BlockHeader::ConstPtr&,
        std::function<void(bcos::Error::UniquePtr)>) override
    {}


    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>)
        override
    {}

    void dagExecuteTransactions(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr>,
        std::function<void(bcos::Error::UniquePtr,
            std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>) override
    {}

    void getHash(bcos::protocol::BlockNumber,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)>) override
    {}

    void prepare(const bcos::protocol::TwoPCParams&, std::function<void(bcos::Error::Ptr)>) override
    {}

    void commit(const bcos::protocol::TwoPCParams&, std::function<void(bcos::Error::Ptr)>) override
    {}

    void rollback(
        const bcos::protocol::TwoPCParams&, std::function<void(bcos::Error::Ptr)>) override
    {}

    void getCode(std::string_view, std::function<void(bcos::Error::Ptr, bcos::bytes)>) override {}
    void getABI(std::string_view, std::function<void(bcos::Error::Ptr, std::string)>) override {}
    void reset(std::function<void(bcos::Error::Ptr)>) override {}
    // void start() override() {}
    // void stop() override() {}


    std::string m_name;
};
#pragma GCC diagnostic pop
}  // namespace bcos::test