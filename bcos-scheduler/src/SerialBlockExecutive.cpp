#include "SerialBlockExecutive.h"
#include "DmcExecutor.h"
#include "SchedulerImpl.h"
#include "bcos-crypto/bcos-crypto/ChecksumAddress.h"
#include "bcos-framework/executor/ExecuteError.h"
#include "bcos-framework/executor/ExecutionMessage.h"


using namespace bcos::scheduler;


void SerialBlockExecutive::prepare()
{
    {
        if (m_hasPrepared)
        {
            return;
        }
        WriteGuard lock(x_prepareLock);
        if (m_hasPrepared)
        {
            return;
        }

        uint64_t txSize = 0;
        if (m_block->transactionsMetaDataSize() > 0)
        {
            txSize = m_block->transactionsMetaDataSize();
        }
        else if (m_block->transactionsSize() > 0)
        {
            txSize = m_block->transactionsSize();
        }
        else
        {
            SCHEDULER_LOG(DEBUG) << "BlockExecutive prepare: empty block"
                                 << LOG_KV("block number", m_block->blockHeaderConst()->number());
        }
        m_transactions.resize(txSize);

        if (m_executor == nullptr)
        {
            m_executor = m_scheduler->executorManager()->dispatchExecutor(SERIAL_EXECUTOR_NAME);
            m_executorInfo = m_scheduler->executorManager()->getExecutorInfo(SERIAL_EXECUTOR_NAME);
        }
    }

    BlockExecutive::prepare();
}

void SerialBlockExecutive::saveMessage(
    std::string, protocol::ExecutionMessage::UniquePtr message, bool)
{
    auto idx = message->contextID() - m_startContextID;

    if (m_transactions.size() <= idx)
    {
        m_transactions.resize(idx + 1);
    }
    m_transactions[idx] = std::move(message);
}

void SerialBlockExecutive::asyncExecute(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    SERIAL_EXECUTE_LOG(INFO) << BLOCK_NUMBER(number()) << LOG_DESC("serialExecute execute block");
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
        SERIAL_EXECUTE_LOG(INFO) << BLOCK_NUMBER(number())
                                 << LOG_DESC("serialExecute batch next block");
        // Execute nextBlock
        batchNextBlock([this, startT, createMsgT, callback = std::move(callback)](
                           Error::UniquePtr error) {
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

            SERIAL_EXECUTE_LOG(INFO)
                << BLOCK_NUMBER(number()) << LOG_DESC("serialExecute block begin");

            serialExecute([this, createMsgT, startT, callback = std::move(callback)](
                              bcos::Error::UniquePtr error, protocol::BlockHeader::Ptr header,
                              bool isSysBlock) {
                if (error)
                {
                    SERIAL_EXECUTE_LOG(INFO)
                        << BLOCK_NUMBER(number()) << LOG_DESC("serialExecute block failed")
                        << LOG_KV("createMsgT", createMsgT)
                        << LOG_KV("executeT", (utcTime() - startT))
                        << LOG_KV("errorCode", error->errorCode())
                        << LOG_KV("errorMessage", error->errorMessage());
                    callback(std::move(error), nullptr, isSysBlock);
                    return;
                }

                SERIAL_EXECUTE_LOG(INFO)
                    << BLOCK_NUMBER(number()) << LOG_DESC("serialExecute success")
                    << LOG_KV("createMsgT", createMsgT) << LOG_KV("executeT", (utcTime() - startT))
                    << LOG_KV("hash", header->hash().abridged());
                callback(nullptr, header, isSysBlock);
            });
        });
    }
    else
    {
        serialExecute(std::move(callback));
    }
}

void SerialBlockExecutive::serialExecute(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    SERIAL_EXECUTE_LOG(INFO) << "Send Transaction size " << m_transactions.size();
    // handle create message, to generate address
    for (auto& tx : m_transactions)
    {
        if (tx->to().empty())
        {
            auto newSeq = tx->seq();
            if (tx->createSalt())
            {
                // TODO: Add sender in this process(consider compat with ethereum)
                tx->setTo(bcos::newEVMAddress(
                    m_scheduler->getHashImpl(), tx->from(), tx->data(), *(tx->createSalt())));
            }
            else
            {
                // TODO: Add sender in this process(consider compat with ethereum)
                tx->setTo(bcos::newEVMAddress(m_scheduler->getHashImpl(),
                    m_block->blockHeaderConst()->number(), tx->contextID(), newSeq));
            }
        }

        SERIAL_EXECUTE_LOG(DEBUG) << BLOCK_NUMBER(number()) << "0.Send:\t >>>> ["
                                  << m_executorInfo->name << "]: " << tx->toString();
    }

    // send one tx
    if (m_staticCall)
    {
        if (m_transactions.size() != 1)
        {
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::CallError,
                         "The block of call should only has 1 request, but has " +
                             std::to_string(m_transactions.size())),
                nullptr, m_syncBlock);
            return;
        }
        m_executor->call(std::move(std::move(m_transactions[0])),
            [this, callback = std::move(callback)](
                bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr output) {
                if (error)
                {
                    SERIAL_EXECUTE_LOG(DEBUG)
                        << BLOCK_NUMBER(number())
                        << "serialExecute:\t Error: " << error->errorMessage();
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 SchedulerError::SerialExecuteError, error->errorMessage()),
                        nullptr, m_syncBlock);

                    if (error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                    {
                        triggerSwitch();
                    }
                    return;
                }

                onTxFinish(std::move(output));
                onExecuteFinish(std::move(callback));
            });
    }
    else
    {
        m_executor->executeTransactions(bcos::protocol::SERIAL_EXECUTIVE_FLOW_ADDRESS,
            m_transactions,
            [this, callback = std::move(callback)](bcos::Error::UniquePtr error,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) {
                if (error)
                {
                    SERIAL_EXECUTE_LOG(DEBUG)
                        << BLOCK_NUMBER(number())
                        << "serialExecute:\t Error: " << error->errorMessage();
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 SchedulerError::SerialExecuteError, error->errorMessage()),
                        nullptr, m_syncBlock);

                    if (error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                    {
                        triggerSwitch();
                    }
                    return;
                }

                for (auto& output : outputs)
                {
                    SERIAL_EXECUTE_LOG(DEBUG)
                        << BLOCK_NUMBER(number()) << "1.Receive:\t <<<< [" << m_executorInfo->name
                        << "]: " << output->toString();
                    onTxFinish(std::move(output));
                }
                onExecuteFinish(std::move(callback));
            });
    }
}

void SerialBlockExecutive::onExecuteFinish(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    if (!m_staticCall)
    {
        SERIAL_EXECUTE_LOG(DEBUG) << BLOCK_NUMBER(number()) << "2.Receipt:\t [^^]"
                                  << LOG_KV("receiptsSize", m_executiveResults.size())
                                  << LOG_KV("blockNumber", number());
    }
    BlockExecutive::onExecuteFinish(std::move(callback));
}
