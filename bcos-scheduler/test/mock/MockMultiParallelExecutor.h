#pragma once

#include "MockExecutor.h"
#include <bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h>
#include <tbb/task_group.h>

namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockMultiParallelExecutor : public MockParallelExecutor
{
public:
    MockMultiParallelExecutor(const std::string& name) : MockParallelExecutor(name) {}
    ~MockMultiParallelExecutor() noexcept override { m_taskGroup.wait(); }

    void nextBlockHeader(int64_t schedulerTermId,
        const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr)> callback) override
    {
        m_taskGroup.run([this, blockHeader = blockHeader, callback = std::move(callback)]() {
            MockParallelExecutor::nextBlockHeader(0, blockHeader, std::move(callback));
        });
    }

    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        m_taskGroup.run([this, inputRaw = input.release(), callback = std::move(callback)]() {
            MockParallelExecutor::executeTransaction(
                bcos::protocol::ExecutionMessage::UniquePtr(inputRaw), std::move(callback));
        });
    }

    void dagExecuteTransactions(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        m_taskGroup.run([this, inputs = std::move(inputs), callback = std::move(callback)]() {
            MockParallelExecutor::dagExecuteTransactions(std::move(inputs), std::move(callback));
        });
    }

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {}

    void getHash(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback) override
    {
        m_taskGroup.run([this, number = number, callback = std::move(callback)]() {
            MockParallelExecutor::getHash(number, std::move(callback));
        });
    }

    /* ----- XA Transaction interface Start ----- */

    // Write data to storage uncommitted
    void prepare(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_taskGroup.run([this, params = params, callback = std::move(callback)]() {
            MockParallelExecutor::prepare(params, std::move(callback));
        });
    }

    // Commit uncommitted data
    void commit(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_taskGroup.run([this, params = params, callback = std::move(callback)]() {
            MockParallelExecutor::commit(params, std::move(callback));
        });
    }

    // Rollback the changes
    void rollback(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_taskGroup.run([this, params = params, callback = std::move(callback)]() {
            MockParallelExecutor::rollback(params, std::move(callback));
        });
    }

    /* ----- XA Transaction interface End ----- */

    // drop all status
    void reset(std::function<void(bcos::Error::Ptr)> callback) override {}

private:
    tbb::task_group m_taskGroup;
};
#pragma GCC diagnostic pop
}  // namespace bcos::test