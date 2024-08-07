#include "BlockExecutive.h"
#include "Common.h"
#include "DmcExecutor.h"
#include "SchedulerImpl.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <bcos-utilities/Error.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for_each.h>
#include <boost/algorithm/hex.hpp>
#include <boost/archive/basic_archive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/asio/defer.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <utility>

#ifdef USE_TCMALLOC
#include "gperftools/malloc_extension.h"
#endif

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
    m_blockFactory(std::move(_blockFactory)),
    m_txPool(std::move(_txPool)),
    m_staticCall(staticCall)
{
    m_hashImpl = m_blockFactory->cryptoSuite()->hashImpl();
    m_blockHeader = m_block->blockHeaderConst();
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

    // prepare all executors
    if (needPrepareExecutor())
    {
        // prepare DMC executor
        serialPrepareExecutor();
    }

    m_hasPrepared = true;

    SCHEDULER_LOG(INFO) << METRIC << LOG_BADGE("BlockTrace") << BLOCK_NUMBER(number())
                        << "preExeBlock success"
                        << LOG_KV("blockHeader.timestamp", blockHeader()->timestamp())
                        << LOG_KV("metaTxCount", m_block->transactionsMetaDataSize())
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
        if (bcos::precompiled::c_systemTxsAddress.contains(toAddress))
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
            if (!m_staticCall && isSysContractDeploy(number()) &&
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
    auto toAddress = tx->to();
    if (precompiled::c_systemTxsAddress.contains(toAddress))
    {
        message->setGasAvailable(TRANSACTION_GAS);
    }
    message->setData(tx->input().toBytes());
    message->setStaticCall(m_staticCall);

    if (message->create())
    {
        message->setABI(std::string(tx->abi()));
    }

    // set value
    message->setValue(std::string(tx->value()));
    message->setGasLimit(tx->gasLimit());
    message->setGasPrice(m_gasPrice);
    message->setMaxFeePerGas(std::string(tx->maxFeePerGas()));
    message->setMaxPriorityFeePerGas(std::string(tx->maxPriorityFeePerGas()));
    message->setEffectiveGasPrice(m_gasPrice);

    return message;
}

void BlockExecutive::buildExecutivesFromMetaData()
{
    SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number())
                         << "BlockExecutive prepare: buildExecutivesFromMetaData"
                         << LOG_KV("tx meta count", m_block->transactionsMetaDataSize());

    m_blockTxs = fetchBlockTxsFromTxPool(m_block, m_txPool);  // no need to async
    m_executiveResults.resize(m_block->transactionsMetaDataSize());
    std::vector<std::tuple<std::string, protocol::ExecutionMessage::UniquePtr, bool>> results(
        m_block->transactionsMetaDataSize());

    auto blockImpl = std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(m_block);
    if (m_blockTxs)
    {
        tbb::parallel_for(tbb::blocked_range<size_t>(0U, m_block->transactionsMetaDataSize()),
            [&](auto const& range) {
                for (auto i = range.begin(); i < range.end(); ++i)
                {
                    auto metaData = blockImpl->transactionMetaDataImpl(i);
                    // if (metaData)
                    {
                        m_executiveResults[i].transactionHash = metaData.hash();
                        m_executiveResults[i].source = metaData.source();
                    }
                    auto contextID = i + m_startContextID;

                    auto& [toAddress, message, enableDAG] = results[i];
                    message = buildMessage(contextID, (*m_blockTxs)[i]);
                    // recoder tx version
                    m_executiveResults[i].version = (*m_blockTxs)[i]->version();
                    toAddress = {message->to().data(), message->to().size()};
                    enableDAG = metaData.attribute() & bcos::protocol::Transaction::Attribute::DAG;
                }
            });
    }
    else
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(
            SchedulerError::BuildBlockError, "buildExecutivesFromMetaData: fetchBlockTxs error"));
    }

    for (auto& it : results)
    {
        auto& [to, message, enableDAG] = it;
        if (message)
        {
            m_hasDAG = m_hasDAG || enableDAG;
            saveMessage(std::move(to), std::move(message), enableDAG);
        }
    }
}

void BlockExecutive::buildExecutivesFromNormalTransaction()
{
    SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number())
                         << "BlockExecutive prepare: buildExecutivesFromNormalTransaction"
                         << LOG_KV("block number", number())
                         << LOG_KV("txCount", m_block->transactionsSize());

    m_executiveResults.resize(m_block->transactionsSize());
    std::vector<std::tuple<std::string, protocol::ExecutionMessage::UniquePtr, bool>> results(
        m_block->transactionsSize());

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0U, m_block->transactionsSize(), 256), [&](auto const& range) {
            for (auto i = range.begin(); i < range.end(); ++i)
            {
                auto tx = m_block->transaction(i);
                m_executiveResults[i].transactionHash = tx->hash();
                m_executiveResults[i].version = tx->version();

                auto contextID = i + m_startContextID;
                auto& [to, message, enableDAG] = results[i];
                message = buildMessage(contextID, tx);
                to = {message->to().data(), message->to().size()};
                enableDAG = tx->attribute() & bcos::protocol::Transaction::Attribute::DAG;
            }
        });

    for (auto& it : results)
    {
        auto& [to, message, enableDAG] = it;
        if (message)
        {
            m_hasDAG = m_hasDAG || enableDAG;
            saveMessage(std::move(to), std::move(message), enableDAG);
        }
    }
}

bcos::protocol::ConstTransactionsPtr BlockExecutive::fetchBlockTxsFromTxPool(
    bcos::protocol::Block::Ptr block, bcos::txpool::TxPoolInterface::Ptr txPool)
{
    SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number()) << "BlockExecutive prepare: fillBlock start"
                         << LOG_KV("txNum", block->transactionsMetaDataSize());
    bcos::protocol::ConstTransactionsPtr txs = nullptr;
    auto lastT = utcTime();
    if (txPool != nullptr)
    {
        // Get tx hash list
        auto txHashes = std::make_shared<protocol::HashList>();
        for (size_t i = 0; i < block->transactionsMetaDataSize(); ++i)
        {
            txHashes->emplace_back(block->transactionMetaData(i)->hash());
        }
        if (c_fileLogLevel <= TRACE) [[unlikely]]
        {
            for (auto const& tx : *txHashes)
            {
                SCHEDULER_LOG(TRACE) << "fetch: " << tx.abridged();
            }
        }
        std::shared_ptr<std::promise<bcos::protocol::ConstTransactionsPtr>> txsPromise =
            std::make_shared<std::promise<bcos::protocol::ConstTransactionsPtr>>();
        txPool->asyncFillBlock(
            txHashes, [txsPromise](Error::Ptr error, bcos::protocol::ConstTransactionsPtr txs) {
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
            SCHEDULER_LOG(WARNING)
                << BLOCK_NUMBER(number()) << "BlockExecutive prepare: fillBlock timeout/failed"
                << LOG_KV("txNum", block->transactionsMetaDataSize())
                << LOG_KV("cost", utcTime() - lastT) << LOG_KV("fetchNum", txs ? txs->size() : 0);
            return nullptr;
        }
        txs = future.get();
        txsPromise = nullptr;
    }
    SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number()) << "BlockExecutive prepare: fillBlock end"
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
                SCHEDULER_LOG(WARNING)
                    << BLOCK_NUMBER(number()) << "Next block with failed!" << error->errorMessage();
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             SchedulerError::NextBlockError, "Next block failed!", *error),
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
                        SCHEDULER_LOG(WARNING)
                            << BLOCK_NUMBER(number()) << "DAG execute block with failed!"
                            << error->errorMessage();
                        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                     SchedulerError::DAGError, "DAG execute failed!", *error),
                            nullptr, m_isSysBlock);
                        return;
                    }
                    auto blockHeader = m_block->blockHeader();
                    blockHeader->calculateHash(*m_hashImpl);
                    SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << LOG_DESC("DAGExecute success")
                                        << LOG_KV("createMsgT", createMsgT)
                                        << LOG_KV("dagExecuteT", (utcTime() - startT))
                                        << LOG_KV("hash", blockHeader->hash().abridged());

                    SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << LOG_BADGE("BlockTrace")
                                        << LOG_DESC("DMCExecute begin after DAGExecute");
                    DMCExecute(std::move(callback));
                });
            }
            else
            {
                SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << LOG_BADGE("BlockTrace")
                                    << LOG_DESC("DMCExecute begin without DAGExecute");
                DMCExecute(std::move(callback));
            }
        });
    }
    else
    {
        SCHEDULER_LOG(TRACE) << BLOCK_NUMBER(number()) << LOG_BADGE("BlockTrace")
                             << LOG_DESC("DMCExecute begin for call");
        DMCExecute(std::move(callback));
    }
}

void BlockExecutive::asyncCommit(std::function<void(Error::UniquePtr)> callback)
{
    task::wait([this](decltype(callback) callback) -> task::Task<void> {
        ledger::Features features;
        co_await features.readFromStorage(*m_scheduler->m_storage, m_blockHeader->number());
        auto stateStorage = std::make_shared<storage::StateStorage>(m_scheduler->m_storage,
            features.get(ledger::Features::Flag::bugfix_set_row_with_dirty_flag));

        m_currentTimePoint = std::chrono::system_clock::now();
        SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number()) << LOG_DESC("BlockExecutive commit block");

        m_scheduler->m_ledger->asyncPrewriteBlock(
            stateStorage, m_blockTxs, m_block,
            [this, stateStorage, callback = std::move(callback)](
                std::string primiaryKey, Error::Ptr&& error) mutable {
                if (error)
                {
                    SCHEDULER_LOG(ERROR) << "Prewrite block error!" << error->errorMessage();

                    if (error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                    {
                        triggerSwitch();
                    }

                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(SchedulerError::PrewriteBlockError,
                        "Prewrite block failed: " + error->errorMessage(), *error));

                    return;
                }

                auto status = std::make_shared<CommitStatus>();
                // self + ledger(txs receipts) + executors = 1 + 1 + executors
                status->total = 2 + getExecutorSize();
                status->checkAndCommit = [this, callback](const CommitStatus& status) {
                    if (!m_isRunning)
                    {
                        callback(BCOS_ERROR_UNIQUE_PTR(
                            SchedulerError::Stopped, "BlockExecutive is stopped"));
                        return;
                    }

                    if (status.failed > 0)
                    {
                        std::string errorMessage = "Prepare with errors, begin rollback, status: " +
                                                   boost::lexical_cast<std::string>(status.failed);
                        SCHEDULER_LOG(WARNING) << BLOCK_NUMBER(number()) << errorMessage;
                        batchBlockRollback(status.startTS, [this, callback, errorMessage](
                                                               Error::UniquePtr&& error) {
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
                                callback(BCOS_ERROR_UNIQUE_PTR(
                                    SchedulerError::CommitError, errorMessage));
                                return;
                            }
                        });

                        return;
                    }

                    SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number()) << "batchCommitBlock begin";
                    batchBlockCommit(status.startTS, [this, callback](Error::UniquePtr&& error) {
                        if (error)
                        {
                            SCHEDULER_LOG(WARNING)
                                << BLOCK_NUMBER(number()) << "Commit block to storage failed!"
                                << error->errorMessage();

                            if (error->errorCode() ==
                                bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                            {
                                triggerSwitch();
                            }

                            // FATAL ERROR, NEED MANUAL FIX!
                            callback(std::move(error));

                            return;
                        }

                        m_commitElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now() - m_currentTimePoint);
                        SCHEDULER_LOG(DEBUG)
                            << BLOCK_NUMBER(number()) << "CommitBlock: "
                            << "success, execute elapsed: " << m_executeElapsed.count()
                            << "ms hash elapsed: " << m_hashElapsed.count()
                            << "ms commit elapsed: " << m_commitElapsed.count() << "ms";

                        callback(nullptr);
                    });
                };

                bcos::protocol::TwoPCParams params;
                params.number = number();
                params.primaryKey = std::move(primiaryKey);
                m_scheduler->m_storage->asyncPrepare(params, *stateStorage,
                    [status, this, callback](Error::Ptr&& error, uint64_t startTimeStamp,
                        const std::string& primaryKey) {
                        if (error)
                        {
                            {
                                WriteGuard lock(status->x_lock);
                                ++status->failed;
                            }
                            SCHEDULER_LOG(WARNING) << BLOCK_NUMBER(number())
                                                   << "scheduler asyncPrepare storage error: "
                                                   << error->errorMessage();
                            callback(BCOS_ERROR_UNIQUE_PTR(error->errorCode(),
                                "asyncPrepare block error: " + error->errorMessage()));
                            return;
                        }
                        else
                        {
                            WriteGuard lock(status->x_lock);
                            ++status->success;
                        }

                        SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number())
                                             << "primary prepare finished, call executor prepare"
                                             << LOG_KV("startTimeStamp", startTimeStamp)
                                             << LOG_KV("executors", getExecutorSize())
                                             << LOG_KV("success", status->success)
                                             << LOG_KV("failed", status->failed);
                        bcos::protocol::TwoPCParams executorParams;
                        executorParams.number = number();
                        executorParams.primaryKey = primaryKey;
                        executorParams.timestamp = startTimeStamp;
                        status->startTS = startTimeStamp;
                        for (const auto& executorIt : *(m_scheduler->m_executorManager))
                        {
                            executorIt->prepare(executorParams, [this, status](Error::Ptr&& error) {
                                {
                                    WriteGuard lock(status->x_lock);
                                    if (error)
                                    {
                                        ++status->failed;
                                        SCHEDULER_LOG(ERROR)
                                            << BLOCK_NUMBER(number())
                                            << "asyncPrepare executor failed: "
                                            << LOG_KV("code", error->errorCode()) << error->what();

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
                // write transactions and receipts in another DB txn
                auto err = m_scheduler->m_ledger->storeTransactionsAndReceipts(
                    m_blockTxs, std::const_pointer_cast<const bcos::protocol::Block>(m_block));
                {
                    WriteGuard lock(status->x_lock);
                    if (err)
                    {
                        ++status->failed;
                        SCHEDULER_LOG(ERROR)
                            << BLOCK_NUMBER(number()) << "write txs and receipts failed: "
                            << LOG_KV("message", err->errorMessage());
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
            },
            false);
    }(std::move(callback)));
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
    auto blockHash = blockHeader()->hash();
    auto number = blockHeader()->number();
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
    notifier(number, results, [_callback, number, blockHash, txsSize](Error::Ptr _error) {
        if (_callback)
        {
            _callback(_error);
        }
        if (_error == nullptr)
        {
            SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number) << LOG_DESC("notify block result success")
                                << LOG_KV("hash", blockHash.abridged())
                                << LOG_KV("txsSize", txsSize);
            return;
        }
        SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number) << LOG_DESC("notify block result failed")
                            << LOG_KV("code", _error->errorCode())
                            << LOG_KV("msg", _error->errorMessage());
    });
}

void BlockExecutive::saveMessage(
    std::string address, protocol::ExecutionMessage::UniquePtr message, bool withDAG)
{
    registerAndGetDmcExecutor(std::move(address))->submit(std::move(message), withDAG);
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
                        << LOG_BADGE("BlockTrace") << "DAGExecute.0:\t>>> Start send to executor";

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
                    tbb::parallel_for(tbb::blocked_range<size_t>(0U, responseMessages.size()),
                        [&responseMessages, &iterators](auto const& range) {
                            for (auto j = range.begin(); j < range.end(); ++j)
                            {
                                assert(responseMessages[j]);
                                iterators[j]->second->message = std::move(responseMessages[j]);
                            }
                        });
                }

                if (totalCount->fetch_sub(messages->size()) == messages->size())
                {
                    // only one thread can get in this field
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
                        << BLOCK_NUMBER(number()) << LOG_BADGE("BlockTrace")
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
    try
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
        DMC_LOG(DEBUG) << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
                       << "DMCExecute.0:\t [+] Start\t\t\t"
                       << LOG_KV("round", m_dmcRecorder->getRound())
                       << LOG_KV("checksum", m_dmcRecorder->getChecksum());

        // prepare all dmcExecutor
        serialPrepareExecutor();
        DMC_LOG(DEBUG) << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
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
                batchStatus->errorMessage = error.get()->errorMessage();
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
                callback(
                    BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"),
                    nullptr, m_isSysBlock);
                return;
            }

            // handle batch result(only one thread can get in here)
            DMC_LOG(DEBUG) << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
                           << "DMCExecute.5:\t <<< Join all executor result\t"
                           << LOG_KV("round", m_dmcRecorder->getRound())
                           << LOG_KV("checksum", m_dmcRecorder->getChecksum())
                           << LOG_KV("sendChecksum", m_dmcRecorder->getSendChecksum())
                           << LOG_KV("receiveChecksum", m_dmcRecorder->getReceiveChecksum())
                           << LOG_KV("cost(after prepare finish)", utcTime() - lastT);

            if (batchStatus->error != 0)
            {
                DMC_LOG(ERROR) << BLOCK_NUMBER(number())
                               << "DMCExecute with errors: " << batchStatus->errorMessage;
                callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::DMCError, batchStatus->errorMessage),
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

        DMC_LOG(DEBUG) << LOG_BADGE("Stat") << BLOCK_NUMBER(number())
                       << "DMCExecute.2:\t >>> Start send to executors\t"
                       << LOG_KV("round", m_dmcRecorder->getRound())
                       << LOG_KV("checksum", m_dmcRecorder->getChecksum())
                       << LOG_KV("cost", utcTime() - lastT)
                       << LOG_KV("contractNum", contractAddress.size());

        // for each dmcExecutor
        // Use isolate task_arena to avoid error
        tbb::this_task_arena::isolate([this, &contractAddress, &executorCallback] {
            tbb::parallel_for(tbb::blocked_range<size_t>(0U, contractAddress.size()),
                [this, &contractAddress, &executorCallback](auto const& range) {
                    for (auto i = range.begin(); i < range.end(); ++i)
                    {
                        auto dmcExecutor = m_dmcExecutors[contractAddress[i]];
                        dmcExecutor->go(executorCallback);
                    }
                });
        });
    }
    catch (bcos::Error& e)
    {
        DMC_LOG(WARNING) << "DMCExecute exception: " << LOG_KV("code", e.errorCode())
                         << LOG_KV("message", e.errorMessage());
        callback(BCOS_ERROR_UNIQUE_PTR(e.errorCode(), e.errorMessage()), nullptr, m_isSysBlock);
    }
    catch (std::exception& e)
    {
        DMC_LOG(WARNING) << "DMCExecute exception " << boost::diagnostic_information(e);
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::UnknownError, "DMCExecute exception"),
            nullptr, m_isSysBlock);
    }
    catch (...)
    {
        DMC_LOG(WARNING) << "DMCExecute exception. ";
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::UnknownError, "DMCExecute exception"),
            nullptr, m_isSysBlock);
    }
}

void BlockExecutive::onDmcExecuteFinish(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    auto dmcChecksum = m_dmcRecorder->dumpAndClearChecksum();
    if (m_staticCall)
    {
        DMC_LOG(TRACE) << LOG_BADGE("Stat") << "DMCExecute.6:" << "\t " << LOG_BADGE("DMCRecorder")
                       << " DMCExecute for call finished " << LOG_KV("blockNumber", number())
                       << LOG_KV("checksum", dmcChecksum);
    }
    else
    {
        DMC_LOG(INFO) << LOG_BADGE("Stat") << "DMCExecute.6:" << "\t " << LOG_BADGE("DMCRecorder")
                      << " DMCExecute for transaction finished " << LOG_KV("blockNumber", number())
                      << LOG_KV("checksum", dmcChecksum);

        DMC_LOG(INFO) << BLOCK_NUMBER(number()) << LOG_BADGE("BlockTrace")
                      << LOG_BADGE("DMCRecorder") << " DMCExecute for transaction finished "
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
        for (auto dmcExecutor : m_dmcExecutors)
        {
            if (dmcExecutor.second->hasContractTableChanged())
            {
                // if contract table has changed in execution,
                // we need to break block pipeline and execute/commit block one by one
                SCHEDULER_LOG(DEBUG)
                    << "Break block pipeline" << LOG_KV("contract", dmcExecutor.first);
                m_isSysBlock.store(true);
            }
        }

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
            executedBlockHeader->setGasUsed(m_gasUsed.load());
            executedBlockHeader->setTxsRoot(m_block->calculateTransactionRoot(*m_hashImpl));
            executedBlockHeader->setReceiptsRoot(m_block->calculateReceiptRoot(*m_hashImpl));
            executedBlockHeader->calculateHash(*m_hashImpl);

            m_result = executedBlockHeader;
            callback(nullptr, m_result, m_isSysBlock);
        });
    }
}

void BlockExecutive::batchNextBlock(std::function<void(Error::UniquePtr)> callback)
{
    auto startTime = utcTime();
    auto status = std::make_shared<CommitStatus>();
    status->total = getExecutorSize();
    status->checkAndCommit = [this, startTime, callback = std::move(callback)](
                                 const CommitStatus& status) {
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

        SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << LOG_BADGE("BlockTrace")
                            << "NextBlock success" << LOG_KV("executorNum", getExecutorSize())
                            << LOG_KV("timeCost", utcTime() - startTime);
        callback(nullptr);
    };
    SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << LOG_BADGE("BlockTrace") << "NextBlock request"
                        << LOG_KV("executorNum", getExecutorSize());

    // for (auto& it : *(m_scheduler->m_executorManager))
    forEachExecutor([this, status](std::string,
                        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor) {
        auto blockHeader = m_blockHeader;
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
    status->total = getExecutorSize();  // all executors
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

    forEachExecutor([this, status, totalHash](std::string,
                        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor) {
        executor->getHash(
            number(), [status, totalHash](bcos::Error::Ptr&& error, crypto::HashType&& hash) {
                {
                    WriteGuard lock(status->x_lock);
                    if (error)
                    {
                        SCHEDULER_LOG(ERROR) << "GetHash error!" << error->errorMessage();
                        ++status->failed;
                    }
                    else
                    {
                        ++status->success;
                        SCHEDULER_LOG(DEBUG) << "GetHash success, success: " << status->success;

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

void BlockExecutive::batchBlockCommit(
    uint64_t rollbackVersion, std::function<void(Error::UniquePtr)> callback)
{
    auto status = std::make_shared<CommitStatus>();
    status->total = 1 + getExecutorSize();  // self + all executors
    status->checkAndCommit = [this, callback](const CommitStatus& status) {
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
#ifdef USE_TCMALLOC
        // SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number()) << "TCMalloc release";
        // MallocExtension::instance()->ReleaseFreeMemory();
#endif
    };

    bcos::protocol::TwoPCParams params;
    params.number = number();
    params.timestamp = 0;
    m_scheduler->m_storage->asyncCommit(params, [rollbackVersion, status, this, callback](
                                                    Error::Ptr&& error, uint64_t commitTS) {
        if (error)
        {
// #define COMMIT_FAILED_NEED_ROLLBACK
#ifdef COMMIT_FAILED_NEED_ROLLBACK
            SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number())
                                 << "Commit node storage error! need rollback"
                                 << error->errorMessage();

            batchBlockRollback(rollbackVersion, [this, callback](Error::UniquePtr&& error) {
                if (error)
                {
                    SCHEDULER_LOG(ERROR)
                        << BLOCK_NUMBER(number())
                        << "Rollback storage(for commit scheduler storage error) failed!"
                        << LOG_KV("number", number()) << " " << error->errorMessage();
                    // FATAL ERROR, NEED MANUAL FIX!

                    callback(std::move(error));
                    return;
                }
                else
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(
                        SchedulerError::CommitError, "Commit scheduler storage error, rollback"));
                    return;
                }
            });
#else
                SCHEDULER_LOG(WARNING)
                        << BLOCK_NUMBER(number())
                        << "Commit scheduler storage error, just return with no rollback" <<  LOG_KV("message", error->errorMessage())
                        << LOG_KV("rollbackVersion", rollbackVersion);
                callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::CommitError,
                            "Commit scheduler storage error, just return with no rollback"));
#endif
            return;
        }
        else
        {
            ++status->success;
        }

        bcos::protocol::TwoPCParams executorParams;
        executorParams.number = number();
        executorParams.timestamp = commitTS;

        // TODO: new parallel_for_each no suite
        for (auto const& executorIt : *m_scheduler->m_executorManager)
        {
            SCHEDULER_LOG(TRACE) << "Commit executor for block " << executorParams.number;

            executorIt->commit(executorParams, [this, status](bcos::Error::Ptr&& error) {
                {
                    WriteGuard lock(status->x_lock);
                    if (error)
                    {
                        SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number()) << "Commit executor error!"
                                             << error->errorMessage();

                        // executor failed is also success++
                        // because commit has been successful after ledger commit
                        // this executorIt->commit is just for clear storage cache.
                        ++status->success;

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
        }
    });
}

void BlockExecutive::batchBlockRollback(
    uint64_t version, std::function<void(Error::UniquePtr)> callback)
{
    auto status = std::make_shared<CommitStatus>();
    status->total = 1 + getExecutorSize();  // self + all executors
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
    params.timestamp = version;
    m_scheduler->m_storage->asyncRollback(
        params, [this, version, number = params.number, status](Error::Ptr&& error) {
            {
                WriteGuard lock(status->x_lock);
                if (error)
                {
                    SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number) << "Rollback node storage error!"
                                         << error->errorMessage();

                    ++status->failed;
                }
                else
                {
                    ++status->success;
                }
            }

            if (status->failed > 0)
            {
                status->checkAndCommit(*status);
            }
            else
            {
                // for (auto& it : *(m_scheduler->m_executorManager))
                forEachExecutor(
                    [this, version, number, status](std::string,
                        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor) {
                        bcos::protocol::TwoPCParams executorParams;
                        executorParams.number = number;
                        executorParams.timestamp = version;
                        executor->rollback(
                            executorParams, [this, number, status](bcos::Error::Ptr&& error) {
                                {
                                    WriteGuard lock(status->x_lock);
                                    if (error)
                                    {
                                        if (error->errorCode() ==
                                            bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                                        {
                                            SCHEDULER_LOG(INFO)
                                                << BLOCK_NUMBER(number)
                                                << "Rollback a restarted executor. Ignore."
                                                << error->errorMessage();
                                            ++status->success;
                                            triggerSwitch();
                                        }
                                        else
                                        {
                                            SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number)
                                                                 << "Rollback executor error!"
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
        });
}


DmcExecutor::Ptr BlockExecutive::buildDmcExecutor(const std::string& name,
    const std::string& contractAddress,
    bcos::executor::ParallelTransactionExecutorInterface::Ptr executor)
{
    auto dmcExecutor = std::make_shared<DmcExecutor>(name, contractAddress, m_block, executor,
        m_keyLocks, m_scheduler->m_hashImpl, m_dmcRecorder, isCall());
    return dmcExecutor;
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

        std::string dispatchAddress;
        if (number() == 0)
        {
            dispatchAddress = "genesis block use same executor to init";
        }
        else
        {
            dispatchAddress = contractAddress;
        }

        auto executor = m_scheduler->executorManager()->dispatchExecutor(dispatchAddress);
        auto executorInfo = m_scheduler->executorManager()->getExecutorInfo(dispatchAddress);

        if (executor == nullptr || executorInfo == nullptr)
        {
            BOOST_THROW_EXCEPTION(BCOS_ERROR(
                SchedulerError::ExecutorNotEstablishedError, "The executor has not started!"));
        }

        if (!m_dmcRecorder)
        {
            m_dmcRecorder = std::make_shared<DmcStepRecorder>();
        }

        auto dmcExecutor = buildDmcExecutor(executorInfo->name, contractAddress, executor);
        if (m_scheduler->ledgerConfig().features().get(ledger::Features::Flag::bugfix_dmc_revert))
        {
            dmcExecutor->setEnablePreFinishType(true);
        }

        m_dmcExecutors.emplace(contractAddress, dmcExecutor);

        // register functions
        dmcExecutor->setSchedulerOutHandler([this](ExecutiveState::Ptr executiveState) {
            scheduleExecutive(std::move(executiveState));
        });

        dmcExecutor->setOnTxFinishedHandler(
            [this](bcos::protocol::ExecutionMessage::UniquePtr output) {
                onTxFinish(std::move(output));
            });
        dmcExecutor->setOnNeedSwitchEventHandler([this]() { triggerSwitch(); });

        dmcExecutor->setOnGetCodeHandler([this](std::string_view address) {
            auto executor = m_scheduler->executorManager()->dispatchExecutor(address);
            if (!executor)
            {
                SCHEDULER_LOG(ERROR) << "Could not dispatch correspond executor during getCode(). "
                                     << LOG_KV("address", address);
                return bcos::bytes();
            }

            // getCode from executor
            std::promise<bcos::bytes> codeFuture;
            executor->getCode(
                address, [&codeFuture, this](bcos::Error::Ptr error, bcos::bytes codes) {
                    if (error)
                    {
                        SCHEDULER_LOG(ERROR)
                            << "Could not getCode from correspond executor. Trigger switch."
                            << LOG_KV("code", error->errorCode())
                            << LOG_KV("message", error->errorMessage());
                        triggerSwitch();
                        codeFuture.set_value(bcos::bytes());
                    }
                    else
                    {
                        codeFuture.set_value(std::move(codes));
                    }
                });
            bcos::bytes codes = codeFuture.get_future().get();

            return codes;
        });

        return dmcExecutor;
    }
}

void BlockExecutive::scheduleExecutive(ExecutiveState::Ptr executiveState)
{
    DmcExecutor::Ptr dmcExecutor =
        registerAndGetDmcExecutor(std::string(executiveState->message->to()));
    dmcExecutor->scheduleIn(std::move(executiveState));
}

void BlockExecutive::onTxFinish(bcos::protocol::ExecutionMessage::UniquePtr output)
{
    auto txGasUsed = m_gasLimit - output->gasAvailable();
    // Calc the gas set to header

    if (bcos::precompiled::c_systemTxsAddress.contains(output->from()))
    {
        // Note: We will not consume gas when EOA call sys contract directly.
        // When dmc return, sys contract is from(), to() is EOA address.
        // But if this tx is a deploy tx, from() is sys contract but to() is new address.
        // So we need to fix this bug here: only consider output->newEVMContractAddress().empty()
        // here. Leaving only EOA call sys contract here.
        auto hasBugfix = m_scheduler->ledgerConfig().features().get(
            ledger::Features::Flag::bugfix_dmc_deploy_gas_used);
        if (!hasBugfix || (hasBugfix && output->newEVMContractAddress().empty()))
        {
            txGasUsed = 0;
        }
    }
    m_gasUsed.fetch_add(txGasUsed);
    auto version = m_executiveResults[output->contextID() - m_startContextID].version;
    switch (version)
    {
    case int32_t(bcos::protocol::TransactionVersion::V0_VERSION):
    {
        auto receipt = m_scheduler->m_blockFactory->receiptFactory()->createReceipt(txGasUsed,
            std::string(output->newEVMContractAddress()), output->takeLogEntries(),
            output->status(), output->data(), number());
        // write receipt in results
        SCHEDULER_LOG(TRACE) << " 6.GenReceipt:\t [^^] " << output->toString()
                             << " -> contextID:" << output->contextID() - m_startContextID
                             << ", receipt: " << receipt->hash()
                             << ", gasUsed: " << receipt->gasUsed()
                             << ", version: " << receipt->version()
                             << ", status: " << receipt->status();
        m_executiveResults[output->contextID() - m_startContextID].receipt = std::move(receipt);
        break;
    }
    case int32_t(bcos::protocol::TransactionVersion::V1_VERSION):
    case int32_t(bcos::protocol::TransactionVersion::V2_VERSION):
    {
        auto receipt = m_scheduler->m_blockFactory->receiptFactory()->createReceipt2(txGasUsed,
            std::string(output->newEVMContractAddress()), output->takeLogEntries(),
            output->status(), output->data(), number(), std::string(output->effectiveGasPrice()),
            static_cast<protocol::TransactionVersion>(version));
        // write receipt in results
        SCHEDULER_LOG(TRACE) << " 6.GenReceipt:\t [^^] " << output->toString()
                             << " -> contextID:" << output->contextID() - m_startContextID
                             << ", receipt: " << receipt->hash()
                             << ", gasUsed: " << receipt->gasUsed()
                             << ", version: " << receipt->version()
                             << ", status: " << receipt->status()
                             << ", effectiveGasPrice: " << receipt->effectiveGasPrice();
        m_executiveResults[output->contextID() - m_startContextID].receipt = std::move(receipt);
        break;
    }
    default:
        BOOST_THROW_EXCEPTION(BCOS_ERROR(SchedulerError::InvalidTransactionVersion,
            "Invalid receipt version: " + std::to_string(version)));
    }
}


void BlockExecutive::serialPrepareExecutor()
{
    // Notice:
    // For the same DMC lock priority
    // m_dmcExecutors must be prepared in contractAddress less<> serial order

    /// Handle normal message
    bool hasScheduleOutMessage = false;
    do
    {
        hasScheduleOutMessage = false;
        // dump current DmcExecutor (m_dmcExecutors may be modified during traversing)
        std::set<std::string, std::less<>> currentExecutors;
        for (auto& it : m_dmcExecutors)
        {
            it.second->releaseOutdatedLock();  // release last round's lock
            currentExecutors.insert(it.first);
        }

        // for each current DmcExecutor
        for (const auto& address : currentExecutors)
        {
            DMC_LOG(TRACE) << " 0.Pre-DmcExecutor: \t----------------- addr:" << address
                           << " | number:" << number() << " -----------------";
            hasScheduleOutMessage |=
                m_dmcExecutors[address]->prepare();  // may generate new contract in m_dmcExecutors
        }

        // must all schedule out message has been handled.
    } while (hasScheduleOutMessage);


    /// try to handle locked message
    // try to unlock some locked tx
    bool needDetectDeadlock = true;
    bool allFinished = true;
    for (auto& it : m_dmcExecutors)
    {
        const auto& address = it.first;
        auto dmcExecutor = m_dmcExecutors[address];
        if (dmcExecutor->hasFinished())
        {
            continue;  // must jump finished executor
        }
        DMC_LOG(TRACE) << " 3.UnlockPrepare: \t |---------------- addr:" << address
                       << " | number:" << std::to_string(number()) << " ----------------|";

        allFinished = false;
        bool need = dmcExecutor->unlockPrepare();
        needDetectDeadlock &= need;
        // if there is an executor need detect deadlock, noNeedDetectDeadlock = false
    }

    if (needDetectDeadlock && !allFinished)
    {
        bool needRevert = false;
        // The code below can be optimized by reverting many tx in one DMC round
        for (auto& it : m_dmcExecutors)
        {
            const auto& address = it.first;
            DMC_LOG(TRACE) << " --detect--revert-- " << address << " | " << number()
                           << " -----------------";
            if (m_dmcExecutors[address]->detectLockAndRevert())
            {
                needRevert = true;
                break;  // Just revert the first found tx
            }
        }

        if (!needRevert)
        {
            std::string errorMsg =
                "Need detect deadlock but no deadlock detected! block: " + std::to_string(number());
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

    if (m_scheduler->ledgerConfig().features().get(ledger::Features::Flag::bugfix_eip55_addr))
    {
        boost::to_lower(out);  // support sdk pass EIP55 address
    }

    return out;
}

bcos::storage::TransactionalStorageInterface::Ptr BlockExecutive::getStorage()
{
    return m_scheduler->m_storage;
}

size_t BlockExecutive::getExecutorSize()
{
    return m_scheduler->m_executorManager->size();
}

void BlockExecutive::forEachExecutor(
    std::function<void(std::string, bcos::executor::ParallelTransactionExecutorInterface::Ptr)>
        handleExecutor)
{
    m_scheduler->m_executorManager->forEachExecutor(std::move(handleExecutor));
}
