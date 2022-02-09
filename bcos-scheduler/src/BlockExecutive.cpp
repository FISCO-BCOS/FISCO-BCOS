#include "BlockExecutive.h"
#include "ChecksumAddress.h"
#include "Common.h"
#include "SchedulerImpl.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/executor/NativeExecutionMessage.h"
#include "bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/interfaces/executor/PrecompiledTypeDef.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-table/src/StateStorage.h"
#include <bcos-utilities/Error.h>
#include <tbb/parallel_for_each.h>
#include <boost/algorithm/hex.hpp>
#include <boost/asio/defer.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/latch.hpp>
#include <boost/thread/lock_options.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <thread>
#include <utility>

using namespace bcos::scheduler;
using namespace bcos::ledger;
void BlockExecutive::asyncCall(
    std::function<void(Error::UniquePtr&&, protocol::TransactionReceipt::Ptr&&)> callback)
{
    auto self = std::weak_ptr<BlockExecutive>(shared_from_this());
    asyncExecute([self, callback](Error::UniquePtr&& _error, protocol::BlockHeader::Ptr) {
        auto executive = self.lock();
        if (!executive)
        {
            callback(
                BCOS_ERROR_UNIQUE_PTR(SchedulerError::UnknownError, "get block executive failed"),
                nullptr);
            return;
        }
        auto receipt =
            std::const_pointer_cast<protocol::TransactionReceipt>(executive->block()->receipt(0));
        callback(std::move(_error), std::move(receipt));
    });
}

void BlockExecutive::asyncExecute(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr)> callback)
{
    if (m_result)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, "Invalid status"), nullptr);
        return;
    }

    m_currentTimePoint = std::chrono::system_clock::now();

    bool withDAG = false;
    if (m_block->transactionsMetaDataSize() > 0)
    {
        SCHEDULER_LOG(DEBUG) << LOG_KV("block number", m_block->blockHeaderConst()->number())
                             << LOG_KV("meta tx count", m_block->transactionsMetaDataSize());

        m_executiveResults.resize(m_block->transactionsMetaDataSize());
        for (size_t i = 0; i < m_block->transactionsMetaDataSize(); ++i)
        {
            auto metaData = m_block->transactionMetaData(i);

            auto message = m_scheduler->m_executionMessageFactory->createExecutionMessage();
            message->setContextID(i + m_startContextID);
            message->setType(protocol::ExecutionMessage::TXHASH);
            message->setTransactionHash(metaData->hash());

            if (metaData->attribute() & bcos::protocol::Transaction::Attribute::LIQUID_SCALE_CODEC)
            {
                // LIQUID
                if (metaData->attribute() & bcos::protocol::Transaction::Attribute::LIQUID_CREATE)
                {
                    message->setCreate(true);
                }
                message->setTo(std::string(metaData->to()));
            }
            else
            {
                // SOLIDITY
                if (metaData->to().empty())
                {
                    message->setCreate(true);
                }
                else
                {
                    message->setTo(preprocessAddress(metaData->to()));
                }
            }

            message->setDepth(0);
            message->setGasAvailable(TRANSACTION_GAS);
            message->setStaticCall(false);

            if (metaData->attribute() & bcos::protocol::Transaction::Attribute::DAG)
            {
                withDAG = true;
            }

            auto to = message->to();
            m_executiveStates.emplace(
                std::make_tuple(std::move(to), i), ExecutiveState(i, std::move(message), withDAG));

            if (metaData)
            {
                m_executiveResults[i].transactionHash = metaData->hash();
                m_executiveResults[i].source = metaData->source();
            }
        }
    }
    else if (m_block->transactionsSize() > 0)
    {
        SCHEDULER_LOG(DEBUG) << LOG_KV("block number", m_block->blockHeaderConst()->number())
                             << LOG_KV("tx count", m_block->transactionsSize());

        m_executiveResults.resize(m_block->transactionsSize());
        for (size_t i = 0; i < m_block->transactionsSize(); ++i)
        {
            auto tx = m_block->transaction(i);
            m_executiveResults[i].transactionHash = tx->hash();
            m_executiveResults[i].source = tx->source();

            auto message = m_scheduler->m_executionMessageFactory->createExecutionMessage();
            message->setType(protocol::ExecutionMessage::MESSAGE);
            message->setContextID(i + m_startContextID);

            message->setOrigin(toHex(tx->sender()));
            message->setFrom(std::string(message->origin()));


            if (tx->attribute() & bcos::protocol::Transaction::Attribute::LIQUID_SCALE_CODEC)
            {
                // LIQUID
                if (tx->attribute() & bcos::protocol::Transaction::Attribute::LIQUID_CREATE)
                {
                    message->setCreate(true);
                }
                message->setTo(std::string(tx->to()));
            }
            else
            {
                // SOLIDITY
                if (tx->to().empty())
                {
                    message->setCreate(true);
                }
                else
                {
                    if (m_scheduler->m_isAuthCheck && !m_staticCall &&
                        m_block->blockHeaderConst()->number() == 0 &&
                        tx->to() == precompiled::AUTH_COMMITTEE_ADDRESS)
                    {
                        // if enable auth check, and first deploy auth contract
                        message->setCreate(true);
                    }
                    message->setTo(preprocessAddress(tx->to()));
                }
            }

            message->setDepth(0);
            message->setGasAvailable(TRANSACTION_GAS);
            message->setData(tx->input().toBytes());
            message->setStaticCall(m_staticCall);

            if (tx->attribute() & bcos::protocol::Transaction::Attribute::DAG)
            {
                withDAG = true;
            }

            auto to = std::string(message->to());
            m_executiveStates.emplace(
                std::make_tuple(std::move(to), i), ExecutiveState(i, std::move(message), withDAG));
        }
    }

    if (!m_staticCall)
    {
        // Execute nextBlock
        batchNextBlock([this, withDAG, callback = std::move(callback)](Error::UniquePtr error) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Next block with error!" << boost::diagnostic_information(*error);
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             SchedulerError::NextBlockError, "Next block error!", *error),
                    nullptr);
                return;
            }

            if (withDAG)
            {
                DAGExecute([this, callback = std::move(callback)](Error::UniquePtr error) {
                    if (error)
                    {
                        SCHEDULER_LOG(ERROR) << "DAG execute block with error!"
                                             << boost::diagnostic_information(*error);
                        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                     SchedulerError::DAGError, "DAG execute error!", *error),
                            nullptr);
                        return;
                    }

                    DMTExecute(std::move(callback));
                });
            }
            else
            {
                DMTExecute(std::move(callback));
            }
        });
    }
    else
    {
        DMTExecute(std::move(callback));
    }
}

void BlockExecutive::asyncCommit(std::function<void(Error::UniquePtr)> callback)
{
    auto stateStorage = std::make_shared<storage::StateStorage>(m_scheduler->m_storage);

    m_currentTimePoint = std::chrono::system_clock::now();

    m_scheduler->m_ledger->asyncPrewriteBlock(stateStorage, m_block,
        [this, stateStorage, callback = std::move(callback)](Error::Ptr&& error) mutable {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Prewrite block error!" << boost::diagnostic_information(*error);
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                    SchedulerError::PrewriteBlockError, "Prewrite block error!", *error));
                return;
            }

            auto status = std::make_shared<CommitStatus>();
            status->total = 1 + m_scheduler->m_executorManager->size();  // self + all executors
            status->checkAndCommit = [this, callback = std::move(callback)](
                                         const CommitStatus& status) {
                if (status.success + status.failed < status.total)
                {
                    return;
                }

                if (status.failed > 0)
                {
                    SCHEDULER_LOG(WARNING) << "Prepare with errors! " +
                                                  boost::lexical_cast<std::string>(status.failed);
                    batchBlockRollback([this, callback](Error::UniquePtr&& error) {
                        if (error)
                        {
                            SCHEDULER_LOG(ERROR)
                                << "Rollback storage failed!" << LOG_KV("number", number()) << " "
                                << boost::diagnostic_information(*error);
                            // FATAL ERROR, NEED MANUAL FIX!

                            callback(std::move(error));
                            return;
                        }
                    });

                    return;
                }

                batchBlockCommit([this, callback](Error::UniquePtr&& error) {
                    if (error)
                    {
                        SCHEDULER_LOG(ERROR)
                            << "Commit block to storage failed!" << LOG_KV("number", number())
                            << boost::diagnostic_information(*error);

                        // FATAL ERROR, NEED MANUAL FIX!

                        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(SchedulerError::UnknownError,
                            "Commit block to storage failed!", *error));
                        return;
                    }

                    m_commitElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now() - m_currentTimePoint);
                    SCHEDULER_LOG(INFO) << "CommitBlock: " << number()
                                        << " success, execute elapsed: " << m_executeElapsed.count()
                                        << "ms hash elapsed: " << m_hashElapsed.count()
                                        << "ms commit elapsed: " << m_commitElapsed.count() << "ms";

                    callback(nullptr);
                });
            };

            storage::TransactionalStorageInterface::TwoPCParams params;
            params.number = number();
            params.primaryTableName = SYS_CURRENT_STATE;
            params.primaryTableKey = SYS_KEY_CURRENT_NUMBER;
            m_scheduler->m_storage->asyncPrepare(
                params, *stateStorage, [status, this](Error::Ptr&& error, uint64_t startTimeStamp) {
                    if (error)
                    {
                        ++status->failed;
                    }
                    else
                    {
                        ++status->success;
                    }

                    executor::ParallelTransactionExecutorInterface::TwoPCParams executorParams;
                    executorParams.number = number();
                    executorParams.primaryTableName = SYS_CURRENT_STATE;
                    executorParams.primaryTableKey = SYS_KEY_CURRENT_NUMBER;
                    executorParams.startTS = startTimeStamp;
                    for (auto& executorIt : *(m_scheduler->m_executorManager))
                    {
                        executorIt->prepare(executorParams, [status](Error::Ptr&& error) {
                            if (error)
                            {
                                ++status->failed;
                            }
                            else
                            {
                                ++status->success;
                                SCHEDULER_LOG(DEBUG)
                                    << "Prepare executor success, success: " << status->success;
                            }
                            status->checkAndCommit(*status);
                        });
                    }
                });
        });
}

void BlockExecutive::asyncNotify(
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(Error::Ptr)>)>& notifier,
    std::function<void(Error::Ptr)> _callback)
{
    if (!notifier)
    {
        return;
    }
    auto results = std::make_shared<bcos::protocol::TransactionSubmitResults>();
    auto blockHeader = m_block->blockHeaderConst();
    auto blockHash = blockHeader->hash();
    size_t index = 0;
    for (auto& it : m_executiveResults)
    {
        auto submitResult = m_transactionSubmitResultFactory->createTxSubmitResult();
        submitResult->setTransactionIndex(index);
        submitResult->setBlockHash(blockHash);
        submitResult->setTxHash(it.transactionHash);
        submitResult->setStatus(it.receipt->status());
        submitResult->setTransactionReceipt(it.receipt);
        if (m_syncBlock)
        {
            auto tx = m_block->transaction(index);
            submitResult->setNonce(tx->nonce());
        }
        index++;
        results->emplace_back(submitResult);
    }
    auto txsSize = m_executiveResults.size();
    notifier(blockHeader->number(), results, [_callback, blockHeader, txsSize](Error::Ptr _error) {
        if (_callback)
        {
            _callback(_error);
        }
        if (_error == nullptr)
        {
            SCHEDULER_LOG(INFO) << LOG_DESC("notify block result success")
                                << LOG_KV("number", blockHeader->number())
                                << LOG_KV("hash", blockHeader->hash().abridged())
                                << LOG_KV("txsSize", txsSize);
            return;
        }
        SCHEDULER_LOG(INFO) << LOG_DESC("notify block result failed")
                            << LOG_KV("code", _error->errorCode())
                            << LOG_KV("msg", _error->errorMessage());
    });
}

void BlockExecutive::DAGExecute(std::function<void(Error::UniquePtr)> callback)
{
    std::multimap<std::string, decltype(m_executiveStates)::iterator> requests;

    for (auto it = m_executiveStates.begin(); it != m_executiveStates.end(); ++it)
    {
        if (it->second.enableDAG)
        {
            requests.emplace(std::get<0>(it->first), it);
        }
    }

    if (requests.empty())
    {
        callback(nullptr);
        return;
    }

    auto totalCount = std::make_shared<std::atomic_size_t>(requests.size());
    auto failed = std::make_shared<std::atomic_size_t>(0);
    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    for (auto it = requests.begin(); it != requests.end(); it = requests.upper_bound(it->first))
    {
        SCHEDULER_LOG(TRACE) << "DAG contract: " << it->first;

        auto executor = m_scheduler->m_executorManager->dispatchExecutor(it->first);
        auto count = requests.count(it->first);
        auto range = requests.equal_range(it->first);

        auto messages = std::make_shared<std::vector<protocol::ExecutionMessage::UniquePtr>>(count);
        auto iterators =
            std::make_shared<std::vector<decltype(m_executiveStates)::iterator>>(count);
        size_t i = 0;
        for (auto messageIt = range.first; messageIt != range.second; ++messageIt)
        {
            SCHEDULER_LOG(TRACE) << "message: " << messageIt->second->second.message.get()
                                 << " to: " << messageIt->first;
            messageIt->second->second.callStack.push(messageIt->second->second.currentSeq++);
            messages->at(i) = std::move(messageIt->second->second.message);
            iterators->at(i) = messageIt->second;

            ++i;
        }

        executor->dagExecuteTransactions(*messages,
            [messages, iterators, totalCount, failed, callbackPtr](bcos::Error::UniquePtr error,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr> responseMessages) {
                *totalCount -= responseMessages.size();

                if (error)
                {
                    ++(*failed);
                    SCHEDULER_LOG(ERROR)
                        << "DAG execute error: " << boost::diagnostic_information(*error);
                }
                else if (messages->size() != responseMessages.size())
                {
                    ++(*failed);
                    SCHEDULER_LOG(ERROR) << "DAG messages mismatch!";
                }
                else
                {
                    for (size_t i = 0; i < responseMessages.size(); ++i)
                    {
                        (*iterators)[i]->second.message = std::move(responseMessages[i]);
                    }
                }

                if (*totalCount == 0)
                {
                    if (*failed > 0)
                    {
                        (*callbackPtr)(BCOS_ERROR_UNIQUE_PTR(
                            SchedulerError::DAGError, "Execute dag with errors"));
                        return;
                    }

                    (*callbackPtr)(nullptr);
                }
            });
    }
}

void BlockExecutive::DMTExecute(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr)> callback)
{
    startBatch([this, callback = std::move(callback)](Error::UniquePtr&& error) {
        if (error)
        {
            callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                         SchedulerError::DMTError, "Execute with errors", *error),
                nullptr);
            return;
        }
        if (!m_executiveStates.empty())
        {
            SCHEDULER_LOG(TRACE) << "Non empty states, continue startBatch";
            DMTExecute(callback);
        }
        else
        {
            SCHEDULER_LOG(TRACE) << "Empty states, end";
            auto now = std::chrono::system_clock::now();
            m_executeElapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - m_currentTimePoint);
            m_currentTimePoint = now;

            if (m_staticCall)
            {
                // Set result to m_block
                for (auto& it : m_executiveResults)
                {
                    m_block->appendReceipt(it.receipt);
                }
                callback(nullptr, nullptr);
            }
            else
            {
                // All Transaction finished, get hash
                batchGetHashes([this, callback = std::move(callback)](
                                   Error::UniquePtr error, crypto::HashType hash) {
                    if (error)
                    {
                        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                     SchedulerError::UnknownError, "Unknown error", *error),
                            nullptr);
                        return;
                    }

                    m_hashElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now() - m_currentTimePoint);

                    // Set result to m_block
                    for (auto& it : m_executiveResults)
                    {
                        m_block->appendReceipt(it.receipt);
                    }
                    auto executedBlockHeader =
                        m_blockFactory->blockHeaderFactory()->populateBlockHeader(
                            m_block->blockHeader());
                    executedBlockHeader->setStateRoot(hash);
                    executedBlockHeader->setGasUsed(m_gasUsed);
                    executedBlockHeader->setTxsRoot(m_block->calculateTransactionRoot());
                    executedBlockHeader->setReceiptsRoot(m_block->calculateReceiptRoot());

                    m_result = executedBlockHeader;
                    callback(nullptr, m_result);
                });
            }
        }
    });
}

void BlockExecutive::batchNextBlock(std::function<void(Error::UniquePtr)> callback)
{
    auto status = std::make_shared<CommitStatus>();
    status->total = m_scheduler->m_executorManager->size();
    status->checkAndCommit = [this, callback = std::move(callback)](const CommitStatus& status) {
        if (status.success + status.failed < status.total)
        {
            return;
        }

        if (status.failed > 0)
        {
            auto message = "Next block:" + boost::lexical_cast<std::string>(number()) +
                           " with errors! " + boost::lexical_cast<std::string>(status.failed);
            SCHEDULER_LOG(ERROR) << message;

            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::UnknownError, std::move(message)));
            return;
        }

        callback(nullptr);
    };

    for (auto& it : *(m_scheduler->m_executorManager))
    {
        auto blockHeader = m_block->blockHeaderConst();
        it->nextBlockHeader(blockHeader, [status](bcos::Error::Ptr&& error) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Nextblock executor error!" << boost::diagnostic_information(*error);
                ++status->failed;
            }
            else
            {
                ++status->success;
            }

            status->checkAndCommit(*status);
        });
    }
}

void BlockExecutive::batchGetHashes(
    std::function<void(bcos::Error::UniquePtr, bcos::crypto::HashType)> callback)
{
    auto mutex = std::make_shared<std::mutex>();
    auto totalHash = std::make_shared<h256>();

    auto status = std::make_shared<CommitStatus>();
    status->total = m_scheduler->m_executorManager->size();  // all executors
    status->checkAndCommit = [this, totalHash, callback = std::move(callback)](
                                 const CommitStatus& status) {
        if (status.success + status.failed < status.total)
        {
            return;
        }

        if (status.failed > 0)
        {
            auto message = "Commit block:" + boost::lexical_cast<std::string>(number()) +
                           " with errors! " + boost::lexical_cast<std::string>(status.failed);
            SCHEDULER_LOG(WARNING) << message;

            callback(
                BCOS_ERROR_UNIQUE_PTR(SchedulerError::CommitError, std::move(message)), h256(0));
            return;
        }

        callback(nullptr, std::move(*totalHash));
    };

    for (auto& it : *(m_scheduler->m_executorManager))
    {
        it->getHash(number(), [status, mutex, totalHash](
                                  bcos::Error::Ptr&& error, crypto::HashType&& hash) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Commit executor error!" << boost::diagnostic_information(*error);
                ++status->failed;
            }
            else
            {
                ++status->success;
                SCHEDULER_LOG(DEBUG) << "GetHash executor success, success: " << status->success;

                std::unique_lock<std::mutex> lock(*mutex);
                *totalHash ^= hash;
            }

            status->checkAndCommit(*status);
        });
    }
}

void BlockExecutive::batchBlockCommit(std::function<void(Error::UniquePtr)> callback)
{
    auto status = std::make_shared<CommitStatus>();
    status->total = 1 + m_scheduler->m_executorManager->size();  // self + all executors
    status->checkAndCommit = [this, callback = std::move(callback)](const CommitStatus& status) {
        if (status.success + status.failed < status.total)
        {
            return;
        }

        if (status.failed > 0)
        {
            auto message = "Commit block:" + boost::lexical_cast<std::string>(number()) +
                           " with errors! " + boost::lexical_cast<std::string>(status.failed);
            SCHEDULER_LOG(WARNING) << message;

            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::CommitError, std::move(message)));
            return;
        }

        callback(nullptr);
    };

    storage::TransactionalStorageInterface::TwoPCParams params;
    params.number = number();
    m_scheduler->m_storage->asyncCommit(params, [status, this](Error::Ptr&& error) {
        if (error)
        {
            SCHEDULER_LOG(ERROR) << "Commit storage error!"
                                 << boost::diagnostic_information(*error);

            ++status->failed;
        }
        else
        {
            ++status->success;
        }

        executor::ParallelTransactionExecutorInterface::TwoPCParams executorParams;
        executorParams.number = number();
        tbb::parallel_for_each(m_scheduler->m_executorManager->begin(),
            m_scheduler->m_executorManager->end(), [&](auto const& executorIt) {
                executorIt->commit(executorParams, [status](bcos::Error::Ptr&& error) {
                    if (error)
                    {
                        SCHEDULER_LOG(ERROR)
                            << "Commit executor error!" << boost::diagnostic_information(*error);
                        ++status->failed;
                    }
                    else
                    {
                        ++status->success;
                        SCHEDULER_LOG(DEBUG)
                            << "Commit executor success, success: " << status->success;
                    }
                    status->checkAndCommit(*status);
                });
            });
    });
}

void BlockExecutive::batchBlockRollback(std::function<void(Error::UniquePtr)> callback)
{
    auto status = std::make_shared<CommitStatus>();
    status->total = 1 + m_scheduler->m_executorManager->size();  // self + all executors
    status->checkAndCommit = [this, callback = std::move(callback)](const CommitStatus& status) {
        if (status.success + status.failed < status.total)
        {
            return;
        }

        if (status.failed > 0)
        {
            auto message = "Rollback block:" + boost::lexical_cast<std::string>(number()) +
                           " with errors! " + boost::lexical_cast<std::string>(status.failed);
            SCHEDULER_LOG(WARNING) << message;

            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::RollbackError, std::move(message)));
            return;
        }

        callback(nullptr);
    };

    storage::TransactionalStorageInterface::TwoPCParams params;
    params.number = number();
    m_scheduler->m_storage->asyncRollback(params, [status](Error::Ptr&& error) {
        if (error)
        {
            SCHEDULER_LOG(ERROR) << "Commit storage error!"
                                 << boost::diagnostic_information(*error);

            ++status->failed;
        }
        else
        {
            ++status->success;
        }
    });

    for (auto& it : *(m_scheduler->m_executorManager))
    {
        executor::ParallelTransactionExecutorInterface::TwoPCParams executorParams;
        executorParams.number = number();
        it->rollback(executorParams, [status](bcos::Error::Ptr&& error) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Rollback executor error!" << boost::diagnostic_information(*error);
                ++status->failed;
            }
            else
            {
                ++status->success;
            }

            status->checkAndCommit(*status);
        });
    }
}

void BlockExecutive::startBatch(std::function<void(Error::UniquePtr)> callback)
{
    SCHEDULER_LOG(TRACE) << "Start batch";
    auto batchStatus = std::make_shared<BatchStatus>();
    batchStatus->callback = std::move(callback);

    traverseExecutive([this, &batchStatus, calledContract = std::set<std::string, std::less<>>()](
                          ExecutiveState& executiveState) mutable {
        if (executiveState.error)
        {
            batchStatus->allSended = true;
            ++batchStatus->error;

            SCHEDULER_LOG(TRACE) << "Detected error!";
            return END;
        }

        auto& message = executiveState.message;

        assert(message);

        auto contextID = executiveState.contextID;
        auto seq = message->seq();

        // Check if another context processing same contract
        auto contractIt = calledContract.end();
        if (!message->to().empty())
        {
            contractIt = calledContract.lower_bound(message->to());
            if (contractIt != calledContract.end() && *contractIt == message->to())
            {
                SCHEDULER_LOG(TRACE)
                    << "Skip, " << contextID << " | " << seq << " | " << message->to();
                executiveState.skip = true;
                return SKIP;
            }
        }

        switch (message->type())
        {
        // Request type, push stack
        case protocol::ExecutionMessage::MESSAGE:
        case protocol::ExecutionMessage::TXHASH:
        {
            auto newSeq = executiveState.currentSeq++;
            if (message->to().empty())
            {
                if (message->createSalt())
                {
                    message->setTo(
                        newEVMAddress(message->from(), message->data(), *(message->createSalt())));
                }
                else
                {
                    message->setTo(newEVMAddress(number(), contextID, newSeq));
                }
            }
            executiveState.callStack.push(newSeq);
            executiveState.message->setSeq(newSeq);

            SCHEDULER_LOG(TRACE) << "Execute, " << message->contextID() << " | " << message->seq()
                                 << " | " << std::hex << message->transactionHash() << " | "
                                 << message->to();

            break;
        }
        // Return type, pop stack
        case protocol::ExecutionMessage::FINISHED:
        case protocol::ExecutionMessage::REVERT:
        {
            executiveState.callStack.pop();

            // Empty stack, execution is finished
            if (executiveState.callStack.empty())
            {
                m_executiveResults[executiveState.contextID].receipt =
                    m_scheduler->m_blockFactory->receiptFactory()->createReceipt(
                        message->gasAvailable(), message->newEVMContractAddress(),
                        std::make_shared<std::vector<bcos::protocol::LogEntry>>(
                            message->takeLogEntries()),
                        message->status(), message->takeData(),
                        m_block->blockHeaderConst()->number());

                // Calc the gas
                m_gasUsed += (TRANSACTION_GAS - message->gasAvailable());

                // Remove executive state and continue
                SCHEDULER_LOG(TRACE)
                    << "Eraseing, " << message->contextID() << " | " << message->seq() << " | "
                    << std::hex << message->transactionHash() << " | " << message->to();

                return DELETE;
            }

            message->setSeq(executiveState.callStack.top());
            message->setCreate(false);

            SCHEDULER_LOG(TRACE) << "FINISHED/REVERT, " << message->contextID() << " | "
                                 << message->seq() << " | " << std::hex
                                 << message->transactionHash() << " | " << message->to();

            break;
        }
        case protocol::ExecutionMessage::REVERT_KEY_LOCK:
        {
            message->setType(protocol::ExecutionMessage::REVERT);
            message->setCreate(false);
            message->setKeyLocks({});
            SCHEDULER_LOG(TRACE) << "REVERT By key lock, " << message->contextID() << " | "
                                 << message->seq() << " | " << std::hex
                                 << message->transactionHash() << " | " << message->to();

            break;
        }
        // Retry type, send again
        case protocol::ExecutionMessage::KEY_LOCK:
        {
            // Try acquire key lock
            if (!m_keyLocks.acquireKeyLock(
                    message->from(), message->keyLockAcquired(), contextID, seq))
            {
                SCHEDULER_LOG(TRACE)
                    << "Waiting key, contract: " << contextID << " | " << seq << " | "
                    << message->from() << " keyLockAcquired: " << toHex(message->keyLockAcquired());
                return PASS;
            }

            SCHEDULER_LOG(TRACE) << "Wait key lock success, " << contextID << " | " << seq << " | "
                                 << message->from()
                                 << " keyLockAcquired: " << toHex(message->keyLockAcquired());
            break;
        }
        // Retry type, send again
        case protocol::ExecutionMessage::SEND_BACK:
        {
            SCHEDULER_LOG(TRACE) << "Send back, " << contextID << " | " << seq << " | "
                                 << message->transactionHash();

            if (message->transactionHash() != h256(0))
            {
                message->setType(protocol::ExecutionMessage::TXHASH);
            }
            else
            {
                message->setType(protocol::ExecutionMessage::MESSAGE);
            }

            if (message->to().empty())
            {
                if (message->createSalt())
                {
                    message->setTo(
                        newEVMAddress(message->from(), message->data(), *(message->createSalt())));
                }
                else
                {
                    message->setTo(newEVMAddress(number(), contextID, seq));
                }
            }

            break;
        }
        }

        calledContract.emplace_hint(contractIt, message->to());

        // Set current key lock into message
        auto keyLocks = m_keyLocks.getKeyLocksNotHoldingByContext(message->to(), contextID);
        message->setKeyLocks(std::move(keyLocks));

        if (c_fileLogLevel >= bcos::LogLevel::TRACE)
        {
            for (auto& keyIt : message->keyLocks())
            {
                SCHEDULER_LOG(TRACE)
                    << boost::format(
                           "Dispatch key lock type: %s, from: %s, to: %s, key: %s, "
                           "contextID: %ld, seq: %ld") %
                           message->type() % message->from() % message->to() % toHex(keyIt) %
                           contextID % message->seq();
            }
        }

        ++batchStatus->total;
        auto executor = m_scheduler->m_executorManager->dispatchExecutor(message->to());

        auto executeCallback = [this, &executiveState, batchStatus](bcos::Error::UniquePtr error,
                                   bcos::protocol::ExecutionMessage::UniquePtr response) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Execute transaction error: " << boost::diagnostic_information(*error);

                executiveState.error = std::move(error);
                executiveState.message.reset();

                // Set error to batch
                ++batchStatus->error;
            }
            else if (!response)
            {
                SCHEDULER_LOG(ERROR) << "Execute transaction with null response!";

                ++batchStatus->error;
            }
            else
            {
                executiveState.message = std::move(response);
            }

            SCHEDULER_LOG(TRACE) << "Execute is finished!";

            ++batchStatus->received;
            checkBatch(*batchStatus);
        };

        if (executiveState.message->staticCall())
        {
            executor->call(std::move(executiveState.message), std::move(executeCallback));
        }
        else
        {
            executor->executeTransaction(
                std::move(executiveState.message), std::move(executeCallback));
        }

        return PASS;
    });

    batchStatus->allSended = true;
    checkBatch(*batchStatus);
}

void BlockExecutive::checkBatch(BatchStatus& status)
{
    SCHEDULER_LOG(TRACE) << "status: " << status.allSended << " " << status.received << " "
                         << status.total;
    if (status.allSended && status.received == status.total)
    {
        bool expect = false;
        if (status.callbackExecuted.compare_exchange_strong(expect, true))  // Run callback once
        {
            SCHEDULER_LOG(TRACE) << "Enter checkBatch callback: " << status.total << " "
                                 << status.received << " " << std::this_thread::get_id() << " "
                                 << status.callbackExecuted;

            SCHEDULER_LOG(TRACE) << "Batch run finished"
                                 << " total: " << status.total << " error: " << status.error;

            if (status.error > 0)
            {
                status.callback(
                    BCOS_ERROR_UNIQUE_PTR(SchedulerError::BatchError, "Batch with errors"));
                return;
            }

            if (!m_executiveStates.empty() && status.total == 0)
            {
                SCHEDULER_LOG(INFO)
                    << "No transaction executed this batch, start processing dead lock";

                traverseExecutive([this](ExecutiveState& executiveState) {
                    if (executiveState.skip)
                    {
                        executiveState.skip = false;
                        return SKIP;
                    }

                    if (m_keyLocks.detectDeadLock(executiveState.contextID))
                    {
                        SCHEDULER_LOG(INFO)
                            << "Detected dead lock at " << executiveState.contextID << " | "
                            << executiveState.message->seq() << " , revert";

                        executiveState.message->setType(
                            bcos::protocol::ExecutionMessage::REVERT_KEY_LOCK);
                        return END;
                    }
                    return PASS;
                });
            }
            else
            {
                // Process key locks & update order
                traverseExecutive([this](ExecutiveState& executiveState) {
                    if (executiveState.skip)
                    {
                        executiveState.skip = false;
                        return SKIP;
                    }

                    auto& message = executiveState.message;
                    switch (message->type())
                    {
                    case protocol::ExecutionMessage::MESSAGE:
                    {
                        m_keyLocks.batchAcquireKeyLock(message->from(), message->keyLocks(),
                            message->contextID(), message->seq());
                        return UPDATE;
                    }
                    case protocol::ExecutionMessage::KEY_LOCK:
                    {
                        m_keyLocks.batchAcquireKeyLock(message->from(), message->keyLocks(),
                            message->contextID(), message->seq());
                        return PASS;
                    }
                    case bcos::protocol::ExecutionMessage::FINISHED:
                    case bcos::protocol::ExecutionMessage::REVERT:
                    {
                        m_keyLocks.releaseKeyLocks(message->contextID(), message->seq());
                        return UPDATE;
                    }
                    case bcos::protocol::ExecutionMessage::REVERT_KEY_LOCK:
                    {
                        return PASS;
                    }
                    default:
                    {
                        return PASS;
                    }
                    }
                });
            }

            status.callback(nullptr);
        }
    }
}

std::string BlockExecutive::newEVMAddress(int64_t blockNumber, int64_t contextID, int64_t seq)
{
    auto hash = m_scheduler->m_hashImpl->hash(boost::lexical_cast<std::string>(blockNumber) + "_" +
                                              boost::lexical_cast<std::string>(contextID) + "_" +
                                              boost::lexical_cast<std::string>(seq));

    std::string hexAddress;
    hexAddress.reserve(40);
    boost::algorithm::hex(hash.data(), hash.data() + 20, std::back_inserter(hexAddress));

    toChecksumAddress(hexAddress, m_scheduler->m_hashImpl);

    return hexAddress;
}

std::string BlockExecutive::newEVMAddress(
    const std::string_view& _sender, bytesConstRef _init, u256 const& _salt)
{
    auto hash = m_scheduler->m_hashImpl->hash(
        bytes{0xff} + _sender + toBigEndian(_salt) + m_scheduler->m_hashImpl->hash(_init));

    std::string hexAddress;
    hexAddress.reserve(40);
    boost::algorithm::hex(hash.data(), hash.data() + 20, std::back_inserter(hexAddress));

    toChecksumAddress(hexAddress, m_scheduler->m_hashImpl);

    return hexAddress;
}

std::string BlockExecutive::preprocessAddress(const std::string_view& address)
{
    std::string out;
    if (address[0] == '0' && address[1] == 'x')
    {
        out = std::string(address.substr(2));
    }
    else
    {
        out = std::string(address);
    }

    boost::to_lower(out);
    return out;
}

void BlockExecutive::traverseExecutive(std::function<TraverseHint(ExecutiveState&)> callback)
{
    std::forward_list<decltype(m_executiveStates)::node_type> updateNodes;

    for (auto it = m_executiveStates.begin(); it != m_executiveStates.end();)
    {
        SCHEDULER_LOG(TRACE) << "Traverse " << std::get<0>(it->first) << " | "
                             << std::get<1>(it->first);
        auto hint = callback(it->second);
        switch (hint)
        {
        case PASS:
        {
            ++it;
            break;
        }
        case DELETE:
        {
            m_executiveStates.erase(it++);
            break;
        }
        case SKIP:
        {
            it = m_executiveStates.upper_bound({std::get<0>(it->first), INT64_MAX});
            break;
        }
        case UPDATE:
        {
            updateNodes.emplace_front(m_executiveStates.extract(it++));
            break;
        }
        case END:
        {
            goto OUT;
        }
        }
    }

OUT:
    // Process the update nodes
    if (!updateNodes.empty())
    {
        for (auto it = updateNodes.begin(); it != updateNodes.end(); ++it)
        {
            it->key() = std::make_tuple(it->mapped().message->to(), it->mapped().contextID);

            SCHEDULER_LOG(TRACE) << "Reinsert context: " << it->mapped().contextID << " | "
                                 << it->mapped().message->seq() << " | " << std::get<0>(it->key());
            m_executiveStates.insert(std::move(*it));
        }
    }
}