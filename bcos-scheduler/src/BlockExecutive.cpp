#include "BlockExecutive.h"
#include "Common.h"
#include "DmcExecutor.h"
#include "SchedulerImpl.h"
#include "bcos-crypto/bcos-crypto/ChecksumAddress.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-table/src/StateStorage.h"
#include <bcos-framework/executor/ExecuteError.h>
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

BlockExecutive::BlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
    size_t startContextID,
    bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
    bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
    bcos::txpool::TxPoolInterface::Ptr _txPool)
  : m_dmcRecorder(std::make_shared<DmcStepRecorder>()),
    m_block(std::move(block)),
    m_scheduler(scheduler),
    m_schedulerTermId(scheduler->getSchedulerTermId()),
    m_startContextID(startContextID),
    m_transactionSubmitResultFactory(std::move(transactionSubmitResultFactory)),
    m_blockFactory(_blockFactory),
    m_txPool(_txPool),
    m_staticCall(staticCall)
{
    start();
}

void BlockExecutive::prepare()
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

    auto startT = utcTime();

    if (m_block->transactionsMetaDataSize() > 0)
    {
        buildExecutivesFromMetaData();
    }
    else if (m_block->transactionsSize() > 0)
    {
        buildExecutivesFromNormalTransaction();
    }
    else
    {
        SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number()) << "BlockExecutive prepare: empty block";
    }
#pragma omp flush(m_hasDAG)

    // prepare all executors
    if (!m_hasDAG)
    {
        // prepare DMC executor
        serialPrepareExecutor();
    }

    m_hasPrepared = true;

    SCHEDULER_LOG(INFO) << METRIC << LOG_BADGE("prepareBlockExecutive") << BLOCK_NUMBER(number())
                        << LOG_KV("blockHeader.timestamp", m_block->blockHeaderConst()->timestamp())
                        << LOG_KV("meta tx count", m_block->transactionsMetaDataSize())
                        << LOG_KV("timeCost", (utcTime() - startT));
}

bcos::protocol::ExecutionMessage::UniquePtr BlockExecutive::buildMessage(
    ContextID contextID, bcos::protocol::Transaction::ConstPtr tx)
{
    auto message = m_scheduler->m_executionMessageFactory->createExecutionMessage();
    message->setType(protocol::ExecutionMessage::MESSAGE);
    message->setContextID(contextID);
    message->setTransactionHash(tx->hash());
    message->setOrigin(toHex(tx->sender()));
    message->setFrom(std::string(message->origin()));

    if (!m_isSysBlock)
    {
        auto toAddress = tx->to();
        if (bcos::precompiled::c_systemTxsAddress.count(
                std::string(toAddress.begin(), toAddress.end())))
        {
            m_isSysBlock.store(true);
        }
    }

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
                isSysContractDeploy(m_block->blockHeaderConst()->number()) &&
                tx->to() == precompiled::AUTH_COMMITTEE_ADDRESS)
            {
                // if enable auth check, and first deploy auth contract
                message->setCreate(true);
            }
            message->setTo(preprocessAddress(tx->to()));
        }
    }
    message->setDepth(0);
    message->setGasAvailable(m_gasLimit);
    if (precompiled::c_systemTxsAddress.count({tx->to().data(), tx->to().size()}))
    {
        message->setGasAvailable(TRANSACTION_GAS);
    }
    message->setData(tx->input().toBytes());
    message->setStaticCall(m_staticCall);

    if (message->create())
    {
        message->setABI(std::string(tx->abi()));
    }

    bool enableDAG = tx->attribute() & bcos::protocol::Transaction::Attribute::DAG;

    {
#pragma omp critical
        m_hasDAG = enableDAG;
    }
#pragma omp flush(m_hasDAG)
    return message;
}

void BlockExecutive::buildExecutivesFromMetaData()
{
    SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number())
                         << "BlockExecutive prepare: buildExecutivesFromMetaData"
                         << LOG_KV("tx meta count", m_block->transactionsMetaDataSize());

    m_blockTxs = fetchBlockTxsFromTxPool(m_block, m_txPool);  // no need to async

    m_executiveResults.resize(m_block->transactionsMetaDataSize());
    if (m_blockTxs)
    {
        // can fetch tx from txpool, build message which type is MESSAGE
#pragma omp parallel for
        for (size_t i = 0; i < m_block->transactionsMetaDataSize(); ++i)
        {
            auto metaData = m_block->transactionMetaData(i);
            if (metaData)
            {
                m_executiveResults[i].transactionHash = metaData->hash();
                m_executiveResults[i].source = metaData->source();
            }
            auto contextID = i + m_startContextID;
            auto message = buildMessage(contextID, (*m_blockTxs)[i]);
            std::string to = {message->to().data(), message->to().size()};
            bool enableDAG = metaData->attribute() & bcos::protocol::Transaction::Attribute::DAG;
#pragma omp critical
            m_hasDAG = enableDAG;
            saveMessage(to, std::move(message), enableDAG);
        }
    }
    else
    {
        // only has txHash, build message which type is TXHASH
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
            if (precompiled::c_systemTxsAddress.count(
                    {metaData->to().data(), metaData->to().size()}))
            {
                message->setGasAvailable(TRANSACTION_GAS);
            }
            message->setStaticCall(false);
            bool enableDAG = metaData->attribute() & bcos::protocol::Transaction::Attribute::DAG;

            std::string to = {message->to().data(), message->to().size()};
#pragma omp critical
            m_hasDAG = m_hasDAG || enableDAG;
            saveMessage(to, std::move(message), enableDAG);
        }
    }
}


void BlockExecutive::buildExecutivesFromNormalTransaction()
{
    SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number())
                        << "BlockExecutive prepare: buildExecutivesFromNormalTransaction"
                        << LOG_KV("block number", m_block->blockHeaderConst()->number())
                        << LOG_KV("tx count", m_block->transactionsSize());

    m_executiveResults.resize(m_block->transactionsSize());
#pragma omp parallel for
    for (size_t i = 0; i < m_block->transactionsSize(); ++i)
    {
        auto tx = m_block->transaction(i);
        m_executiveResults[i].transactionHash = tx->hash();
        m_executiveResults[i].source = tx->source();

        auto contextID = i + m_startContextID;
        auto message = buildMessage(contextID, tx);
        std::string to = {message->to().data(), message->to().size()};
        bool enableDAG = tx->attribute() & bcos::protocol::Transaction::Attribute::DAG;

#pragma omp critical
        m_hasDAG = m_hasDAG || enableDAG;
        saveMessage(to, std::move(message), enableDAG);
    }
}

bcos::protocol::TransactionsPtr BlockExecutive::fetchBlockTxsFromTxPool(
    bcos::protocol::Block::Ptr block, bcos::txpool::TxPoolInterface::Ptr txPool)
{
    SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << "BlockExecutive prepare: fillBlock start"
                        << LOG_KV("txNum", block->transactionsMetaDataSize());
    bcos::protocol::TransactionsPtr txs = nullptr;
    auto lastT = utcTime();
    if (txPool != nullptr)
    {
        // Get tx hash list
        auto txHashes = std::make_shared<protocol::HashList>();
        for (size_t i = 0; i < block->transactionsMetaDataSize(); ++i)
        {
            txHashes->emplace_back(block->transactionMetaData(i)->hash());
        }

        std::shared_ptr<std::promise<bcos::protocol::TransactionsPtr>> txsPromise =
            std::make_shared<std::promise<bcos::protocol::TransactionsPtr>>();
        txPool->asyncFillBlock(
            txHashes, [txsPromise](Error::Ptr error, bcos::protocol::TransactionsPtr txs) {
                boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                if (!txsPromise)
                {
                    return;
                }

                if (error)
                {
                    txsPromise->set_value(nullptr);
                }
                else
                {
                    txsPromise->set_value(txs);
                }
            });
        auto future = txsPromise->get_future();
        if (future.wait_for(std::chrono::milliseconds(10 * 1000)) != std::future_status::ready)
        {
            // 10s timeout
            SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number())
                                 << "BlockExecutive prepare: fillBlock timeout/error"
                                 << LOG_KV("txNum", block->transactionsMetaDataSize())
                                 << LOG_KV("cost", utcTime() - lastT)
                                 << LOG_KV("fetchNum", txs ? txs->size() : 0);
            return nullptr;
        }
        txs = future.get();
        txsPromise = nullptr;
    }
    SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << "BlockExecutive prepare: fillBlock end"
                        << LOG_KV("txNum", block->transactionsMetaDataSize())
                        << LOG_KV("cost", utcTime() - lastT)
                        << LOG_KV("fetchNum", txs ? txs->size() : 0);
    return txs;
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
            if (!m_isRunning)
            {
                callback(
                    BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"),
                    nullptr, m_isSysBlock);
                return;
            }

            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << BLOCK_NUMBER(number()) << "Next block with error!" << error->errorMessage();
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             SchedulerError::NextBlockError, "Next block error!", *error),
                    nullptr, m_isSysBlock);
                return;
            }

            if (hasDAG)
            {
                DAGExecute([this, startT, createMsgT, callback = std::move(callback)](
                               Error::UniquePtr error) {
                    if (!m_isRunning)
                    {
                        callback(BCOS_ERROR_UNIQUE_PTR(
                                     SchedulerError::Stopped, "BlockExecutive is stopped"),
                            nullptr, m_isSysBlock);
                        return;
                    }

                    if (error)
                    {
                        SCHEDULER_LOG(ERROR)
                            << "DAG execute block with error!" << error->errorMessage();
                        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                     SchedulerError::DAGError, "DAG execute error!", *error),
                            nullptr, m_isSysBlock);
                        return;
                    }
                    auto blockHeader = m_block->blockHeader();
                    SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << LOG_DESC("DAGExecute success")
                                        << LOG_KV("createMsgT", createMsgT)
                                        << LOG_KV("dagExecuteT", (utcTime() - startT))
                                        << LOG_KV("hash", blockHeader->hash().abridged());
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
    SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << LOG_DESC("BlockExecutive commit block");

    m_scheduler->m_ledger->asyncPrewriteBlock(stateStorage, m_blockTxs, m_block,
        [this, stateStorage, callback = std::move(callback)](Error::Ptr&& error) mutable {
            if (error)
            {
                SCHEDULER_LOG(ERROR) << "Prewrite block error!" << error->errorMessage();
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(SchedulerError::PrewriteBlockError,
                    "Prewrite block error: " + error->errorMessage(), *error));
                return;
            }

            auto status = std::make_shared<CommitStatus>();
            status->total = 1 + m_scheduler->m_executorManager->size();  // self + all executors
            status->checkAndCommit = [this, callback](const CommitStatus& status) {
                if (status.failed > 0)
                {
                    std::string errorMessage = "Prepare with errors, begin rollback, status: " +
                                               boost::lexical_cast<std::string>(status.failed);
                    SCHEDULER_LOG(WARNING) << BLOCK_NUMBER(number()) << errorMessage;
                    batchBlockRollback([this, callback, errorMessage](Error::UniquePtr&& error) {
                        if (error)
                        {
                            SCHEDULER_LOG(ERROR)
                                << BLOCK_NUMBER(number()) << "Rollback storage failed!"
                                << LOG_KV("number", number()) << " " << error->errorMessage();
                            // FATAL ERROR, NEED MANUAL FIX!

                            callback(std::move(error));
                            return;
                        }
                        else
                        {
                            callback(
                                BCOS_ERROR_UNIQUE_PTR(SchedulerError::CommitError, errorMessage));
                            return;
                        }
                    });

                    return;
                }

                SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << "batchCommitBlock begin";
                batchBlockCommit([this, callback](Error::UniquePtr&& error) {
                    if (error)
                    {
                        SCHEDULER_LOG(ERROR) << "Commit block to storage failed!"
                                             << LOG_KV("number", number()) << error->errorMessage();

                        // FATAL ERROR, NEED MANUAL FIX!

                        callback(std::move(error));
                        return;
                    }

                    m_commitElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now() - m_currentTimePoint);
                    SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << "CommitBlock: "
                                        << "success, execute elapsed: " << m_executeElapsed.count()
                                        << "ms hash elapsed: " << m_hashElapsed.count()
                                        << "ms commit elapsed: " << m_commitElapsed.count() << "ms";

                    callback(nullptr);
                });
            };

            bcos::protocol::TwoPCParams params;
            params.number = number();
            params.primaryTableName = SYS_CURRENT_STATE;
            params.primaryTableKey = SYS_KEY_CURRENT_NUMBER;
            m_scheduler->m_storage->asyncPrepare(params, *stateStorage,
                [status, this, callback](Error::Ptr&& error, uint64_t startTimeStamp) {
                    if (error)
                    {
                        ++status->failed;
                        SCHEDULER_LOG(ERROR)
                            << BLOCK_NUMBER(number())
                            << "scheduler asyncPrepare storage error: " << error->errorMessage();
                        callback(BCOS_ERROR_UNIQUE_PTR(error->errorCode(),
                            "asyncPrepare block error: " + error->errorMessage()));
                        return;
                    }
                    else
                    {
                        ++status->success;
                    }
                    SCHEDULER_LOG(INFO)
                        << BLOCK_NUMBER(number())
                        << "primary prepare finished, call executor prepare"
                        << LOG_KV("startTimeStamp", startTimeStamp)
                        << LOG_KV("executors", m_scheduler->m_executorManager->size())
                        << LOG_KV("success", status->success) << LOG_KV("failed", status->failed);
                    bcos::protocol::TwoPCParams executorParams;
                    executorParams.number = number();
                    executorParams.primaryTableName = SYS_CURRENT_STATE;
                    executorParams.primaryTableKey = SYS_KEY_CURRENT_NUMBER;
                    executorParams.timestamp = startTimeStamp;
                    for (auto& executorIt : *(m_scheduler->m_executorManager))
                    {
                        executorIt->prepare(executorParams, [this, status](Error::Ptr&& error) {
                            {
                                WriteGuard lock(status->x_lock);
                                if (error)
                                {
                                    ++status->failed;
                                    SCHEDULER_LOG(ERROR)
                                        << BLOCK_NUMBER(number())
                                        << "asyncPrepare executor failed: " << error->what();

                                    if (error->errorCode() ==
                                        bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                                    {
                                        triggerSwitch();
                                    }
                                }
                                else
                                {
                                    ++status->success;
                                    SCHEDULER_LOG(DEBUG)
                                        << BLOCK_NUMBER(number())
                                        << "asyncPrepare executor success, success: "
                                        << status->success;
                                }
                                if (status->success + status->failed < status->total)
                                {
                                    return;
                                }
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
            SCHEDULER_LOG(INFO) << BLOCK_NUMBER(blockHeader->number())
                                << LOG_DESC("notify block result success")
                                << LOG_KV("hash", blockHeader->hash().abridged())
                                << LOG_KV("txsSize", txsSize);
            return;
        }
        SCHEDULER_LOG(INFO) << BLOCK_NUMBER(blockHeader->number())
                            << LOG_DESC("notify block result failed")
                            << LOG_KV("code", _error->errorCode())
                            << LOG_KV("msg", _error->errorMessage());
    });
}

void BlockExecutive::saveMessage(
    std::string address, protocol::ExecutionMessage::UniquePtr message, bool withDAG)
{
    registerAndGetDmcExecutor(address)->submit(std::move(message), withDAG);
}

void BlockExecutive::DAGExecute(std::function<void(Error::UniquePtr)> callback)
{
    if (!m_isRunning)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"));
        return;
    }

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

    // map< string => <tuple<std::string, ContextID> => ExecutiveState::Ptr>::it >
    std::multimap<std::string, decltype(m_executiveStates)::iterator> requests;

    // filter enableDAG request to map
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
    SCHEDULER_LOG(INFO) << LOG_BADGE("DAG") << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
                        << "DAGExecute.0:\t>>> Start send to executor";

    // string => <tuple<std::string, ContextID> => ExecutiveState::Ptr>::it
    for (auto it = requests.begin(); it != requests.end(); it = requests.upper_bound(it->first))
    {
        auto contractAddress = it->first;
        auto startT = utcTime();

        auto executor = m_scheduler->m_executorManager->dispatchExecutor(contractAddress);
        auto count = requests.count(it->first);
        // get all same address request,
        auto range = requests.equal_range(it->first);

        auto messages = std::make_shared<std::vector<protocol::ExecutionMessage::UniquePtr>>(count);
        auto iterators = std::vector<decltype(m_executiveStates)::iterator>(count);
        size_t i = 0;
        // traverse range, messageIt: <string => <tuple<std::string, ContextID> =>
        // ExecutiveState::Ptr>::it>::it
        for (auto messageIt = range.first; messageIt != range.second; ++messageIt)
        {
            SCHEDULER_LOG(TRACE) << "DAG message: " << messageIt->second->second->message.get()
                                 << " to: " << messageIt->first;
            messageIt->second->second->callStack.push(messageIt->second->second->currentSeq++);
            messages->at(i) = std::move(messageIt->second->second->message);
            iterators[i] = messageIt->second;

            ++i;
        }
        SCHEDULER_LOG(INFO) << LOG_BADGE("DAG") << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
                            << "DAGExecute.1:\t--> Send to executor\t"
                            << LOG_KV("contract", contractAddress)
                            << LOG_KV("txNum", messages->size());
        auto prepareT = utcTime() - startT;
        startT = utcTime();
        executor->dagExecuteTransactions(*messages,
            [this, contractAddress, messages, startT, prepareT, iterators = std::move(iterators),
                totalCount, failed, callbackPtr](bcos::Error::UniquePtr error,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr> responseMessages) {
                SCHEDULER_LOG(INFO)
                    << LOG_BADGE("DAG") << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
                    << "DAGExecute.2:\t<-- Receive from executor\t"
                    << LOG_KV("contract", contractAddress) << LOG_KV("txNum", messages->size())
                    << LOG_KV("costT", utcTime() - startT) << LOG_KV("failed", *failed)
                    << LOG_KV("totalCount", *totalCount) << LOG_KV("blockNumber", number());
                if (error)
                {
                    ++(*failed);
                    SCHEDULER_LOG(ERROR)
                        << BLOCK_NUMBER(number()) << "DAG execute error: " << error->errorMessage()
                        << LOG_KV("failed", *failed) << LOG_KV("totalCount", *totalCount)
                        << LOG_KV("blockNumber", number());
                    if (error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                    {
                        triggerSwitch();
                    }
                }
                else if (messages->size() != responseMessages.size())
                {
                    ++(*failed);
                    SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number())
                                         << "DAG messages size and response size mismatch!";
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

                totalCount->fetch_sub(messages->size());
                // TODO: must wait more response
                if (*totalCount == 0)
                {
                    SCHEDULER_LOG(DEBUG)
                        << LOG_BADGE("DAG") << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
                        << "DAGExecute.3:\t<<< Joint all contract result\t"
                        << LOG_KV("costT", utcTime() - startT) << LOG_KV("failed", *failed)
                        << LOG_KV("totalCount", *totalCount) << LOG_KV("blockNumber", number());

                    if (*failed > 0)
                    {
                        (*callbackPtr)(BCOS_ERROR_UNIQUE_PTR(
                            SchedulerError::DAGError, "Execute dag with errors"));
                        return;
                    }

                    SCHEDULER_LOG(INFO)
                        << BLOCK_NUMBER(number()) << LOG_DESC("DAGExecute finish")
                        << LOG_KV("prepareT", prepareT) << LOG_KV("execT", (utcTime() - startT));
                    (*callbackPtr)(nullptr);
                }
            });
    }
}

void BlockExecutive::DMCExecute(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    if (!m_isRunning)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"),
            nullptr, m_isSysBlock);
        return;
    }

    // update DMC recorder for debugging
    m_dmcRecorder->nextDmcRound();

    auto lastT = utcTime();
    DMC_LOG(INFO) << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
                  << "DMCExecute.0:\t [+] Start\t\t\t" << LOG_KV("round", m_dmcRecorder->getRound())
                  << LOG_KV("checksum", m_dmcRecorder->getChecksum());

    // prepare all dmcExecutor
    serialPrepareExecutor();
    DMC_LOG(INFO) << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
                  << "DMCExecute.1:\t [-] PrepareExecutor finish\t"
                  << LOG_KV("round", m_dmcRecorder->getRound())
                  << LOG_KV("checksum", m_dmcRecorder->getChecksum())
                  << LOG_KV("cost", utcTime() - lastT);
    lastT = utcTime();

    // dump address for omp parallelization
    std::vector<std::string> contractAddress;
    contractAddress.reserve(m_dmcExecutors.size());
    for (auto it = m_dmcExecutors.begin(); it != m_dmcExecutors.end(); it++)
    {
        contractAddress.push_back(it->first);
    }
    auto batchStatus = std::make_shared<BatchStatus>();
    batchStatus->total = contractAddress.size();

    // if is empty block, just return
    if (contractAddress.size() == 0)
    {
        onDmcExecuteFinish(std::move(callback));
        return;
    }

    auto executorCallback = [this, lastT, batchStatus = std::move(batchStatus),
                                callback = std::move(callback)](
                                bcos::Error::UniquePtr error, DmcExecutor::Status status) {
        if (error || status == DmcExecutor::Status::ERROR)
        {
            batchStatus->error++;
            SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number()) << LOG_BADGE("DmcExecutor")
                                 << "dmcExecutor->go() with error"
                                 << LOG_KV("code", error ? error->errorCode() : -1)
                                 << LOG_KV("msg", error ? error.get()->errorMessage() : "null");
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

        if (!m_isRunning)
        {
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"),
                nullptr, m_isSysBlock);
            return;
        }

        // handle batch result(only one thread can get in here)
        DMC_LOG(INFO) << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
                      << "DMCExecute.5:\t <<< Joint all executor result\t"
                      << LOG_KV("round", m_dmcRecorder->getRound())
                      << LOG_KV("checksum", m_dmcRecorder->getChecksum())
                      << LOG_KV("sendChecksum", m_dmcRecorder->getSendChecksum())
                      << LOG_KV("receiveChecksum", m_dmcRecorder->getReceiveChecksum())
                      << LOG_KV("cost(after prepare finish)", utcTime() - lastT);

        if (batchStatus->error != 0)
        {
            DMC_LOG(ERROR) << BLOCK_NUMBER(number())
                           << "DMCExecute with errors: " << error->errorMessage();
            callback(std::move(error), nullptr, m_isSysBlock);
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

    DMC_LOG(INFO) << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
                  << "DMCExecute.2:\t >>> Start send to executors\t"
                  << LOG_KV("round", m_dmcRecorder->getRound())
                  << LOG_KV("checksum", m_dmcRecorder->getChecksum())
                  << LOG_KV("cost", utcTime() - lastT)
                  << LOG_KV("contractNum", contractAddress.size());

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
    auto dmcChecksum = m_dmcRecorder->dumpAndClearChecksum();
    if (m_staticCall)
    {
        DMC_LOG(TRACE) << LOG_BADGE("Stat") << "DMCExecute.6:"
                       << "\t " << LOG_BADGE("DMCRecorder") << " DMCExecute for call finished "
                       << LOG_KV("blockNumber", number()) << LOG_KV("checksum", dmcChecksum);
    }
    else
    {
        DMC_LOG(INFO) << LOG_BADGE("Stat") << "DMCExecute.6:"
                      << "\t " << LOG_BADGE("DMCRecorder")
                      << " DMCExecute for transaction finished " << LOG_KV("blockNumber", number())
                      << LOG_KV("checksum", dmcChecksum);
    }

    onExecuteFinish(std::move(callback));
}

void BlockExecutive::onExecuteFinish(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    auto now = std::chrono::system_clock::now();
    m_executeElapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_currentTimePoint);
    m_currentTimePoint = now;


    if (m_staticCall)
    {
        // Set result to m_block
        for (size_t i = 0; i < m_executiveResults.size(); i++)
        {
            if (i < m_block->receiptsSize())
            {
                // bugfix: force update receipt of last executeBlock() remaining
                m_block->setReceipt(i, m_executiveResults[i].receipt);
            }
            else
            {
                m_block->appendReceipt(m_executiveResults[i].receipt);
            }
        }
        callback(nullptr, nullptr, m_isSysBlock);
    }
    else
    {
        // All Transaction finished, get hash
        batchGetHashes([this, callback = std::move(callback)](
                           Error::UniquePtr error, crypto::HashType hash) {
            if (!m_isRunning)
            {
                callback(
                    BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"),
                    nullptr, m_isSysBlock);
                return;
            }

            if (error)
            {
                SCHEDULER_LOG(ERROR) << "batchGetHashes error: " << error->errorMessage();
                callback(std::move(error), nullptr, m_isSysBlock);
                return;
            }

            m_hashElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now() - m_currentTimePoint);

            // Set result to m_block
            for (size_t i = 0; i < m_executiveResults.size(); i++)
            {
                if (i < m_block->receiptsSize())
                {
                    // bugfix: force update receipt of last executeBlock() remaining
                    m_block->setReceipt(i, m_executiveResults[i].receipt);
                }
                else
                {
                    m_block->appendReceipt(m_executiveResults[i].receipt);
                }
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
        if (!m_isRunning)
        {
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"));
            return;
        }

        if (status.failed > 0)
        {
            auto message = "Next block:" + boost::lexical_cast<std::string>(number()) +
                           " with errors! " + boost::lexical_cast<std::string>(status.failed);
            SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number()) << message;

            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::BatchError, std::move(message)));
            return;
        }

        callback(nullptr);
    };

    // for (auto& it : *(m_scheduler->m_executorManager))
    m_scheduler->m_executorManager->forEachExecutor(
        [this, status](
            std::string, bcos::executor::ParallelTransactionExecutorInterface::Ptr executor) {
            auto blockHeader = m_block->blockHeaderConst();
            executor->nextBlockHeader(
                m_schedulerTermId, blockHeader, [this, status](bcos::Error::Ptr&& error) {
                    {
                        WriteGuard lock(status->x_lock);
                        if (error)
                        {
                            SCHEDULER_LOG(ERROR)
                                << BLOCK_NUMBER(number()) << "Next block executor error!"
                                << error->errorMessage();
                            ++status->failed;

                            if (error->errorCode() ==
                                bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                            {
                                triggerSwitch();
                            }
                        }
                        else
                        {
                            ++status->success;
                        }

                        if (status->success + status->failed < status->total)
                        {
                            return;
                        }
                    }
                    status->checkAndCommit(*status);
                });
        });
}

void BlockExecutive::batchGetHashes(
    std::function<void(bcos::Error::UniquePtr, bcos::crypto::HashType)> callback)
{
    auto totalHash = std::make_shared<h256>();

    auto status = std::make_shared<CommitStatus>();
    status->total = m_scheduler->m_executorManager->size();  // all executors
    status->checkAndCommit = [this, totalHash, callback = std::move(callback)](
                                 const CommitStatus& status) {
        if (!m_isRunning)
        {
            callback(
                BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"), {});
            return;
        }

        if (status.failed > 0)
        {
            auto message = "batchGetHashes" + boost::lexical_cast<std::string>(number()) +
                           " with errors! " + boost::lexical_cast<std::string>(status.failed);
            SCHEDULER_LOG(WARNING) << BLOCK_NUMBER(number()) << message;

            callback(
                BCOS_ERROR_UNIQUE_PTR(SchedulerError::BatchError, std::move(message)), h256(0));
            return;
        }

        callback(nullptr, std::move(*totalHash));
    };

    // for (auto& it : *(m_scheduler->m_executorManager))

    m_scheduler->m_executorManager->forEachExecutor(
        [this, status, totalHash](
            std::string, bcos::executor::ParallelTransactionExecutorInterface::Ptr executor) {
            executor->getHash(number(), [status, totalHash](
                                            bcos::Error::Ptr&& error, crypto::HashType&& hash) {
                {
                    WriteGuard lock(status->x_lock);
                    if (error)
                    {
                        SCHEDULER_LOG(ERROR) << "Commit executor error!" << error->errorMessage();
                        ++status->failed;
                    }
                    else
                    {
                        ++status->success;
                        SCHEDULER_LOG(DEBUG)
                            << "GetHash executor success, success: " << status->success;

                        *totalHash ^= hash;
                    }

                    if (status->success + status->failed < status->total)
                    {
                        return;
                    }
                }
                status->checkAndCommit(*status);
            });
        });
}

void BlockExecutive::batchBlockCommit(std::function<void(Error::UniquePtr)> callback)
{
    auto status = std::make_shared<CommitStatus>();
    status->total = 1 + m_scheduler->m_executorManager->size();  // self + all executors
    status->checkAndCommit = [this, callback = std::move(callback)](const CommitStatus& status) {
        if (!m_isRunning)
        {
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"));
            return;
        }

        if (status.failed > 0)
        {
            auto message = "Commit block:" + boost::lexical_cast<std::string>(number()) +
                           " with errors! " + boost::lexical_cast<std::string>(status.failed);
            SCHEDULER_LOG(WARNING) << BLOCK_NUMBER(number()) << message;

            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::CommitError, std::move(message)));
            return;
        }

        callback(nullptr);
    };

    bcos::protocol::TwoPCParams params;
    params.number = number();
    m_scheduler->m_storage->asyncCommit(
        params, [status, this](Error::Ptr&& error, uint64_t commitTS) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << BLOCK_NUMBER(number()) << "Commit storage error!" << error->errorMessage();

                ++status->failed;
                status->checkAndCommit(*status);
                return;
            }
            else
            {
                ++status->success;
            }

            bcos::protocol::TwoPCParams executorParams;
            executorParams.number = number();
            executorParams.timestamp = commitTS;
            tbb::parallel_for_each(m_scheduler->m_executorManager->begin(),
                m_scheduler->m_executorManager->end(), [&](auto const& executorIt) {
                    SCHEDULER_LOG(TRACE) << "Commit executor for block " << executorParams.number;

                    executorIt->commit(executorParams, [this, status](bcos::Error::Ptr&& error) {
                        {
                            WriteGuard lock(status->x_lock);
                            if (error)
                            {
                                SCHEDULER_LOG(ERROR)
                                    << BLOCK_NUMBER(number()) << "Commit executor error!"
                                    << error->errorMessage();
                                ++status->failed;

                                if (error->errorCode() ==
                                    bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                                {
                                    triggerSwitch();
                                }
                            }
                            else
                            {
                                ++status->success;
                                SCHEDULER_LOG(DEBUG)
                                    << BLOCK_NUMBER(number())
                                    << "Commit executor success, success: " << status->success;
                            }

                            if (status->success + status->failed < status->total)
                            {
                                return;
                            }
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
        if (!m_isRunning)
        {
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"));
            return;
        }

        if (status.failed > 0)
        {
            auto message = "Rollback block:" + boost::lexical_cast<std::string>(number()) +
                           " with errors! " + boost::lexical_cast<std::string>(status.failed);
            SCHEDULER_LOG(WARNING) << BLOCK_NUMBER(number()) << message;

            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::RollbackError, std::move(message)));
            return;
        }

        callback(nullptr);
    };

    bcos::protocol::TwoPCParams params;
    params.number = number();
    m_scheduler->m_storage->asyncRollback(
        params, [number = params.number, status](Error::Ptr&& error) {
            {
                WriteGuard lock(status->x_lock);
                if (error)
                {
                    SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number) << "Rollback storage error!"
                                         << error->errorMessage();

                    ++status->failed;
                }
                else
                {
                    ++status->success;
                }

                if (status->success + status->failed < status->total)
                {
                    return;
                }
            }
            status->checkAndCommit(*status);
        });

    // for (auto& it : *(m_scheduler->m_executorManager))
    m_scheduler->m_executorManager->forEachExecutor(
        [this, status](
            std::string, bcos::executor::ParallelTransactionExecutorInterface::Ptr executor) {
            bcos::protocol::TwoPCParams executorParams;
            executorParams.number = number();
            executor->rollback(executorParams, [this, status](bcos::Error::Ptr&& error) {
                {
                    WriteGuard lock(status->x_lock);
                    if (error)
                    {
                        if (error->errorCode() ==
                            bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                        {
                            SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number())
                                                << "Rollback a restarted executor. Ignore."
                                                << error->errorMessage();
                            ++status->success;
                            triggerSwitch();
                        }
                        else
                        {
                            SCHEDULER_LOG(ERROR)
                                << BLOCK_NUMBER(number()) << "Rollback executor error!"
                                << error->errorMessage();
                            ++status->failed;
                        }
                    }
                    else
                    {
                        ++status->success;
                    }

                    if (status->success + status->failed < status->total)
                    {
                        return;
                    }
                }
                status->checkAndCommit(*status);
            });
        });
}


DmcExecutor::Ptr BlockExecutive::registerAndGetDmcExecutor(std::string contractAddress)
{
    {
        bcos::ReadGuard l(x_dmcExecutorLock);
        auto dmcExecutorIt = m_dmcExecutors.find(contractAddress);
        if (dmcExecutorIt != m_dmcExecutors.end())
        {
            return dmcExecutorIt->second;
        }
    }
    {
        bcos::WriteGuard lock(x_dmcExecutorLock);
        auto dmcExecutorIt = m_dmcExecutors.find(contractAddress);
        if (dmcExecutorIt != m_dmcExecutors.end())
        {
            return dmcExecutorIt->second;
        }
        auto executor = m_scheduler->executorManager()->dispatchExecutor(contractAddress);
        auto executorInfo = m_scheduler->executorManager()->getExecutorInfo(contractAddress);

        if (!m_dmcRecorder)
        {
            m_dmcRecorder = std::make_shared<DmcStepRecorder>();
        }

        auto dmcExecutor = std::make_shared<DmcExecutor>(executorInfo->name, contractAddress,
            m_block, executor, m_keyLocks, m_scheduler->m_hashImpl, m_dmcRecorder);
        m_dmcExecutors.emplace(contractAddress, dmcExecutor);

        // register functions
        dmcExecutor->setSchedulerOutHandler(
            [this](ExecutiveState::Ptr executiveState) { scheduleExecutive(executiveState); });

        dmcExecutor->setOnTxFinishedHandler(
            [this](bcos::protocol::ExecutionMessage::UniquePtr output) {
                onTxFinish(std::move(output));
            });
        dmcExecutor->setOnNeedSwitchEventHandler([this]() { triggerSwitch(); });

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
    if (bcos::precompiled::c_systemTxsAddress.count({output->from().data(), output->from().size()}))
    {
        txGasUsed = 0;
    }
    m_gasUsed += txGasUsed;
    auto receipt = m_scheduler->m_blockFactory->receiptFactory()->createReceipt(txGasUsed,
        output->newEVMContractAddress(),
        std::make_shared<std::vector<bcos::protocol::LogEntry>>(output->takeLogEntries()),
        output->status(), output->takeData(), m_block->blockHeaderConst()->number());
    // write receipt in results
    m_executiveResults[output->contextID() - m_startContextID].receipt = receipt;
    SCHEDULER_LOG(TRACE) << " 6.GenReceipt:\t [^^] " << output->toString()
                         << " -> contextID:" << output->contextID() - m_startContextID
                         << ", receipt: " << receipt->hash() << ", gasUsed: " << receipt->gasUsed()
                         << ", version: " << receipt->version()
                         << ", status: " << receipt->status();
}


void BlockExecutive::serialPrepareExecutor()
{
    // Notice:
    // For the same DMC lock priority
    // m_dmcExecutors must be prepared in contractAddress less<> serial order

    /// Handle normal message
    bool hasScheduleOutMessage;
    do
    {
        hasScheduleOutMessage = false;

        // dump current DmcExecutor (m_dmcExecutors may be modified during traversing)
        std::set<std::string, std::less<>> currentExecutors;
        for (auto it = m_dmcExecutors.begin(); it != m_dmcExecutors.end(); it++)
        {
            it->second->releaseOutdatedLock();  // release last round's lock
            currentExecutors.insert(it->first);
        }

        // for each current DmcExecutor
        for (auto& address : currentExecutors)
        {
            DMC_LOG(TRACE) << " 0.Pre-DmcExecutor: \t----------------- addr:" << address
                           << " | number:" << m_block->blockHeaderConst()->number()
                           << " -----------------";
            hasScheduleOutMessage |=
                m_dmcExecutors[address]->prepare();  // may generate new contract in m_dmcExecutors
        }

        // must all schedule out message has been handled.
    } while (hasScheduleOutMessage);


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
        DMC_LOG(TRACE) << " 3.UnlockPrepare: \t |---------------- addr:" << address
                       << " | number:" << std::to_string(m_block->blockHeaderConst()->number())
                       << " ----------------|";

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
            DMC_LOG(TRACE) << " --detect--revert-- " << address << " | "
                           << m_block->blockHeaderConst()->number() << " -----------------";
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
