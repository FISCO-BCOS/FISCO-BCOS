#include "ShardingDmcExecutor.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <tbb/parallel_for.h>

using namespace bcos::scheduler;

ShardingDmcExecutor::ShardingDmcExecutor(std::string name, std::string contractAddress,
    bcos::protocol::Block::Ptr block,
    bcos::executor::ParallelTransactionExecutorInterface::Ptr executor, GraphKeyLocks::Ptr keyLocks,
    bcos::crypto::Hash::Ptr hashImpl, DmcStepRecorder::Ptr dmcRecorder,
    int64_t schedulerTermId, bool isCall)
  : DmcExecutor(std::move(name), std::move(contractAddress), std::move(block),
        std::move(executor), std::move(keyLocks), std::move(hashImpl), std::move(dmcRecorder),
        isCall),
    m_schedulerTermId(schedulerTermId)
{}

ShardingDmcExecutor::~ShardingDmcExecutor() = default;

void ShardingDmcExecutor::submit(protocol::ExecutionMessage::UniquePtr message, bool withDAG)
{
    (void)withDAG;  // no need to use this param
    handleCreateMessage(message, 0);
    m_preparedMessages->emplace_back(std::move(message));
}

void ShardingDmcExecutor::shardGo(std::function<void(bcos::Error::UniquePtr, Status)> callback)
{
    std::shared_ptr<std::vector<protocol::ExecutionMessage::UniquePtr>> messages;
    {
        // preExecute has already completed at this point (guaranteed by caller),
        // so this WriteGuard does not block.
        auto preExecuteGuard = bcos::WriteGuard(x_preExecute);
        messages = std::move(m_preparedMessages);
    }

    if (messages && messages->size() == 1 && (*messages)[0]->staticCall())
    {
        DMC_LOG(TRACE) << "send call request, address:" << m_contractAddress
                       << LOG_KV("executor", m_name) << LOG_KV("to", (*messages)[0]->to())
                       << LOG_KV("contextID", (*messages)[0]->contextID())
                       << LOG_KV("internalCall", (*messages)[0]->internalCall())
                       << LOG_KV("type", (*messages)[0]->type());
        // is static call
        executorCall(std::move((*messages)[0]),
            [this, callback = std::move(callback)](
                bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr output) {
                if (error)
                {
                    SCHEDULER_LOG(ERROR) << "Call error: " << boost::diagnostic_information(*error);

                    if (error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                    {
                        triggerSwitch();
                    }
                    callback(std::move(error), ERROR);
                }
                else
                {
                    f_onTxFinished(std::move(output));
                    callback(nullptr, PAUSED);
                }
            });
    }
    else
    {
        auto lastT = utcTime();
        if (!messages)
        {
            DMC_LOG(DEBUG) << LOG_BADGE("Stat")
                           << "ShardingExecute:\t --> Send to executor by preExecute cache\t"
                           << LOG_KV("name", m_name) << LOG_KV("shard", m_contractAddress)
                           << LOG_KV("txNum", messages ? messages->size() : 0)
                           << LOG_KV("blockNumber", m_block && m_block->blockHeader() ?
                                                        m_block->blockHeader()->number() :
                                                        0)
                           << LOG_KV("cost", utcTime() - lastT);
            messages = std::make_shared<std::vector<protocol::ExecutionMessage::UniquePtr>>();
        }
        else
        {
            DMC_LOG(DEBUG) << LOG_BADGE("Stat") << "ShardingExecute:\t --> Send to executor\t"
                           << LOG_KV("name", m_name) << LOG_KV("shard", m_contractAddress)
                           << LOG_KV("txNum", messages ? messages->size() : 0)
                           << LOG_KV("blockNumber", m_block && m_block->blockHeader() ?
                                                        m_block->blockHeader()->number() :
                                                        0)
                           << LOG_KV("cost", utcTime() - lastT);
        }

        auto self = shared_from_this();
        executorExecuteTransactions(m_contractAddress, *messages,
            [this, self, lastT, messages, callback = std::move(callback)](
                bcos::Error::UniquePtr error,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) {
                // update batch
                DMC_LOG(DEBUG) << LOG_BADGE("Stat")
                               << "ShardingExecute:\t <-- Receive from executor\t"
                               << LOG_KV("name", m_name) << LOG_KV("shard", m_contractAddress)
                               << LOG_KV("txNum", messages ? messages->size() : 0)
                               << LOG_KV("blockNumber", m_block && m_block->blockHeader() ?
                                                            m_block->blockHeader()->number() :
                                                            0)
                               << LOG_KV("cost", utcTime() - lastT);

                if (error)
                {
                    SCHEDULER_LOG(ERROR)
                        << "ShardingExecute transaction error: " << error->errorMessage();

                    if (error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                    {
                        triggerSwitch();
                    }

                    callback(std::move(error), Status::ERROR);
                }
                else
                {
                    handleShardGoOutput(std::move(outputs));
                    callback(nullptr, Status::FINISHED);
                }
            });
    }
}

void ShardingDmcExecutor::handleShardGoOutput(
    std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs)
{
    std::vector<bcos::protocol::ExecutionMessage::UniquePtr> dmcMessages;
    // filter DMC messages and return not DMC messages directly
    for (auto& output : outputs)
    {
        if (output->hasContractTableChanged()) [[unlikely]]
        {
            m_hasContractTableChanged = true;
        }

        if (output->type() == protocol::ExecutionMessage::FINISHED ||
            output->type() == protocol::ExecutionMessage::REVERT) [[likely]]
        {
            f_onTxFinished(std::move(output));
        }
        else
        {
            dmcMessages.emplace_back(std::move(output));
        }
    }
    DMC_LOG(DEBUG) << LOG_BADGE("Stat") << "ShardingExecute: dump output finish";

    // going to dmc logic
    handleExecutiveOutputs(std::move(dmcMessages));
}

void ShardingDmcExecutor::handleExecutiveOutputs(
    std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs)
{
    // create executiveState
    for (auto& dmcOutput : outputs)
    {
        auto contextID = dmcOutput->contextID();
        auto executiveState = m_executivePool.get(contextID);
        if (!executiveState)
        {
            executiveState = std::make_shared<ExecutiveState>(contextID, nullptr, false);
            auto newSeq = executiveState->currentSeq++;
            executiveState->callStack.push(newSeq);
            dmcOutput->setSeq(newSeq);
            m_executivePool.add(contextID, executiveState);
        }
    }

    // going to dmc logic
    DmcExecutor::handleExecutiveOutputs(std::move(outputs));
}

void ShardingDmcExecutor::executorCall(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    m_executor->call(std::move(input), std::move(callback));
}

void ShardingDmcExecutor::executorExecuteTransactions(std::string contractAddress,
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,

    // called every time at all tx stop( pause or finish)
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    m_executor->executeTransactions(std::move(contractAddress), inputs, std::move(callback));
}


void ShardingDmcExecutor::preExecute()
{
    auto preExecuteGuard = std::make_shared<bcos::WriteGuard>(x_preExecute);
    auto message = std::move(m_preparedMessages);
    if (!message || message->size() == 0)
    {
        m_preExecuteFuture = {};
        m_preExecuteCompleted.store(true);
        return;
    }
    DMC_LOG(DEBUG) << LOG_BADGE("BlockTrace") << LOG_BADGE("Sharding") << "send preExecute message"
                   << LOG_KV("name", m_name) << LOG_KV("contract", m_contractAddress)
                   << LOG_KV("txNum", message->size())
                   << LOG_KV("blockNumber", m_block->blockHeader()->number())
                   << LOG_KV("timestamp", m_block->blockHeader()->timestamp());

    m_preExecuteCompleted.store(false);
    auto promise = std::make_shared<std::promise<void>>();
    m_preExecuteFuture = promise->get_future().share();

    m_executor->preExecuteTransactions(m_schedulerTermId, m_block->blockHeader(),
        m_contractAddress, *message,
        [this, preExecuteGuard = std::move(preExecuteGuard),
            promise = std::move(promise)](bcos::Error::UniquePtr error) mutable {
            if (error)
            {
                DMC_LOG(ERROR)
                    << LOG_BADGE("BlockTrace") << LOG_BADGE("Sharding")
                    << "send preExecute message error:" << error->errorMessage()
                    << LOG_KV("name", m_name) << LOG_KV("contract", m_contractAddress)
                    << LOG_KV("blockNumber", m_block->blockHeader()->number())
                    << LOG_KV("timestamp", m_block->blockHeader()->timestamp());
            }
            else
            {
                DMC_LOG(DEBUG)
                    << LOG_BADGE("BlockTrace") << LOG_BADGE("Sharding")
                    << "send preExecute message success " << LOG_KV("name", m_name)
                    << LOG_KV("contract", m_contractAddress)
                    << LOG_KV("blockNumber", m_block->blockHeader()->number())
                    << LOG_KV("timestamp", m_block->blockHeader()->timestamp());
            }
            promise->set_value();

            // Release the WriteGuard BEFORE firing the completion callback,
            // because the callback (shardGo) needs to acquire x_preExecute
            // and WriteGuard is not re-entrant.
            preExecuteGuard.reset();

            // Atomically mark completion and consume the registered callback
            // under the mutex, so that onPreExecuteComplete sees a consistent
            // state: either the flag is false (callback stored, we consume it)
            // or the flag is true (callback invoked directly by onPreExecuteComplete).
            std::function<void()> onComplete;
            {
                std::lock_guard<std::mutex> lock(m_onPreExecuteCompleteMutex);
                m_preExecuteCompleted.store(true);
                onComplete = std::move(m_onPreExecuteComplete);
            }
            if (onComplete)
            {
                onComplete();
            }
        });
    // Returns immediately without blocking the calling thread.
    // The x_preExecute WriteGuard is held by the lambda until preExecuteTransactions completes.
}

void ShardingDmcExecutor::onPreExecuteComplete(std::function<void()> callback)
{
    {
        std::lock_guard<std::mutex> lock(m_onPreExecuteCompleteMutex);
        if (!m_preExecuteCompleted.load())
        {
            // preExecute is still in progress, store the callback
            m_onPreExecuteComplete = std::move(callback);
            return;
        }
    }
    // preExecute already completed (or never started), invoke immediately
    callback();
}
