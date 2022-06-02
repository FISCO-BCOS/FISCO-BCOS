#include "BlockExecutive.h"
#include "ChecksumAddress.h"
#include "Common.h"
#include "DmcExecutor.h"
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
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iterator>
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

    auto startT = utcTime();

    if (m_block->transactionsMetaDataSize() > 0)
    {
        SCHEDULER_LOG(DEBUG) << LOG_KV("block number", m_block->blockHeaderConst()->number())
                             << LOG_KV("", m_block->transactionsMetaDataSize());

        m_executiveResults.resize(m_block->transactionsMetaDataSize());
#pragma omp parallel for
        for (size_t i = 0; i < m_block->transactionsMetaDataSize(); ++i)
        {
            auto metaData = m_block->transactionMetaData(i);
            if (metaData)
            {
                m_executiveResults[i].transactionHash = metaData->hash();
                m_executiveResults[i].source = metaData->source();
            }

            auto message = m_scheduler->m_executionMessageFactory->createExecutionMessage();
            auto contextID = i + m_startContextID;

            message->setContextID(contextID);
            message->setType(protocol::ExecutionMessage::TXHASH);
            // Note: set here for fetching txs when send_back
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
            message->setGasAvailable(m_gasLimit);
            message->setStaticCall(false);


            bool enableDAG = metaData->attribute() & bcos::protocol::Transaction::Attribute::DAG;

            std::string to = {message->to().data(), message->to().size()};
#pragma omp critical
            m_hasDAG = enableDAG;
            registerAndGetDmcExecutor(to)->submit(std::move(message), m_hasDAG);
        }
#pragma omp flush(m_hasDAG)
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
            if (!m_isSysBlock)
            {
                auto toAddress = tx->to();
                if (bcos::precompiled::c_systemTxsAddress.count(
                        std::string(toAddress.begin(), toAddress.end())))
                {
                    m_isSysBlock.store(true);
                }
            }
            m_executiveResults[i].transactionHash = tx->hash();
            m_executiveResults[i].source = tx->source();

            auto contextID = i + m_startContextID;
            auto message = m_scheduler->m_executionMessageFactory->createExecutionMessage();
            message->setType(protocol::ExecutionMessage::MESSAGE);
            message->setContextID(contextID);
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
            if (message->create())
            {
                message->setABI(std::string(tx->abi()));
            }
            message->setDepth(0);
            message->setGasAvailable(m_gasLimit);
            message->setData(tx->input().toBytes());
            message->setStaticCall(m_staticCall);

            bool enableDAG = tx->attribute() & bcos::protocol::Transaction::Attribute::DAG;

            std::string to = {message->to().data(), message->to().size()};

#pragma omp critical
            m_hasDAG = enableDAG;
            registerAndGetDmcExecutor(to)->submit(std::move(message), m_hasDAG);
        }
    }
#pragma omp flush(m_hasDAG)
    // prepare all executors
    if (!m_hasDAG)
    {
        // prepare DMC executor
        serialPrepareExecutor();
    }

    m_hasPrepared = true;

    SCHEDULER_LOG(DEBUG) << METRIC << LOG_BADGE("prepareBlockExecutive")
                         << LOG_KV("block number", m_block->blockHeaderConst()->number())
                         << LOG_KV(
                                "blockHeader.timestamp", m_block->blockHeaderConst()->timestamp())
                         << LOG_KV("meta tx count", m_block->transactionsMetaDataSize())
                         << LOG_KV("timeCost", (utcTime() - startT));
}
void BlockExecutive::asyncCall(
    std::function<void(Error::UniquePtr&&, protocol::TransactionReceipt::Ptr&&)> callback)
{
    asyncExecute([executive = shared_from_this(), callback](
                     Error::UniquePtr&& _error, protocol::BlockHeader::Ptr, bool) {
        // auto executive = self.lock();
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
        bool hasDAG = m_hasDAG;
        batchNextBlock([this, hasDAG, startT, createMsgT, callback = std::move(callback)](
                           Error::UniquePtr error) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Next block with error!" << boost::diagnostic_information(*error);
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             SchedulerError::NextBlockError, "Next block error!", *error),
                    nullptr, m_isSysBlock);
                return;
            }

            if (hasDAG)
            {
                DAGExecute([this, startT, createMsgT, callback = std::move(callback)](
                               Error::UniquePtr error) {
                    if (error)
                    {
                        SCHEDULER_LOG(ERROR) << "DAG execute block with error!"
                                             << boost::diagnostic_information(*error);
                        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                     SchedulerError::DAGError, "DAG execute error!", *error),
                            nullptr, m_isSysBlock);
                        return;
                    }
                    auto blockHeader = m_block->blockHeader();
                    SCHEDULER_LOG(INFO)
                        << LOG_DESC("DAGExecute success") << LOG_KV("createMsgT", createMsgT)
                        << LOG_KV("dagExecuteT", (utcTime() - startT))
                        << LOG_KV("hash", blockHeader->hash().abridged())
                        << LOG_KV("number", blockHeader->number());
                    DMCExecute(std::move(callback));
                });
            }
            else
            {
                DMCExecute(std::move(callback));
            }
        });
    }
    else
    {
        DMCExecute(std::move(callback));
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

            bcos::protocol::TwoPCParams params;
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
                    SCHEDULER_LOG(INFO)
                        << "primary prepare finished, call executor prepare"
                        << LOG_KV("blockNumber", number())
                        << LOG_KV("startTimeStamp", startTimeStamp)
                        << LOG_KV("executors", m_scheduler->m_executorManager->size())
                        << LOG_KV("success", status->success) << LOG_KV("failed", status->failed);
                    bcos::protocol::TwoPCParams executorParams;
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
        // set blockHash for the receipt
        it.receipt->setBlockHash(blockHash);
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
    // dump executive states from DmcExecutor
    for (auto it : m_dmcExecutors)
    {
        auto& address = it.first;
        auto dmcExecutor = it.second;
        dmcExecutor->forEachExecutive(
            [this, &address](ContextID contextID, ExecutiveState::Ptr executiveState) {
                m_executiveStates.emplace(std::make_tuple(address, contextID), executiveState);
            });
    }


    std::multimap<std::string, decltype(m_executiveStates)::iterator> requests;

    for (auto it = m_executiveStates.begin(); it != m_executiveStates.end(); ++it)
    {
        if (it->second->enableDAG)
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
        auto startT = utcTime();

        auto executor = m_scheduler->m_executorManager->dispatchExecutor(it->first);
        auto count = requests.count(it->first);
        auto range = requests.equal_range(it->first);

        auto messages = std::make_shared<std::vector<protocol::ExecutionMessage::UniquePtr>>(count);
        auto iterators = std::vector<decltype(m_executiveStates)::iterator>(count);
        size_t i = 0;
        for (auto messageIt = range.first; messageIt != range.second; ++messageIt)
        {
            SCHEDULER_LOG(TRACE) << "DAG message: " << messageIt->second->second->message.get()
                                 << " to: " << messageIt->first;
            messageIt->second->second->callStack.push(messageIt->second->second->currentSeq++);
            messages->at(i) = std::move(messageIt->second->second->message);
            iterators[i] = messageIt->second;

            ++i;
        }
        auto prepareT = utcTime() - startT;
        startT = utcTime();
        executor->dagExecuteTransactions(*messages,
            [messages, startT, prepareT, iterators = std::move(iterators), totalCount, failed,
                callbackPtr](bcos::Error::UniquePtr error,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr> responseMessages) {
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
#pragma omp parallel for
                    for (size_t j = 0; j < responseMessages.size(); ++j)
                    {
                        assert(responseMessages[j]);
                        iterators[j]->second->message = std::move(responseMessages[j]);
                    }
                }

                totalCount->fetch_sub(responseMessages.size());
                // TODO: must wait more response
                if (*totalCount == 0)
                {
                    if (*failed > 0)
                    {
                        (*callbackPtr)(BCOS_ERROR_UNIQUE_PTR(
                            SchedulerError::DAGError, "Execute dag with errors"));
                        return;
                    }
                    SCHEDULER_LOG(INFO)
                        << LOG_DESC("DAGExecute finish") << LOG_KV("prepareT", prepareT)
                        << LOG_KV("execT", (utcTime() - startT));
                    (*callbackPtr)(nullptr);
                }
            });
    }
}

void BlockExecutive::DMCExecute(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    // prepare all dmcExecutor
    serialPrepareExecutor();

    // dump address for omp parallization
    std::vector<std::string> contractAddress;
    contractAddress.reserve(m_dmcExecutors.size());
    for (auto it = m_dmcExecutors.begin(); it != m_dmcExecutors.end(); it++)
    {
        contractAddress.push_back(it->first);
    }
    auto batchStatus = std::make_shared<BatchStatus>();
    batchStatus->total = contractAddress.size();

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
            else if (status == DmcExecutor::Status::PAUSED ||
                     status == DmcExecutor::Status::NEED_PREPARE)
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

            // block many threads
            if (batchStatus->callbackExecuted)
            {
                return;
            }
            {
                WriteGuard lock(batchStatus->x_lock);
                if (batchStatus->callbackExecuted)
                {
                    return;
                }
                batchStatus->callbackExecuted = true;
            }

            // handle batch result(only one thread can get in here)
            if (batchStatus->error != 0)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             SchedulerError::DMTError, "Execute with errors", *error),
                    nullptr, m_isSysBlock);
            }
            else if (batchStatus->paused != 0)  // new contract
            {
                // Start next DMC round
                DMCExecute(std::move(callback));
            }
            else if (batchStatus->finished == batchStatus->total)
            {
                onDmcExecuteFinish(std::move(callback));
            }
            else
            {
                // assume never goes here
                SCHEDULER_LOG(FATAL) << "Invalid type";
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
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
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
        callback(nullptr, nullptr, m_isSysBlock);
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
                    nullptr, m_isSysBlock);
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
            callback(nullptr, m_result, m_isSysBlock);
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

    bcos::protocol::TwoPCParams params;
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

        bcos::protocol::TwoPCParams executorParams;
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

    bcos::protocol::TwoPCParams params;
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
        bcos::protocol::TwoPCParams executorParams;
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
    auto dmcExecutorIt = m_dmcExecutors.find(contractAddress);
    if (dmcExecutorIt != m_dmcExecutors.end())
    {
        return dmcExecutorIt->second;
    }
    {
        bcos::WriteGuard lock(x_dmcExecutorLock);
        dmcExecutorIt = m_dmcExecutors.find(contractAddress);
        if (dmcExecutorIt != m_dmcExecutors.end())
        {
            return dmcExecutorIt->second;
        }
        auto executor = m_scheduler->executorManager()->dispatchExecutor(contractAddress);
        auto dmcExecutor = std::make_shared<DmcExecutor>(
            contractAddress, m_block, executor, m_keyLocks, m_scheduler->m_hashImpl);
        m_dmcExecutors.emplace(contractAddress, dmcExecutor);

        // register functions
        dmcExecutor->setSchedulerOutHandler(
            [this](ExecutiveState::Ptr executiveState) { scheduleExecutive(executiveState); });

        dmcExecutor->setOnTxFinishedHandler(
            [this](bcos::protocol::ExecutionMessage::UniquePtr output) {
                onTxFinish(std::move(output));
            });

        return dmcExecutor;
    }
}

void BlockExecutive::scheduleExecutive(ExecutiveState::Ptr executiveState)
{
    auto to = std::string(executiveState->message->to());

    DmcExecutor::Ptr dmcExecutor = registerAndGetDmcExecutor(to);

    dmcExecutor->scheduleIn(executiveState);
}

void BlockExecutive::onTxFinish(bcos::protocol::ExecutionMessage::UniquePtr output)
{
    auto txGasUsed = m_gasLimit - output->gasAvailable();
    // Calc the gas set to header
    m_gasUsed += txGasUsed;
#ifdef DMC_TRACE_LOG_ENABLE
    DMC_LOG(TRACE) << " 6.GenReceipt:\t\t [^^] " << output->toString()
                   << " -> contextID:" << output->contextID() - m_startContextID << std::endl;
#endif
    // write receipt in results
    auto receipt = m_scheduler->m_blockFactory->receiptFactory()->createReceipt(txGasUsed,
        output->newEVMContractAddress(),
        std::make_shared<std::vector<bcos::protocol::LogEntry>>(output->takeLogEntries()),
        output->status(), output->takeData(), m_block->blockHeaderConst()->number());
    receipt->setVersion(m_block->blockHeaderConst()->version());
    m_executiveResults[output->contextID() - m_startContextID].receipt = receipt;
}


void BlockExecutive::serialPrepareExecutor()
{
    // Notice:
    // For the same DMC lock priority
    // m_dmcExecutors must be prepared in contractAddress less<> serial order

    /// Handle normal message
    bool hasScheduleOut;
    do
    {
        hasScheduleOut = false;

        // dump current DmcExecutor
        std::set<std::string, std::less<>> currentExecutors;
        for (auto it = m_dmcExecutors.begin(); it != m_dmcExecutors.end(); it++)
        {
            it->second->releaseOutdatedLock();  // release last round's lock
            currentExecutors.insert(it->first);
        }

        // for each current DmcExecutor
        for (auto& address : currentExecutors)
        {
#ifdef DMC_TRACE_LOG_ENABLE
            DMC_LOG(TRACE) << " 0.Pre-DmcExecutor: ----------------- addr:" << address
                           << " | number:" << m_block->blockHeaderConst()->number()
                           << " -----------------" << std::endl;
#endif
            hasScheduleOut |=
                m_dmcExecutors[address]->prepare();  // may generate new contract in m_dmcExecutors
        }

        // must all schedule out message has been handled.
    } while (hasScheduleOut);


    /// try to handle locked message
    // try to unlock some locked tx
    bool needDetectDeadlock = true;
    bool allFinished = true;
    for (auto it = m_dmcExecutors.begin(); it != m_dmcExecutors.end(); it++)
    {
        auto& address = it->first;
        auto dmcExecutor = m_dmcExecutors[address];
        if (dmcExecutor->hasFinished())
        {
            continue;  // must jump finished executor
        }
#ifdef DMC_TRACE_LOG_ENABLE
        DMC_LOG(TRACE) << " 3.UnlockPrepare: \t |---------------- addr:" << address
                       << " | number:" << std::to_string(m_block->blockHeaderConst()->number())
                       << " ----------------|" << std::endl;
#endif

        allFinished = false;
        bool need = dmcExecutor->unlockPrepare();
        needDetectDeadlock &= need;
        // if there is an executor need detect deadlock, noNeedDetectDeadlock = false
    }

    if (needDetectDeadlock && !allFinished)
    {
        bool needRevert = false;
        // detect deadlock and revert the first tx TODO: revert many tx in one DMC round
        for (auto it = m_dmcExecutors.begin(); it != m_dmcExecutors.end(); it++)
        {
            auto& address = it->first;
#ifdef DMC_TRACE_LOG_ENABLE
            DMC_LOG(TRACE) << " --detect--revert-- " << address << " | "
                           << m_block->blockHeaderConst()->number() << " -----------------"
                           << std::endl;
#endif
            if (m_dmcExecutors[address]->detectLockAndRevert())
            {
                needRevert = true;
                break;  // Just revert the first found tx
            }
        }

        if (!needRevert)
        {
            std::string errorMsg = "Need detect deadlock but no deadlock detected! block: " +
                                   toString(m_block->blockHeaderConst()->number());
            DMC_LOG(ERROR) << errorMsg;
            BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, errorMsg));
        }
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
