#pragma once

#include "bcos-executor/src/executor/TransactionExecutor.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <bcos-framework/executor/ParallelTransactionExecutorInterface.h>
#include <bcos-utilities/ThreadPool.h>
#include <thread>

namespace bcos::initializer
{
class ParallelExecutor : public executor::ParallelTransactionExecutorInterface
{
public:
    const int64_t INIT_SCHEDULER_TERM_ID = 0;

    ParallelExecutor(bcos::executor::TransactionExecutorFactory::Ptr factory)
      : m_pool("exec", std::thread::hardware_concurrency()), m_factory(factory)
    {
        refreshExecutor(INIT_SCHEDULER_TERM_ID);
    }

    ~ParallelExecutor() noexcept override {}

    void refreshExecutor(int64_t schedulerTermId)
    {
        // refresh when receive larger schedulerTermId
        if (schedulerTermId > m_schedulerTermId)
        {
            WriteGuard l(m_mutex);
            if (m_schedulerTermId != schedulerTermId)
            {
                // remove old executor and build new
                if (m_executor)
                {
                    m_executor->stop();
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

    bool hasNextBlockHeaderDone()
    {
        ReadGuard l(m_mutex);
        return m_schedulerTermId != INIT_SCHEDULER_TERM_ID;
    }

    void nextBlockHeader(int64_t schedulerTermId,
        const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr)> callback) override
    {
        refreshExecutor(schedulerTermId);


        m_pool.enqueue([executor = m_executor, schedulerTermId,
                           blockHeader = std::move(blockHeader), callback = std::move(callback)]() {
            // create a holder
            auto _holdExecutorCallback = [executorHolder = executor, callback =
                                                                         std::move(callback)](
                                             bcos::Error::UniquePtr error) {
                EXECUTOR_LOG(TRACE)
                    << "Release executor holder" << LOG_KV("ptr count", executorHolder.use_count());
                callback(std::move(error));
            };

            // execute the function
            executor->nextBlockHeader(
                schedulerTermId, blockHeader, std::move(_holdExecutorCallback));
        });
    }

    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        m_pool.enqueue([this, executor = m_executor, inputRaw = input.release(),
                           callback = std::move(callback)] {
            if (!hasNextBlockHeaderDone())
            {
                callback(
                    BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                        "Executor has just inited, need to switch"),
                    nullptr);
                return;
            }

            // create a holder
            auto _holdExecutorCallback = [executorHolder = executor, callback =
                                                                         std::move(callback)](
                                             bcos::Error::UniquePtr error,
                                             bcos::protocol::ExecutionMessage::UniquePtr output) {
                EXECUTOR_LOG(TRACE)
                    << "Release executor holder" << LOG_KV("ptr count", executorHolder.use_count());
                callback(std::move(error), std::move(output));
            };

            // execute the function
            executor->executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr(inputRaw),
                std::move(_holdExecutorCallback));
        });
    }

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        // Note: In order to ensure that the call is in the context of the life cycle of
        // BlockExecutive, the executor call cannot be put into m_pool

        // create a holder
        auto executor = m_executor;
        auto _holdExecutorCallback = [executorHolder = executor, callback = std::move(callback)](
                                         bcos::Error::UniquePtr error,
                                         bcos::protocol::ExecutionMessage::UniquePtr output) {
            EXECUTOR_LOG(TRACE) << "Release executor holder"
                                << LOG_KV("ptr count", executorHolder.use_count());
            callback(std::move(error), std::move(output));
        };

        // execute the function
        executor->call(bcos::protocol::ExecutionMessage::UniquePtr(input.release()),
            std::move(_holdExecutorCallback));
    }

    void executeTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        // Note: copy the inputs here in case of inputs has been released
        auto inputsVec =
            std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();
        for (auto i = 0u; i < inputs.size(); i++)
        {
            inputsVec->emplace_back(std::move(inputs[i]));
        }
        m_pool.enqueue([this, executor = m_executor, contractAddress = std::move(contractAddress),
                           inputsVec, callback = std::move(callback)] {
            if (!hasNextBlockHeaderDone())
            {
                callback(
                    BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                        "Executor has just inited, need to switch"),
                    {});
                return;
            }

            // create a holder
            auto _holdExecutorCallback =
                [executorHolder = executor, callback = std::move(callback)](
                    bcos::Error::UniquePtr error,
                    std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) {
                    EXECUTOR_LOG(TRACE) << "Release executor holder"
                                        << LOG_KV("ptr count", executorHolder.use_count());
                    callback(std::move(error), std::move(outputs));
                };

            // execute the function
            executor->executeTransactions(
                contractAddress, *inputsVec, std::move(_holdExecutorCallback));
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
        for (auto i = 0u; i < inputs.size(); i++)
        {
            inputsVec->emplace_back(std::move(inputs[i]));
        }
        m_pool.enqueue([this, executor = m_executor, contractAddress = std::move(contractAddress),
                           inputsVec, callback = std::move(callback)] {
            if (!hasNextBlockHeaderDone())
            {
                callback(
                    BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                        "Executor has just inited, need to switch"),
                    {});
                return;
            }

            // create a holder
            auto _holdExecutorCallback =
                [executorHolder = executor, callback = std::move(callback)](
                    bcos::Error::UniquePtr error,
                    std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) {
                    EXECUTOR_LOG(TRACE) << "Release executor holder"
                                        << LOG_KV("ptr count", executorHolder.use_count());
                    callback(std::move(error), std::move(outputs));
                };

            // execute the function
            executor->dmcExecuteTransactions(
                contractAddress, *inputsVec, std::move(_holdExecutorCallback));
        });
    }

    void dagExecuteTransactions(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        auto inputsVec =
            std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();
        for (auto i = 0u; i < inputs.size(); i++)
        {
            inputsVec->emplace_back(std::move(inputs[i]));
        }
        m_pool.enqueue([this, executor = m_executor, inputsVec, callback = std::move(callback)] {
            if (!hasNextBlockHeaderDone())
            {
                callback(
                    BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                        "Executor has just inited, need to switch"),
                    {});
                return;
            }

            // create a holder
            auto _holdExecutorCallback =
                [executorHolder = executor, callback = std::move(callback)](
                    bcos::Error::UniquePtr error,
                    std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) {
                    EXECUTOR_LOG(TRACE) << "Release executor holder"
                                        << LOG_KV("ptr count", executorHolder.use_count());
                    callback(std::move(error), std::move(outputs));
                };

            // execute the function
            executor->dagExecuteTransactions(*inputsVec, std::move(_holdExecutorCallback));
        });
    }

    void dmcCall(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        // Note: In order to ensure that the call is in the context of the life cycle of
        // BlockExecutive, the executor call cannot be put into m_pool

        // create a holder
        auto executor = m_executor;
        auto _holdExecutorCallback = [executorHolder = executor, callback = std::move(callback)](
                                         bcos::Error::UniquePtr error,
                                         bcos::protocol::ExecutionMessage::UniquePtr output) {
            EXECUTOR_LOG(TRACE) << "Release executor holder"
                                << LOG_KV("ptr count", executorHolder.use_count());
            callback(std::move(error), std::move(output));
        };

        // execute the function
        executor->dmcCall(bcos::protocol::ExecutionMessage::UniquePtr(input.release()),
            std::move(_holdExecutorCallback));
    }

    void getHash(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback) override
    {
        m_pool.enqueue([executor = m_executor, number, callback = std::move(callback)] {
            // create a holder
            auto _holdExecutorCallback =
                [executorHolder = executor, callback = std::move(callback)](
                    bcos::Error::UniquePtr error, crypto::HashType hashType) {
                    EXECUTOR_LOG(TRACE) << "Release executor holder"
                                        << LOG_KV("ptr count", executorHolder.use_count());
                    callback(std::move(error), std::move(hashType));
                };

            // execute the function
            executor->getHash(number, std::move(_holdExecutorCallback));
        });
    }

    /* ----- XA Transaction interface Start ----- */

    // Write data to storage uncommitted
    void prepare(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_pool.enqueue([this, executor = m_executor, params = bcos::protocol::TwoPCParams(params),
                           callback = std::move(callback)] {
            if (!hasNextBlockHeaderDone())
            {
                callback(
                    BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                        "Executor has just inited, need to switch"));
                return;
            }

            // create a holder
            auto _holdExecutorCallback = [executorHolder = executor, callback =
                                                                         std::move(callback)](
                                             bcos::Error::Ptr error) {
                EXECUTOR_LOG(TRACE)
                    << "Release executor holder" << LOG_KV("ptr count", executorHolder.use_count());
                callback(std::move(error));
            };

            // execute the function
            executor->prepare(params, std::move(_holdExecutorCallback));
        });
    }

    // Commit uncommitted data
    void commit(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_pool.enqueue([this, executor = m_executor, params = bcos::protocol::TwoPCParams(params),
                           callback = std::move(callback)] {
            if (!hasNextBlockHeaderDone())
            {
                callback(
                    BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                        "Executor has just inited, need to switch"));
                return;
            }

            // create a holder
            auto _holdExecutorCallback = [executorHolder = executor, callback =
                                                                         std::move(callback)](
                                             bcos::Error::Ptr error) {
                EXECUTOR_LOG(TRACE)
                    << "Release executor holder" << LOG_KV("ptr count", executorHolder.use_count());
                callback(std::move(error));
            };

            // execute the function
            executor->commit(params, std::move(_holdExecutorCallback));
        });
    }

    // Rollback the changes
    void rollback(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_pool.enqueue([this, executor = m_executor, params = bcos::protocol::TwoPCParams(params),
                           callback = std::move(callback)] {
            if (!hasNextBlockHeaderDone())
            {
                callback(
                    BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                        "Executor has just inited, need to switch"));
                return;
            }

            // create a holder
            auto _holdExecutorCallback = [executorHolder = executor, callback =
                                                                         std::move(callback)](
                                             bcos::Error::Ptr error) {
                EXECUTOR_LOG(TRACE)
                    << "Release executor holder" << LOG_KV("ptr count", executorHolder.use_count());
                callback(std::move(error));
            };

            // execute the function
            executor->rollback(params, std::move(_holdExecutorCallback));
        });
    }

    /* ----- XA Transaction interface End ----- */

    // drop all status
    void reset(std::function<void(bcos::Error::Ptr)> callback) override
    {
        m_pool.enqueue([executor = m_executor, callback = std::move(callback)] {
            // create a holder
            auto _holdExecutorCallback = [executorHolder = executor, callback =
                                                                         std::move(callback)](
                                             bcos::Error::Ptr error) {
                EXECUTOR_LOG(TRACE)
                    << "Release executor holder" << LOG_KV("ptr count", executorHolder.use_count());
                callback(std::move(error));
            };

            // execute the function
            executor->reset(std::move(_holdExecutorCallback));
        });
    }
    void getCode(std::string_view contract,
        std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        m_pool.enqueue([executor = m_executor, contract = std::string(contract),
                           callback = std::move(callback)] {
            // create a holder
            auto _holdExecutorCallback = [executorHolder = executor, callback =
                                                                         std::move(callback)](
                                             bcos::Error::Ptr error, bcos::bytes bytes) {
                EXECUTOR_LOG(TRACE)
                    << "Release executor holder" << LOG_KV("ptr count", executorHolder.use_count());
                callback(std::move(error), std::move(bytes));
            };

            // execute the function
            executor->getCode(contract, std::move(_holdExecutorCallback));
        });
    }

    void getABI(std::string_view contract,
        std::function<void(bcos::Error::Ptr, std::string)> callback) override
    {
        m_pool.enqueue([executor = m_executor, contract = std::string(contract),
                           callback = std::move(callback)] {
            // create a holder
            auto _holdExecutorCallback = [executorHolder = executor, callback =
                                                                         std::move(callback)](
                                             bcos::Error::Ptr error, std::string str) {
                EXECUTOR_LOG(TRACE)
                    << "Release executor holder" << LOG_KV("ptr count", executorHolder.use_count());
                callback(std::move(error), std::move(str));
            };

            // execute the function
            executor->getABI(contract, std::move(_holdExecutorCallback));
        });
    }

private:
    bcos::ThreadPool m_pool;
    bcos::executor::TransactionExecutor::Ptr m_executor;
    int64_t m_schedulerTermId = -1;

    mutable bcos::SharedMutex m_mutex;

    bcos::executor::TransactionExecutorFactory::Ptr m_factory;
};
}  // namespace bcos::initializer