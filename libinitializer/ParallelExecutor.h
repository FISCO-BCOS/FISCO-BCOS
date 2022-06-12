#pragma once

#include "bcos-executor/src/executor/TransactionExecutor.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include <bcos-framework/executor/ParallelTransactionExecutorInterface.h>
#include <bcos-utilities/ThreadPool.h>
#include <thread>

namespace bcos::initializer
{
class ParallelExecutor : public executor::ParallelTransactionExecutorInterface
{
public:
    ParallelExecutor(bcos::executor::TransactionExecutorFactory::Ptr factory)
      : m_pool("exec", std::thread::hardware_concurrency()), m_factory(factory)
    {
        refreshExecutor(0);
    }

    ~ParallelExecutor() noexcept override {}

    void refreshExecutor(int64_t schedulerTermId)
    {
        // refresh when receive larger schedulerTermId
        if (schedulerTermId > m_schedulerTermId)
        {
            static bcos::SharedMutex mutex;
            WriteGuard l(mutex);
            if (m_schedulerTermId != schedulerTermId)
            {
                // remove old executor and build new
                if (m_executor)
                {
                    m_executor->stop();
                    m_oldExecutor = m_executor;  // TODO: remove this
                }

                // TODO: check cycle reference in executor to avoid memory leak
                EXECUTOR_LOG(DEBUG)
                    << LOG_BADGE("Switch") << "ExecutorSwitch: Build new executor instance with "
                    << LOG_KV("schedulerTermId", schedulerTermId);
                m_executor = m_factory->build();

                m_schedulerTermId = schedulerTermId;
            }
        }
    }

    void nextBlockHeader(int64_t schedulerTermId,
        const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr)> callback) override
    {
        refreshExecutor(schedulerTermId);

        m_pool.enqueue([this, schedulerTermId, blockHeader = std::move(blockHeader),
                           callback = std::move(callback)]() {
            m_executor->nextBlockHeader(schedulerTermId, blockHeader, std::move(callback));
        });
    }

    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        m_pool.enqueue([this, inputRaw = input.release(), callback = std::move(callback)] {
            m_executor->executeTransaction(
                bcos::protocol::ExecutionMessage::UniquePtr(inputRaw), std::move(callback));
        });
    }

    void dmcExecuteTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        // Note: copy the inputs here in case of inputs has been released
        auto inputsVec =
            std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();
        for (decltype(inputs)::index_type i = 0; i < inputs.size(); i++)
        {
            inputsVec->emplace_back(std::move(inputs.at(i)));
        }
        m_pool.enqueue([this, contractAddress = std::move(contractAddress), inputsVec,
                           callback = std::move(callback)] {
            m_executor->dmcExecuteTransactions(contractAddress, *inputsVec, std::move(callback));
        });
    }

    void dagExecuteTransactions(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        auto inputsVec =
            std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();
        for (decltype(inputs)::index_type i = 0; i < inputs.size(); i++)
        {
            inputsVec->emplace_back(std::move(inputs.at(i)));
        }
        m_pool.enqueue([this, inputsVec, callback = std::move(callback)] {
            m_executor->dagExecuteTransactions(*inputsVec, std::move(callback));
        });
    }

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        // Note: In order to ensure that the call is in the context of the life cycle of
        // BlockExecutive, the executor call cannot be put into m_pool
        m_executor->call(
            bcos::protocol::ExecutionMessage::UniquePtr(input.release()), std::move(callback));
    }

    void getHash(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback) override
    {
        m_pool.enqueue([this, number, callback = std::move(callback)] {
            m_executor->getHash(number, std::move(callback));
        });
    }

    /* ----- XA Transaction interface Start ----- */

    // Write data to storage uncommitted
    void prepare(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_pool.enqueue(
            [this, params = bcos::protocol::TwoPCParams(params), callback = std::move(callback)] {
                m_executor->prepare(params, std::move(callback));
            });
    }

    // Commit uncommitted data
    void commit(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_pool.enqueue(
            [this, params = bcos::protocol::TwoPCParams(params), callback = std::move(callback)] {
                m_executor->commit(params, std::move(callback));
            });
    }

    // Rollback the changes
    void rollback(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_pool.enqueue(
            [this, params = bcos::protocol::TwoPCParams(params), callback = std::move(callback)] {
                m_executor->rollback(params, std::move(callback));
            });
    }

    /* ----- XA Transaction interface End ----- */

    // drop all status
    void reset(std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_pool.enqueue(
            [this, callback = std::move(callback)] { m_executor->reset(std::move(callback)); });
    }
    void getCode(std::string_view contract,
        std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        m_pool.enqueue([this, contract = std::string(contract), callback = std::move(callback)] {
            m_executor->getCode(contract, std::move(callback));
        });
    }

    void getABI(std::string_view contract,
        std::function<void(bcos::Error::Ptr, std::string)> callback) override
    {
        m_pool.enqueue([this, contract = std::string(contract), callback = std::move(callback)] {
            m_executor->getABI(contract, std::move(callback));
        });
    }

private:
    bcos::ThreadPool m_pool;
    bcos::executor::TransactionExecutor::Ptr m_executor;
    bcos::executor::TransactionExecutor::Ptr m_oldExecutor;
    int64_t m_schedulerTermId = -1;

    bcos::executor::TransactionExecutorFactory::Ptr m_factory;
};
}  // namespace bcos::initializer