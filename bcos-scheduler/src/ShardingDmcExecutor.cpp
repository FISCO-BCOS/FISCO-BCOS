#include "ShardingDmcExecutor.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <tbb/parallel_for.h>

using namespace bcos::scheduler;

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
        // NOTICE: waiting for preExecute finish
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
        return;
    }
    DMC_LOG(DEBUG) << LOG_BADGE("BlockTrace") << LOG_BADGE("Sharding") << "send preExecute message"
                   << LOG_KV("name", m_name) << LOG_KV("contract", m_contractAddress)
                   << LOG_KV("txNum", message->size())
                   << LOG_KV("blockNumber", m_block->blockHeader()->number())
                   << LOG_KV("timestamp", m_block->blockHeader()->timestamp());

    std::shared_ptr<std::promise<bcos::Error::UniquePtr>> preExePromise =
        std::make_shared<std::promise<bcos::Error::UniquePtr>>();
    m_executor->preExecuteTransactions(m_schedulerTermId, m_block->blockHeaderConst(),
        m_contractAddress, *message, [preExePromise](bcos::Error::UniquePtr error) {
            preExePromise->set_value(std::move(error));
        });

    auto future = preExePromise->get_future();
    auto status = future.wait_for(std::chrono::seconds(30));
    if (status != std::future_status::ready)
    {
        std::string reason;
        switch (status)
        {
        case std::future_status::deferred:
            reason = "deferred\n";
            break;
        case std::future_status::timeout:
            reason = "timeout\n";
            break;
        case std::future_status::ready:
            reason = "ready!\n";
            break;
        }
        DMC_LOG(ERROR) << LOG_BADGE("BlockTrace") << LOG_BADGE("Sharding")
                       << "send preExecute message error:" << LOG_KV("reason", reason)
                       << LOG_KV("contract", m_contractAddress)
                       << LOG_KV("blockNumber", m_block->blockHeader()->number())
                       << LOG_KV("timestamp", m_block->blockHeader()->timestamp());
        return;
    }

    auto error = future.get();
    if (error)
    {
        DMC_LOG(ERROR) << LOG_BADGE("BlockTrace") << LOG_BADGE("Sharding")
                       << "send preExecute message error:" << error->errorMessage()
                       << LOG_KV("name", m_name) << LOG_KV("contract", m_contractAddress)
                       << LOG_KV("blockNumber", m_block->blockHeader()->number())
                       << LOG_KV("timestamp", m_block->blockHeader()->timestamp());
    }
    else
    {
        DMC_LOG(DEBUG) << LOG_BADGE("BlockTrace") << LOG_BADGE("Sharding")
                       << "send preExecute message success " << LOG_KV("name", m_name)
                       << LOG_KV("contract", m_contractAddress)
                       << LOG_KV("blockNumber", m_block->blockHeader()->number())
                       << LOG_KV("timestamp", m_block->blockHeader()->timestamp());
    }
}