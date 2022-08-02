#include "SerialDmcBlockExecutive.h"
#include "DmcExecutor.h"
#include "SchedulerImpl.h"
#include "bcos-crypto/bcos-crypto/ChecksumAddress.h"
#include "bcos-framework/executor/ExecuteError.h"

using namespace bcos::scheduler;


void SerialDmcBlockExecutive::prepare()
{
    BlockExecutive::prepare();

    // dump executive states from DmcExecutor
    WriteGuard lock(x_prepareLock);
    if (m_serialExecutiveStates.empty())
    {
        m_serialExecutiveStates.resize(m_executiveResults.size());

        for (auto it : m_dmcExecutors)
        {
            auto dmcExecutor = it.second;
            dmcExecutor->forEachExecutive(
                [this](ContextID contextID, ExecutiveState::Ptr executiveState) {
                    m_serialExecutiveStates[contextID - m_startContextID] = executiveState;
                    executiveState->callStack = std::stack<int64_t, std::list<int64_t>>();
                    executiveState->currentSeq = 0;
                    executiveState->message->setSeq(0);
                });
        }
    }

    if (m_executor == nullptr)
    {
        m_executor = m_scheduler->executorManager()->dispatchExecutor(SERIAL_EXECUTOR_NAME);
        m_executorInfo = m_scheduler->executorManager()->getExecutorInfo(SERIAL_EXECUTOR_NAME);
    }
}
void SerialDmcBlockExecutive::asyncExecute(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    if (m_result)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, "Invalid status"), nullptr,
            m_isSysBlock);
        return;
    }

    if (m_scheduler->executorManager()->size() == 0)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(
                     SchedulerError::ExecutorNotEstablishedError, "The executor has not started!"),
            nullptr, m_isSysBlock);
    }
    m_currentTimePoint = std::chrono::system_clock::now();

    auto startT = utcTime();
    prepare();

    auto createMsgT = utcTime() - startT;
    startT = utcTime();

    if (!m_staticCall)
    {
        // Execute nextBlock
        batchNextBlock(
            [this, startT, createMsgT, callback = std::move(callback)](Error::UniquePtr error) {
                if (!m_isRunning)
                {
                    callback(
                        BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"),
                        nullptr, m_isSysBlock);
                    return;
                }

                if (error)
                {
                    SERIAL_EXECUTE_LOG(ERROR) << "Next block with error!" << error->errorMessage();
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                 SchedulerError::NextBlockError, "Next block error!", *error),
                        nullptr, m_isSysBlock);

                    if (error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                    {
                        triggerSwitch();
                    }
                    return;
                }

                serialExecute([this, createMsgT, startT, callback = std::move(callback)](
                                  bcos::Error::UniquePtr error, protocol::BlockHeader::Ptr header,
                                  bool isSysBlock) {
                    if (error)
                    {
                        SERIAL_EXECUTE_LOG(INFO)
                            << LOG_DESC("serialExecute failed") << LOG_KV("createMsgT", createMsgT)
                            << LOG_KV("executeT", (utcTime() - startT))
                            << LOG_KV("number", number()) << LOG_KV("errorCode", error->errorCode())
                            << LOG_KV("errorMessage", error->errorMessage());
                        callback(std::move(error), nullptr, isSysBlock);
                        return;
                    }

                    SERIAL_EXECUTE_LOG(INFO)
                        << LOG_DESC("serialExecute success") << LOG_KV("createMsgT", createMsgT)
                        << LOG_KV("executeT", (utcTime() - startT))
                        << LOG_KV("hash", header->hash().abridged())
                        << LOG_KV("number", header->number());
                    callback(nullptr, header, isSysBlock);
                });
            });
    }
    else
    {
        serialExecute(std::move(callback));
    }
}

void SerialDmcBlockExecutive::serialExecute(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    recursiveExecuteTx(
        m_startContextID, [this, callback = std::move(callback)](bcos::Error::UniquePtr error) {
            if (error)
            {
                callback(std::move(error), nullptr, m_syncBlock);
                return;
            }

            onExecuteFinish(std::move(callback));
        });
}

void SerialDmcBlockExecutive::recursiveExecuteTx(
    ContextID contextID, std::function<void(bcos::Error::UniquePtr)> callback)
{
    if (((size_t)contextID - m_startContextID) >= m_serialExecutiveStates.size())
    {
        callback(nullptr);  // execute finish
        return;
    }


    auto executiveState = m_serialExecutiveStates[contextID - m_startContextID];

    if (!executiveState)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlocks,
            "Transaction not found of contextID: " + std::to_string(contextID) + " in block " +
                std::to_string(number())));
        return;
    }

    if (!executiveState->message)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlocks,
            "ExecutionMessage not found of contextID: " + std::to_string(contextID) + " in block " +
                std::to_string(number())));
        return;
    }

    // handle message type
    auto& message = executiveState->message;
    switch (message->type())
    {
        // Request type, push stack
    case protocol::ExecutionMessage::MESSAGE:
    case protocol::ExecutionMessage::TXHASH:
    case protocol::ExecutionMessage::KEY_LOCK:
    case protocol::ExecutionMessage::SEND_BACK:
    {
        // handle create message
        if (message->to().empty())
        {
            auto newSeq = executiveState->currentSeq;
            if (message->createSalt())
            {
                // TODO: Add sender in this process(consider compat with ethereum)
                message->setTo(bcos::newEVMAddress(m_scheduler->getHashImpl(), message->from(),
                    message->data(), *(message->createSalt())));
            }
            else
            {
                // TODO: Add sender in this process(consider compat with ethereum)
                message->setTo(bcos::newEVMAddress(m_scheduler->getHashImpl(),
                    m_block->blockHeaderConst()->number(), contextID, newSeq));
            }
        }

        // handle normal message
        auto newSeq = executiveState->currentSeq++;
        executiveState->callStack.push(newSeq);
        executiveState->message->setSeq(newSeq);
        break;
    }
        // Return type, pop stack
    case protocol::ExecutionMessage::FINISHED:
    case protocol::ExecutionMessage::REVERT:
    {
        executiveState->callStack.pop();
        if (executiveState->callStack.empty())
        {
            // Empty stack, execution is finished
            onTxFinish(std::move(message));
            recursiveExecuteTx(contextID + 1, std::move(callback));  // execute next tx
            return;
        }
        else
        {
            message->setSeq(executiveState->callStack.top());
            message->setCreate(false);
        }
        break;
    }
    default:
    {
        SERIAL_EXECUTE_LOG(FATAL) << "Unrecognized message type: " << message->toString();
        assert(false);
        break;
    }
    }

    auto handleExecutorResponse = [this, contextID, executiveState, callback = std::move(callback)](
                                      bcos::Error::UniquePtr error,
                                      bcos::protocol::ExecutionMessage::UniquePtr output) {
        // handle error
        if (error)
        {
            SERIAL_EXECUTE_LOG(DEBUG)
                << BLOCK_NUMBER(number()) << "serialExecute:\t Error: " << error->errorMessage();
            callback(
                BCOS_ERROR_UNIQUE_PTR(SchedulerError::SerialExecuteError, error->errorMessage()));

            if (error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
            {
                triggerSwitch();
            }
            return;
        }

        if (!output)
        {
            SERIAL_EXECUTE_LOG(ERROR)
                << "Execute success but output is null" << LOG_KV("contextID", contextID);
            callback(
                BCOS_ERROR_UNIQUE_PTR(SchedulerError::SerialExecuteError, error->errorMessage()));
            return;
        }

        SERIAL_EXECUTE_LOG(DEBUG) << BLOCK_NUMBER(number()) << "1.Receive:\t <<<< ["
                                  << m_executorInfo->name << "]: " << output->toString();
        // clear keylock
        output->setKeyLocks({});
        output->setKeyLockAcquired({});
        executiveState->message = std::move(output);
        recursiveExecuteTx(contextID, std::move(callback));
    };

    // send one tx
    SERIAL_EXECUTE_LOG(DEBUG) << BLOCK_NUMBER(number()) << "0.Send:\t >>>> ["
                              << m_executorInfo->name << "]: " << message->toString();
    if (m_staticCall)
    {
        if (m_serialExecutiveStates.size() != 1)
        {
            callback(BCOS_ERROR_UNIQUE_PTR(
                SchedulerError::CallError, "The block of call should only has 1 request, but has " +
                                               std::to_string(m_serialExecutiveStates.size())));
            return;
        }
        m_executor->call(std::move(message), std::move(handleExecutorResponse));
    }
    else
    {
        m_executor->executeTransaction(std::move(message), std::move(handleExecutorResponse));
    }
}

void SerialDmcBlockExecutive::onExecuteFinish(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    if (!m_staticCall)
    {
        SERIAL_EXECUTE_LOG(DEBUG) << BLOCK_NUMBER(number()) << "2.Receipt:\t [^^]"
                                  << LOG_KV("receiptsSize", m_executiveResults.size());
    }
    BlockExecutive::onExecuteFinish(std::move(callback));
}
