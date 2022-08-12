#pragma once
#include "Common.h"
#include "bcos-framework/executor/ExecuteError.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/executor/NativeExecutionMessage.h"
#include "bcos-framework/executor/ParallelTransactionExecutorInterface.h"
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

    void dmcCall(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        // auto output = std::make_unique<bcos::protocol::ExecutionMessage>();
        // output = std::move(input);
        SCHEDULER_LOG(DEBUG) << LOG_KV(",input type ", input->type());
        if (input->type() == bcos::protocol::ExecutionMessage::TXHASH)
        {
            std::string str = "Call error, Need Switch!";
            input->setData(bcos::bytes(str.begin(), str.end()));
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::SCHEDULER_TERM_ID_ERROR, "Call is error"),
                std::move(input));
            SCHEDULER_LOG(DEBUG) << LOG_KV("call  error, input type is ", input->type());
            return;
        }
        else
        {
            if (input->to() == "0xaabbccdd")
            {
                std::string str = "Call Finished!";
                input->setType(protocol::ExecutionMessage::FINISHED);
                input->setData(bcos::bytes(str.begin(), str.end()));
                input->setGasAvailable(123456);
                SCHEDULER_LOG(DEBUG) << LOG_KV("call  finished, input type is ", input->type());
                callback(nullptr, std::move(input));
            }
            else
            {
                DMC_LOG(FATAL) << "Error! Need schedulerOut, But perform Call!";
                assert(false);
                callback(
                    BCOS_ERROR_UNIQUE_PTR(ExecuteError::SCHEDULER_TERM_ID_ERROR, "Call is error"),
                    std::move(input));
                return;
            }
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
        for (auto i = 0u; i < inputs.size(); i++)
        {
            SCHEDULER_LOG(DEBUG) << "begin  dmcExecute" << LOG_KV("input type ", inputs[i]->type());
            results.at(i) = std::move(inputs[i]);
            if (results.at(i)->to() == "aabbccdd")
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

    void nextBlockHeader(int64_t, const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr)> callback) override
    {
        SCHEDULER_LOG(TRACE) << "Receiving nextBlock: " << blockHeader->number();
        m_blockNumber = blockHeader->number();
        callback(nullptr);  // always success
    }


    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        if (input->to() == "0xaabbccdd")
        {
            callback(BCOS_ERROR_UNIQUE_PTR(-1, "i am an error!!!!"), nullptr);
            return;
        }

        // Always success
        BOOST_CHECK(input);
        if (input->type() == bcos::protocol::ExecutionMessage::TXHASH)
        {
            BOOST_CHECK_NE(input->transactionHash(), bcos::crypto::HashType());
        }

        input->setStatus(0);
        input->setMessage("");

        std::string data = "Hello world!";
        input->setData(bcos::bytes(data.begin(), data.end()));
        input->setType(bcos::protocol::ExecutionMessage::FINISHED);

        callback(nullptr, std::move(input));
    }

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {}

    void executeTransactions(std::string, gsl::span<bcos::protocol::ExecutionMessage::UniquePtr>,
        std::function<void(bcos::Error::UniquePtr,
            std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>) override
    {}

    void dagExecuteTransactions(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        std::vector<bcos::protocol::ExecutionMessage::UniquePtr> messages(inputs.size());
        for (auto i = 0u; i < inputs.size(); ++i)
        {
            auto [it, inserted] = m_dagHashes.emplace(inputs[i]->transactionHash());
            boost::ignore_unused(it);
            BOOST_TEST(inserted);

            // SCHEDULER_LOG(TRACE) << "Executing: " << inputs[i].get();
            BOOST_TEST(inputs[i].get());
            messages.at(i) = std::move(inputs[i]);
            if (i < 5)
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


    std::string m_name;
    bcos::protocol::BlockNumber m_blockNumber = 0;
    std::set<bcos::crypto::HashType> m_dagHashes;
};
#pragma GCC diagnostic pop
}  // namespace bcos::test