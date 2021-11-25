#pragma once

#include "bcos-executor/TransactionExecutor.h"
#include "interfaces/executor/ExecutionMessage.h"
#include "libutilities/ThreadPool.h"
#include <bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h>
#include <bcos-framework/libutilities/ThreadPool.h>
#include <thread>

namespace bcos::initializer
{
class ParallelExecutor : public executor::ParallelTransactionExecutorInterface
{
public:
    ParallelExecutor(bcos::executor::TransactionExecutor::Ptr executor)
      : m_pool("exec", std::thread::hardware_concurrency()), m_executor(std::move(executor))
    {}
    ~ParallelExecutor() noexcept override {}

    void nextBlockHeader(const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr)> callback) override
    {
        m_pool.enqueue(
            [this, blockHeader = std::move(blockHeader), callback = std::move(callback)]() {
                m_executor->nextBlockHeader(blockHeader, std::move(callback));
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

    void dagExecuteTransactions(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        m_pool.enqueue([this, inputs = std::move(inputs), callback = std::move(callback)] {
            m_executor->dagExecuteTransactions(std::move(inputs), std::move(callback));
        });
    }

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        m_pool.enqueue([this, inputRaw = input.release(), callback = std::move(callback)] {
            m_executor->call(
                bcos::protocol::ExecutionMessage::UniquePtr(inputRaw), std::move(callback));
        });
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
    void prepare(const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_pool.enqueue([this, params = TwoPCParams(params), callback = std::move(callback)] {
            m_executor->prepare(params, std::move(callback));
        });
    }

    // Commit uncommitted data
    void commit(const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_pool.enqueue([this, params = TwoPCParams(params), callback = std::move(callback)] {
            m_executor->commit(params, std::move(callback));
        });
    }

    // Rollback the changes
    void rollback(
        const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_pool.enqueue([this, params = TwoPCParams(params), callback = std::move(callback)] {
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

private:
    bcos::ThreadPool m_pool;
    bcos::executor::TransactionExecutor::Ptr m_executor;
};
}  // namespace bcos::initializer