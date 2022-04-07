#include "BlockExecutive.h"
#include "ChecksumAddress.h"
#include "Common.h"
#include "SchedulerImpl.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/interfaces/executor/PrecompiledTypeDef.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-table/src/StateStorage.h"
#include <bcos-utilities/Error.h>
#include <tbb/parallel_for_each.h>
#include <boost/algorithm/hex.hpp>
#include <boost/asio/defer.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>

using namespace bcos::scheduler;
using namespace bcos::ledger;

void BlockExecutive::prepare()
{
    if (m_hasPrepared)
    {
        return;
    }

    {
        bcos::WriteGuard lock(x_prepareLock);

        auto startT = utcTime();

        if (m_block->transactionsMetaDataSize() > 0)
        {
            SCHEDULER_LOG(DEBUG) << LOG_KV("block number", m_block->blockHeaderConst()->number())
                                 << LOG_KV("meta tx count", m_block->transactionsMetaDataSize());

            m_executiveResults.resize(m_block->transactionsMetaDataSize());
#pragma omp parallel for
            for (size_t i = 0; i < m_block->transactionsMetaDataSize(); i++)
            {
                auto metaData = m_block->transactionMetaData(i);

                auto message = m_scheduler->m_executionMessageFactory->createExecutionMessage();
                message->setContextID(i + m_startContextID);
                message->setType(protocol::ExecutionMessage::TXHASH);
                message->setTransactionHash(metaData->hash());

                if (metaData->attribute() &
                    bcos::protocol::Transaction::Attribute::LIQUID_SCALE_CODEC)
                {
                    // LIQUID
                    if (metaData->attribute() &
                        bcos::protocol::Transaction::Attribute::LIQUID_CREATE)
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
                    m_withDAG = true;
                }

                if (metaData)
                {
                    m_executiveResults[i].transactionHash = metaData->hash();
                    m_executiveResults[i].source = metaData->source();
                }

                auto to = std::string(message->to());
#pragma omp critical
                registerAndGetDmcExecutor(to)->submit(std::move(message), m_withDAG);
            }
        }
        else if (m_block->transactionsSize() > 0)
        {
            SCHEDULER_LOG(DEBUG) << LOG_KV("block number", m_block->blockHeaderConst()->number())
                                 << LOG_KV("tx count", m_block->transactionsSize());

            m_executiveResults.resize(m_block->transactionsSize());
#pragma omp parallel for
            for (size_t i = 0; i < m_block->transactionsSize(); ++i)
            {
                auto tx = m_block->transaction(i);
                m_executiveResults[i].transactionHash = tx->hash();
                m_executiveResults[i].source = tx->source();

                auto message = m_scheduler->m_executionMessageFactory->createExecutionMessage();
                message->setType(protocol::ExecutionMessage::MESSAGE);
                message->setContextID(i + m_startContextID);
                message->setTransactionHash(tx->hash());
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
                    m_withDAG = true;
                }

                auto to = std::string(message->to());

#pragma omp critical
                registerAndGetDmcExecutor(to)->submit(std::move(message), m_withDAG);
            }
        }

        // prepare all executors
        serialPrepareExecutor();

        SCHEDULER_LOG(DEBUG) << LOG_BADGE("prepareBlockExecutive")
                             << LOG_KV("block number", m_block->blockHeaderConst()->number())
                             << LOG_KV("blockHeader.timestamp",
                                    m_block->blockHeaderConst()->timestamp())
                             << LOG_KV("meta tx count", m_block->transactionsMetaDataSize())
                             << LOG_KV("timeCost", (utcTime() - startT));
        m_hasPrepared = true;
    }
}

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

    if (m_scheduler->executorManager()->size() == 0)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(
                     SchedulerError::ExecutorNotEstablishedError, "The executor has not started!"),
            nullptr);
    }
    m_currentTimePoint = std::chrono::system_clock::now();
    prepare();

    if (!m_staticCall)
    {
        // Execute nextBlock
        batchNextBlock([this, callback = std::move(callback)](Error::UniquePtr error) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Next block with error!" << boost::diagnostic_information(*error);
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             SchedulerError::NextBlockError, "Next block error!", *error),
                    nullptr);
                return;
            }

            auto newBatchStatus = std::shared_ptr<BatchStatus>();
            newBatchStatus->total = m_dmcExecutors.size();
            DMCExecute(newBatchStatus, std::move(callback));
        });
    }
    else
    {
        auto newBatchStatus = std::shared_ptr<BatchStatus>();
        newBatchStatus->total = m_dmcExecutors.size();
        DMCExecute(newBatchStatus, std::move(callback));
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


void BlockExecutive::DMCExecute(BatchStatus::Ptr batchStatus,
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr)> callback)
{
    // prepare all dmcExecutor
    serialPrepareExecutor();

    // dump address for omp parallization
    std::vector<std::string> contractAddress;
    for (auto it = m_dmcExecutors.begin(); it != m_dmcExecutors.end(); it++)
    {
        contractAddress.push_back(it->first);
    }

    auto executorCallback =
        [this, batchStatus = std::move(batchStatus), callback = std::move(callback)](
            bcos::Error::UniquePtr error, DmcExecutor::Status status) {
            // update batch
            if (error || status == DmcExecutor::Status::ERROR)
            {
                batchStatus->error++;
                SCHEDULER_LOG(ERROR)
                    << LOG_BADGE("DmcExecutor") << "dmcExecutor->go() error, "
                    << LOG_KV("errorCode", error ? error->errorCode() : -1)
                    << LOG_KV("errorMessage", error ? error.get()->errorMessage() : "null");
            }
            else if (status == DmcExecutor::Status::PAUSED)
            {
                batchStatus->paused++;
            }
            else if (status == DmcExecutor::Status::FINISHED)
            {
                batchStatus->finished++;
            }


            // check batch
            if ((batchStatus->error + batchStatus->paused + batchStatus->finished) !=
                batchStatus->total)
            {
                return;
            }

            if (batchStatus->callbackExecuted)
            {
                return;
            }
#pragma omp critical
            {
                if (batchStatus->callbackExecuted)
                {
                    return;
                }
                batchStatus->callbackExecuted = true;
            }

            // only one thread can get in here
            if (batchStatus->error != 0)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             SchedulerError::DMTError, "Execute with errors", *error),
                    nullptr);
            }
            else if (batchStatus->paused != 0)
            {
                // Start next DMC round
                auto newBatchStatus = std::shared_ptr<BatchStatus>();
                newBatchStatus->total = m_dmcExecutors.size();
                DMCExecute(newBatchStatus, std::move(callback));
            }
            else if (batchStatus->finished == batchStatus->total)
            {
                // All finished, dump results
                onDmcExecuteFinish(std::move(callback));
            }
            else
            {
                // assume never goes here
                assert(false);
            }
        };

    // for each dmcExecutor
#pragma omp parallel for
    for (size_t i = 0; i < contractAddress.size(); i++)
    {
        auto dmcExecutor = m_dmcExecutors[contractAddress[i]];
        dmcExecutor->go(executorCallback);
    }
}

void BlockExecutive::onDmcExecuteFinish(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr)> callback)
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
                m_blockFactory->blockHeaderFactory()->populateBlockHeader(m_block->blockHeader());
            executedBlockHeader->setStateRoot(hash);
            executedBlockHeader->setGasUsed(m_gasUsed);
            executedBlockHeader->setTxsRoot(m_block->calculateTransactionRoot());
            executedBlockHeader->setReceiptsRoot(m_block->calculateReceiptRoot());

            m_result = executedBlockHeader;
            callback(nullptr, m_result);
        });
    }
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
    auto startT = utcTime();
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
        it->getHash(number(),
            [startT, status, mutex, totalHash](bcos::Error::Ptr&& error, crypto::HashType&& hash) {
                if (error)
                {
                    SCHEDULER_LOG(ERROR)
                        << "Commit executor error!" << boost::diagnostic_information(*error);
                    ++status->failed;
                }
                else
                {
                    ++status->success;
                    SCHEDULER_LOG(DEBUG) << "GetHash executor success, success: " << status->success
                                         << LOG_KV("timecost", (utcTime() - startT));

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


DmcExecutor::Ptr BlockExecutive::registerAndGetDmcExecutor(std::string contractAddress)
{
    auto dmcExecutor = m_dmcExecutors[contractAddress];
    if (dmcExecutor)
    {
        return dmcExecutor;
    }

    auto executor = m_scheduler->executorManager()->dispatchExecutor(contractAddress);
    dmcExecutor = std::make_shared<DmcExecutor>(
        contractAddress, m_block, executor, m_keyLocks, m_scheduler->m_hashImpl);
    m_dmcExecutors[contractAddress] = dmcExecutor;

    // register functions
    dmcExecutor->setSchedulerOutHandler(
        [this](ExecutiveState::Ptr executiveState) { schedulerExecutive(executiveState); });

    dmcExecutor->setOnTxFinishedHandler([this](bcos::protocol::ExecutionMessage::UniquePtr output) {
        onTxFinish(std::move(output));
    });

    return dmcExecutor;
}

void BlockExecutive::schedulerExecutive(ExecutiveState::Ptr executiveState)
{
    auto to = std::string(executiveState->message->to());

    auto dmcExecutor = m_dmcExecutors[to];
    if (!dmcExecutor)
    {
        dmcExecutor = registerAndGetDmcExecutor(to);
    }

    dmcExecutor->schedulerIn(executiveState);
}

void BlockExecutive::onTxFinish(bcos::protocol::ExecutionMessage::UniquePtr output)
{
    auto txGasUsed = TRANSACTION_GAS - output->gasAvailable();
    // Calc the gas set to header
    m_gasUsed += txGasUsed;

    // write receipt in results
    m_executiveResults[output->contextID()].receipt =
        m_scheduler->m_blockFactory->receiptFactory()->createReceipt(txGasUsed,
            output->newEVMContractAddress(),
            std::make_shared<std::vector<bcos::protocol::LogEntry>>(output->takeLogEntries()),
            output->status(), output->takeData(), m_block->blockHeaderConst()->number());
}


void BlockExecutive::serialPrepareExecutor()
{
    // Notice:
    // For the same aquire lock priority
    // m_dmcExecutors must be prepared in contractAddress less<> serial order
    // Aquire lock happens in dmcExecutor->prepare()
    for (auto it = m_dmcExecutors.begin(); it != m_dmcExecutors.end(); it++)
    {
        it->second->prepare();
    }
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
