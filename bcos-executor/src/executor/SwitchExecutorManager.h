#pragma once

#include "TransactionExecutorFactory.h"
#include "bcos-executor/src/executor/TransactionExecutor.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <bcos-framework/executor/ParallelTransactionExecutorInterface.h>
#include <bcos-utilities/ThreadPool.h>
#include <thread>

namespace bcos::executor
{
class SwitchExecutorManager : public executor::ParallelTransactionExecutorInterface
{
public:
    using Ptr = std::shared_ptr<SwitchExecutorManager>;

    const int64_t INIT_SCHEDULER_TERM_ID = 0;
    const int64_t STOPPED_TERM_ID = -1;

    SwitchExecutorManager(bcos::executor::TransactionExecutorFactory::Ptr factory)
      : m_pool("exec", std::thread::hardware_concurrency()), m_factory(factory)
    {
        factory->registerNeedSwitchEvent([this]() { selfAsyncRefreshExecutor(); });

        // refreshExecutor(INIT_SCHEDULER_TERM_ID + 1);
    }

    ~SwitchExecutorManager() noexcept override {}

    void refreshExecutor(int64_t schedulerTermId)
    {
        // refresh when receive larger schedulerTermId
        if (schedulerTermId > m_schedulerTermId)
        {
            bcos::executor::TransactionExecutor::Ptr oldExecutor;
            {
                WriteGuard l(m_mutex);
                if (m_schedulerTermId != schedulerTermId)
                {
                    oldExecutor = m_executor;

                    m_executor = m_factory->build();  // may throw exception

                    m_schedulerTermId = schedulerTermId;

                    EXECUTOR_LOG(DEBUG) << LOG_BADGE("Switch")
                                        << "ExecutorSwitch: Build new executor instance with "
                                        << LOG_KV("schedulerTermId", schedulerTermId);
                }
            }
            if (oldExecutor)
            {
                oldExecutor->stop();
            }
        }
    }

    void selfAsyncRefreshExecutor()
    {
        auto toTermId = m_schedulerTermId + 1;
        auto toSeq = m_seq + 1;
        m_pool.enqueue([toTermId, toSeq, this]() {
            if (toTermId == m_schedulerTermId)
            {
                // already switch
                return;
            }

            try
            {
                refreshExecutor(toTermId);
            }
            catch (Exception const& _e)
            {
                EXECUTOR_LOG(WARNING)
                    << LOG_DESC("selfAsyncRefreshExecutor exception. Re-push to task pool")
                    << LOG_KV("toTermId", toTermId) << LOG_KV("currentTermId", m_schedulerTermId)
                    << diagnostic_information(_e);

                selfAsyncRefreshExecutor();
                return;
            }


            if (toTermId == m_schedulerTermId)
            {
                // if switch success, set seq to trigger scheduler switch
                m_seq = toSeq;
                EXECUTOR_LOG(DEBUG)
                    << LOG_BADGE("Switch") << "ExecutorSwitch: selfAsyncRefreshExecutor success"
                    << LOG_KV("schedulerTermId", m_schedulerTermId) << LOG_KV("seq", m_seq);
            }
        });
    }

    void triggerSwitch() { selfAsyncRefreshExecutor(); }

    bool hasStopped() { return m_schedulerTermId == STOPPED_TERM_ID; }

    bool hasNextBlockHeaderDone() { return m_schedulerTermId != INIT_SCHEDULER_TERM_ID; }

    void nextBlockHeader(int64_t schedulerTermId,
        const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr)> callback) override
    {
        if (hasStopped())
        {
            std::string message = "nextBlockHeader: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message));
            return;
        }

        if (schedulerTermId < m_schedulerTermId)
        {
            // Call from an outdated scheduler instance
            // Just return without callback, because run callback may trigger a new switch, thus
            // some message will be outdated and trigger switch again and again.
            EXECUTOR_LOG(INFO)
                << LOG_DESC("nextBlockHeader: not refreshExecutor for invalid schedulerTermId")
                << LOG_KV("termId", schedulerTermId) << LOG_KV("currentTermId", m_schedulerTermId);
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                "old executor has been stopped"));
            return;
        }

        try
        {
            refreshExecutor(schedulerTermId);
        }
        catch (Exception const& _e)
        {
            EXECUTOR_LOG(WARNING) << LOG_DESC("nextBlockHeader: not refreshExecutor for exception")
                                  << LOG_KV("toTermId", schedulerTermId)
                                  << LOG_KV("currentTermId", m_schedulerTermId)
                                  << diagnostic_information(_e);
            callback(BCOS_ERROR_UNIQUE_PTR(
                bcos::executor::ExecuteError::INTERNAL_ERROR, "refreshExecutor exception"));
            return;
        }

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
        if (hasStopped())
        {
            std::string message = "executeTransaction: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(
                BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message), nullptr);
            return;
        }

        if (!hasNextBlockHeaderDone())
        {
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                         "Executor has just inited, need to switch"),
                nullptr);
            return;
        }

        m_pool.enqueue(
            [executor = m_executor, inputRaw = input.release(), callback = std::move(callback)] {
                // create a holder
                auto _holdExecutorCallback =
                    [executorHolder = executor, callback = std::move(callback)](
                        bcos::Error::UniquePtr error,
                        bcos::protocol::ExecutionMessage::UniquePtr output) {
                        EXECUTOR_LOG(TRACE) << "Release executor holder"
                                            << LOG_KV("ptr count", executorHolder.use_count());
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
        if (hasStopped())
        {
            std::string message = "call: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(
                BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message), nullptr);
            return;
        }

        auto currentExecutor = getAndNewExecutorIfNotExists();

        // create a holder
        auto _holdExecutorCallback =
            [executorHolder = currentExecutor, callback = std::move(callback)](
                bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr output) {
                EXECUTOR_LOG(TRACE)
                    << "Release executor holder" << LOG_KV("ptr count", executorHolder.use_count());
                callback(std::move(error), std::move(output));
            };

        // execute the function
        currentExecutor->call(bcos::protocol::ExecutionMessage::UniquePtr(input.release()),
            std::move(_holdExecutorCallback));
    }

    void executeTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        if (hasStopped())
        {
            std::string message = "executeTransactions: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message), {});
            return;
        }

        // Note: copy the inputs here in case of inputs has been released
        if (!hasNextBlockHeaderDone())
        {
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                         "Executor has just inited, need to switch"),
                {});
            return;
        }

        auto inputsVec =
            std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();
        for (auto i = 0u; i < inputs.size(); i++)
        {
            inputsVec->emplace_back(std::move(inputs[i]));
        }
        auto queryTime = utcTime();
        m_pool.enqueue(
            [queryTime, executor = m_executor, contractAddress = std::move(contractAddress),
                inputsVec, callback = std::move(callback)] {
                auto waitInPoolCost = utcTime() - queryTime;
                // create a holder
                auto _holdExecutorCallback =
                    [queryTime, waitInPoolCost, executorHolder = executor,
                        callback = std::move(callback)](bcos::Error::UniquePtr error,
                        std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) {
                        EXECUTOR_LOG(TRACE) << "Release executor holder executeTransactions"
                                            << LOG_KV("ptr count", executorHolder.use_count())
                                            << LOG_KV("waitInPoolCost", waitInPoolCost)
                                            << LOG_KV("costFromQuery", utcTime() - queryTime);
                        callback(std::move(error), std::move(outputs));
                    };

                // execute the function
                executor->executeTransactions(
                    contractAddress, *inputsVec, std::move(_holdExecutorCallback));
            });
    }

    void preExecuteTransactions(int64_t schedulerTermId,
        const bcos::protocol::BlockHeader::ConstPtr& blockHeader, std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(bcos::Error::UniquePtr)> callback) override
    {
        if (hasStopped())
        {
            std::string message = "preExecuteTransactions: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message));
            return;
        }

        if (schedulerTermId < m_schedulerTermId)
        {
            // Call from an outdated scheduler instance
            // Just return without callback, because run callback may trigger a new switch, thus
            // some message will be outdated and trigger switch again and again.
            EXECUTOR_LOG(INFO)
                << LOG_DESC(
                       "preExecuteTransactions: not refreshExecutor for invalid schedulerTermId")
                << LOG_KV("termId", schedulerTermId) << LOG_KV("currentTermId", m_schedulerTermId);
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                "old executor has been stopped"));
            return;
        }

        try
        {
            refreshExecutor(schedulerTermId);
        }
        catch (Exception const& _e)
        {
            EXECUTOR_LOG(ERROR) << LOG_DESC(
                                       "preExecuteTransactions: not refreshExecutor for exception")
                                << LOG_KV("toTermId", schedulerTermId)
                                << LOG_KV("currentTermId", m_schedulerTermId)
                                << diagnostic_information(_e);
            callback(BCOS_ERROR_UNIQUE_PTR(
                bcos::executor::ExecuteError::INTERNAL_ERROR, "refreshExecutor exception"));
            return;
        }

        auto inputsVec =
            std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();
        for (auto i = 0u; i < inputs.size(); i++)
        {
            inputsVec->emplace_back(std::move(inputs[i]));
        }
        m_pool.enqueue(
            [executor = m_executor, schedulerTermId, blockHeader = std::move(blockHeader),
                contractAddress = std::move(contractAddress), inputsVec,
                callback = std::move(callback)] {
                // create a holder
                auto _holdExecutorCallback = [executorHolder = executor, callback =
                                                                             std::move(callback)](
                                                 bcos::Error::UniquePtr error) {
                    EXECUTOR_LOG(TRACE) << "Release executor holder"
                                        << LOG_KV("ptr count", executorHolder.use_count());
                    callback(std::move(error));
                };

                // execute the function
                executor->preExecuteTransactions(schedulerTermId, blockHeader, contractAddress,
                    *inputsVec, std::move(_holdExecutorCallback));
            });
    }

    void dmcExecuteTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        if (hasStopped())
        {
            std::string message = "dmcExecuteTransactions: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message), {});
            return;
        }

        if (!hasNextBlockHeaderDone())
        {
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                         "Executor has just inited, need to switch"),
                {});
            return;
        }

        // Note: copy the inputs here in case of inputs has been released
        auto inputsVec =
            std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();
        for (auto i = 0u; i < inputs.size(); i++)
        {
            inputsVec->emplace_back(std::move(inputs[i]));
        }
        m_pool.enqueue([executor = m_executor, contractAddress = std::move(contractAddress),
                           inputsVec, callback = std::move(callback)] {
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
        if (hasStopped())
        {
            std::string message = "dagExecuteTransactions: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message), {});
            return;
        }

        if (!hasNextBlockHeaderDone())
        {
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                         "Executor has just inited, need to switch"),
                {});
            return;
        }

        auto inputsVec =
            std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();
        for (auto i = 0u; i < inputs.size(); i++)
        {
            inputsVec->emplace_back(std::move(inputs[i]));
        }
        m_pool.enqueue([executor = m_executor, inputsVec, callback = std::move(callback)] {
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
        if (hasStopped())
        {
            std::string message = "dmcCall: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(
                BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message), nullptr);
            return;
        }

        auto currentExecutor = getAndNewExecutorIfNotExists();

        // create a holder
        auto _holdExecutorCallback =
            [executorHolder = currentExecutor, callback = std::move(callback)](
                bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr output) {
                EXECUTOR_LOG(TRACE)
                    << "Release executor holder" << LOG_KV("ptr count", executorHolder.use_count());
                callback(std::move(error), std::move(output));
            };

        // execute the function
        currentExecutor->dmcCall(bcos::protocol::ExecutionMessage::UniquePtr(input.release()),
            std::move(_holdExecutorCallback));
    }

    void getHash(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback) override
    {
        if (hasStopped())
        {
            std::string message = "getHash: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message), {});
            return;
        }

        auto currentExecutor = getAndNewExecutorIfNotExists();

        m_pool.enqueue([executor = currentExecutor, number, callback = std::move(callback)] {
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
        if (hasStopped())
        {
            std::string message = "prepare: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message));
            return;
        }

        if (!hasNextBlockHeaderDone())
        {
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                "Executor has just inited, need to switch"));
            return;
        }

        m_pool.enqueue([executor = m_executor, params = bcos::protocol::TwoPCParams(params),
                           callback = std::move(callback)] {
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
        if (hasStopped())
        {
            std::string message = "commit: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message));
            return;
        }

        if (!hasNextBlockHeaderDone())
        {
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                "Executor has just inited, need to switch"));
            return;
        }

        m_pool.enqueue([executor = m_executor, params = bcos::protocol::TwoPCParams(params),
                           callback = std::move(callback)] {
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
        if (hasStopped())
        {
            std::string message = "rollback: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message));
            return;
        }

        if (!hasNextBlockHeaderDone())
        {
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR,
                "Executor has just inited, need to switch"));
            return;
        }

        m_pool.enqueue([executor = m_executor, params = bcos::protocol::TwoPCParams(params),
                           callback = std::move(callback)] {
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
        if (hasStopped())
        {
            std::string message = "reset: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message));
            return;
        }

        auto currentExecutor = getAndNewExecutorIfNotExists();

        m_pool.enqueue([executor = currentExecutor, callback = std::move(callback)] {
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
        if (hasStopped())
        {
            std::string message = "getCode: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message), {});
            return;
        }

        auto currentExecutor = getAndNewExecutorIfNotExists();

        m_pool.enqueue([executor = currentExecutor, contract = std::string(contract),
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
        if (hasStopped())
        {
            std::string message = "getABI: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(bcos::executor::ExecuteError::STOPPED, message), {});
            return;
        }

        auto currentExecutor = getAndNewExecutorIfNotExists();

        m_pool.enqueue([executor = currentExecutor, contract = std::string(contract),
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

    void updateEoaNonce(std::unordered_map<std::string, u256> const& nonceMap) override
    {
        if (hasStopped())
        {
            std::string message = "updateEoaNonce: executor has been stopped";
            EXECUTOR_LOG(DEBUG) << message;
            return;
        }

        getAndNewExecutorIfNotExists()->updateEoaNonce(std::move(nonceMap));
    }

    void stop() override
    {
        EXECUTOR_LOG(INFO) << "Try to stop SwitchExecutorManager";
        m_schedulerTermId = STOPPED_TERM_ID;

        auto executorUseCount = 0;
        bcos::executor::TransactionExecutor::Ptr executor = getCurrentExecutor();
        m_executor = nullptr;

        if (executor)
        {
            executor->stop();
        }
        executorUseCount = executor.use_count();

        // waiting for stopped
        while (executorUseCount > 1)
        {
            if (executor != nullptr)
            {
                executorUseCount = executor.use_count();
            }
            EXECUTOR_LOG(DEBUG) << "Executor is stopping.. "
                                << LOG_KV("unfinishedTaskNum", executorUseCount - 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        EXECUTOR_LOG(INFO) << "Executor has stopped.";
    }

    bcos::executor::TransactionExecutor::Ptr getCurrentExecutor() { return m_executor; }

    bcos::executor::TransactionExecutor::Ptr getAndNewExecutorIfNotExists()
    {
        if (!m_executor)
        {
            refreshExecutor(INIT_SCHEDULER_TERM_ID + 1);
        }

        auto executor = m_executor;
        return executor;
    }

private:
    bcos::ThreadPool m_pool;
    bcos::executor::TransactionExecutor::Ptr m_executor;
    int64_t m_schedulerTermId = INIT_SCHEDULER_TERM_ID;

    mutable bcos::SharedMutex m_mutex;

    bcos::executor::TransactionExecutorFactory::Ptr m_factory;
};
}  // namespace bcos::executor