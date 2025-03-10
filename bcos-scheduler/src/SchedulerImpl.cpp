#include "SchedulerImpl.h"
#include "BlockExecutive.h"
#include "Common.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Common.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-tool/VersionConverter.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/ITTAPI.h>
#include <bcos-utilities/Overloaded.h>
#include <ittnotify.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <memory>
#include <mutex>
#include <string_view>


using namespace bcos::scheduler;

const __itt_domain* const ITT_DOMAIN_SCHEDULER_EXECUTE = __itt_domain_create("scheduler.execute");
const __itt_domain* const ITT_DOMAIN_SCHEDULER_COMMIT = __itt_domain_create("scheduler.commit");

// const std::string_view c_SCHEDULER_EXECUTE_BLOCK = "scheduler.execute";
// const std::string_view c_SCHEDULER_COMMIT_BLOCK = "scheduler.commit";
const __itt_string_handle* const ITT_STRING_SCHEDULER_EXECUTE =
    __itt_string_handle_create("scheduler.execute");
const __itt_string_handle* const ITT_STRING_SCHEDULER_COMMIT =
    __itt_string_handle_create("scheduler.commit");
// __itt_id ITT_SCHEDULER_EXECUTE_ID = 1;

SchedulerImpl::SchedulerImpl(ExecutorManager::Ptr executorManager,
    bcos::ledger::LedgerInterface::Ptr ledger,
    bcos::storage::TransactionalStorageInterface::Ptr storage,
    bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
    bcos::protocol::BlockFactory::Ptr blockFactory, bcos::txpool::TxPoolInterface::Ptr txPool,
    bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
    bcos::crypto::Hash::Ptr hashImpl, bool isAuthCheck, bool isWasm, int64_t schedulerTermId,
    size_t keyPageSize)
  : SchedulerImpl(executorManager, ledger, storage, executionMessageFactory, blockFactory, txPool,
        transactionSubmitResultFactory, hashImpl, isAuthCheck, isWasm, false, schedulerTermId,
        keyPageSize)
{}

SchedulerImpl::SchedulerImpl(ExecutorManager::Ptr executorManager,
    bcos::ledger::LedgerInterface::Ptr ledger,
    bcos::storage::TransactionalStorageInterface::Ptr storage,
    bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
    bcos::protocol::BlockFactory::Ptr blockFactory, bcos::txpool::TxPoolInterface::Ptr txPool,
    bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
    bcos::crypto::Hash::Ptr hashImpl, bool isAuthCheck, bool isWasm, bool isSerialExecute,
    int64_t schedulerTermId, size_t keyPageSize)
  : m_executorManager(std::move(executorManager)),
    m_ledger(std::move(ledger)),
    m_storage(std::move(storage)),
    m_executionMessageFactory(std::move(executionMessageFactory)),
    m_blockExecutiveFactory(
        std::make_shared<bcos::scheduler::BlockExecutiveFactory>(isSerialExecute, keyPageSize)),
    m_blockFactory(std::move(blockFactory)),
    m_txPool(txPool),
    m_transactionSubmitResultFactory(std::move(transactionSubmitResultFactory)),
    m_hashImpl(std::move(hashImpl)),
    m_isWasm(isWasm),
    m_schedulerTermId(schedulerTermId),
    m_preExeWorker("preExeScheduler", 2),  // assume that preExe is no slower than exe speed/2
    m_exeWorker("exeScheduler", 1)
{
    start();

    if (!m_ledgerConfig)
    {
        m_ledgerConfig = task::syncWait(ledger::getLedgerConfig(*m_ledger));
    }
}

void SchedulerImpl::handleBlockQueue(bcos::protocol::BlockNumber requestBlockNumber,
    std::function<void(bcos::protocol::BlockNumber)> whenOlder,  // whenOlder(frontNumber)
    std::function<void(BlockExecutive::Ptr)> whenQueueFront, std::function<void()> afterFront,
    std::function<void(BlockExecutive::Ptr)> whenNotFrontInQueue, std::function<void()> beforeBack,
    std::function<void()> whenQueueBack,
    std::function<void(bcos::protocol::BlockNumber)> whenNewer,  // whenNewer(backNumber)
    std::function<void(std::exception const&)> whenException)
{
    //                [--------------- block queue --------------]
    //  ..95 96 97    [              98 | 99 100 101 102 103     ]            104  | 105 106...
    //  whenOlder(98) [whenQueueFront() | whenNotFrontInQueue()..] whenQueueBack() | whenNewer(104)
    //  whenQueueFront() -> afterFront()
    //  beforeBack() -> whenQueueBack()

    bcos::protocol::BlockNumber currentNumber = getCurrentBlockNumber();
    std::unique_lock<std::mutex> blocksLock(m_blocksMutex);

    try
    {
        if (m_blocks->empty())
        {
            bcos::protocol::BlockNumber number = currentNumber;
            if (requestBlockNumber == 0)
            {
                // handle genesis block, to execute or commit
                beforeBack();
                blocksLock.unlock();
                whenQueueBack();
            }
            else if (requestBlockNumber <= number)
            {
                blocksLock.unlock();
                whenOlder(number);
            }
            else if (requestBlockNumber == number + 1)
            {
                beforeBack();
                blocksLock.unlock();
                whenQueueBack();
            }
            else
            {
                blocksLock.unlock();
                whenNewer(number);
            }
        }
        else
        {
            // has block cache
            bcos::protocol::BlockNumber frontNumber = m_blocks->front()->number();
            bcos::protocol::BlockNumber backNumber = m_blocks->back()->number();

            if (requestBlockNumber < frontNumber)
            {
                blocksLock.unlock();
                whenOlder(frontNumber);
            }
            else if (requestBlockNumber == frontNumber)
            {
                auto frontBlock = m_blocks->front();
                blocksLock.unlock();
                whenQueueFront(frontBlock);

                blocksLock.lock();
                afterFront();
                blocksLock.unlock();
            }
            else if (requestBlockNumber <= backNumber)
            {
                auto it = ++m_blocks->begin();  // it is begin next
                while (it != m_blocks->end() && it->get()->number() != requestBlockNumber)
                {
                    ++it;
                }
                if (it != m_blocks->end())
                {
                    blocksLock.unlock();
                    whenNotFrontInQueue(*it);
                }
            }
            else if (requestBlockNumber == backNumber + 1)
            {
                // Notice that whenQueueBack is backNumber + 1
                beforeBack();
                blocksLock.unlock();
                whenQueueBack();
            }
            else
            {
                blocksLock.unlock();
                whenNewer(backNumber);
            }
        }
    }
    catch (std::exception const& e)
    {
        SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(requestBlockNumber) << "handleBlockQueue exception"
                             << boost::diagnostic_information(e);
        if (blocksLock.owns_lock())
        {
            blocksLock.unlock();
        }
        whenException(e);
    }
}


void SchedulerImpl::executeBlock(bcos::protocol::Block::Ptr block, bool verify,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool)> _callback)
{
    m_exeWorker.enqueue(
        [this, block = std::move(block), verify, callback = std::move(_callback)]() mutable {
            __itt_frame_begin_v3(ITT_DOMAIN_SCHEDULER_EXECUTE, nullptr);
            executeBlockInternal(std::move(block), verify,
                [callback = std::move(callback)](bcos::Error::Ptr&& err,
                    bcos::protocol::BlockHeader::Ptr&& header, bool isSysBlock) {
                    __itt_frame_end_v3(ITT_DOMAIN_SCHEDULER_EXECUTE, nullptr);
                    callback(std::move(err), std::move(header), isSysBlock);
                });
        });
}
void SchedulerImpl::executeBlockInternal(bcos::protocol::Block::Ptr block, bool verify,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool _sysBlock)>
        _callback)
{
    if (block->blockHeader()->version() > (uint32_t)g_BCOSConfig.maxSupportedVersion())
    {
        auto errorMessage = "The block version is larger than maxSupportedVersion";
        SCHEDULER_LOG(WARNING) << BLOCK_NUMBER(block->blockHeaderConst()->number()) << errorMessage
                               << LOG_KV("version", block->version())
                               << LOG_KV("maxSupportedVersion", g_BCOSConfig.maxSupportedVersion());
        _callback(
            BCOS_ERROR_PTR(SchedulerError::InvalidBlockVersion, errorMessage), nullptr, false);
        return;
    }
    uint64_t waitT = 0;
    if (m_lastExecuteFinishTime > 0)
    {
        waitT = utcTime() - m_lastExecuteFinishTime;
    }
    if (waitT > 3000)
    {
        waitT = 0;
        m_lastExecuteFinishTime = 0;
    }
    auto signature = block->blockHeaderConst()->signatureList();
    auto requestBlockNumber = block->blockHeader()->number();
    try
    {
        fetchConfig(requestBlockNumber);
    }
    catch (std::exception& e)
    {
        SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(block->blockHeaderConst()->number())
                             << "fetchGasLimit exception: " << boost::diagnostic_information(e);
        _callback(BCOS_ERROR_WITH_PREV_PTR(
                      SchedulerError::fetchGasLimitError, "fetchGasLimitError exception", e),
            nullptr, false);
        return;
    }

    SCHEDULER_LOG(INFO) << METRIC << BLOCK_NUMBER(requestBlockNumber) << "ExecuteBlock request"
                        << LOG_KV("gasLimit", m_gasLimit) << LOG_KV("gasPrice", getGasPrice())
                        << LOG_KV("verify", verify) << LOG_KV("signatureSize", signature.size())
                        << LOG_KV("txCount", block->transactionsSize())
                        << LOG_KV("metaTxCount", block->transactionsMetaDataSize())
                        << LOG_KV("version", (bcos::protocol::BlockVersion)(block->version()))
                        << LOG_KV("waitT", waitT)
                        << LOG_KV("executor version", m_ledgerConfig->executorVersion());
    auto start = utcTime();
    auto callback = [requestBlockNumber, _callback = std::move(_callback)](bcos::Error::Ptr&& error,
                        bcos::protocol::BlockHeader::Ptr&& blockHeader, bool _sysBlock) {
        SCHEDULER_LOG(DEBUG) << METRIC << BLOCK_NUMBER(requestBlockNumber)
                             << "ExecuteBlock response"

                             << LOG_KV(error ? "failed" : "ok", error ? error->what() : "ok");

        _callback(error == nullptr ? nullptr : std::move(error), std::move(blockHeader), _sysBlock);
    };


    auto whenOlder = [requestBlockNumber, callback](bcos::protocol::BlockNumber number) {
        auto message = (boost::format("Execute an history block %d, current number is %d") %
                        requestBlockNumber % number)
                           .str();
        SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number) << message;
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlocks, message), nullptr, false);
    };

    auto whenInQueue = [this, callback, block, requestBlockNumber, &signature, verify](
                           BlockExecutive::Ptr blockExecutive) {
        auto blockHeader = blockExecutive->result();

        if (blockHeader == nullptr)
        {
            auto message = "hit block cache, but block is executing!";
            SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(requestBlockNumber) << "ExecuteBlock error, "
                                 << message;
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, message), nullptr, false);
        }
        else
        {
            if (verify && blockHeader->hash() != block->blockHeader()->hash())
            {
                SCHEDULER_LOG(WARNING)
                    << BLOCK_NUMBER(requestBlockNumber)
                    << "ExecuteBlock failed. The executed block has been cached "
                       "but request header hash is not the same. Trigger switch."
                    << LOG_KV("cachedHeaderHash", blockHeader->hash().abridged())
                    << LOG_KV("requestHeaderHash", block->blockHeader()->hash().abridged())
                    << LOG_KV("verify", verify);
                triggerSwitch();
                callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlocks,
                             "request header not the same with cached"),
                    nullptr, false);
            }
            else
            {
                SCHEDULER_LOG(INFO)
                    << BLOCK_NUMBER(requestBlockNumber) << LOG_BADGE("BlockTrace")
                    << "ExecuteBlock success, return executed block"
                    << LOG_KV("signatureSize", signature.size()) << LOG_KV("verify", verify);
                callback(nullptr, std::move(blockHeader), blockExecutive->sysBlock());
            }
        }
    };

    auto whenQueueFront = [whenInQueue](
                              BlockExecutive::Ptr blockExecutive) { whenInQueue(blockExecutive); };

    auto afterFront = []() {
        // do nothing
    };

    auto whenNotFrontInQueue = [whenInQueue](BlockExecutive::Ptr blockExecutive) {
        whenInQueue(blockExecutive);
    };

    auto whenNewer = [requestBlockNumber, callback](bcos::protocol::BlockNumber backNumber) {
        auto message =
            (boost::format(
                 "Try to execute an discontinuous block: %ld, queue back blockNumber: %ld") %
                requestBlockNumber % backNumber)
                .str();
        SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(requestBlockNumber) << "ExecuteBlock error, "
                             << message;
        callback(
            BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlockNumber, message), nullptr, false);
    };
    auto whenException = [requestBlockNumber, callback](std::exception const& e) {
        auto message = (boost::format("ExecuteBlock exception %s") % e.what()).str();
        SCHEDULER_LOG(WARNING) << BLOCK_NUMBER(requestBlockNumber) << message;
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::UnknownError, message), nullptr, false);
    };

    std::shared_ptr<std::unique_lock<std::mutex>> executeLock = nullptr;
    BlockExecutive::Ptr blockExecutive = nullptr;

    auto beforeBack = [this, block, verify, &executeLock, &blockExecutive, callback]() {
        // update m_block
        blockExecutive = getPreparedBlock(
            block->blockHeaderConst()->number(), block->blockHeaderConst()->timestamp());

        if (blockExecutive == nullptr)
        {
            // the block has not been prepared, just make a new one here
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("preExeBlock")
                                 << BLOCK_NUMBER(block->blockHeaderConst()->number())
                                 << "Not hit prepared block executive, create.";
            // blockExecutive = std::make_shared<SerialBlockExecutive>(std::move(block), this,
            // 0,
            //     m_transactionSubmitResultFactory, false, m_blockFactory, m_txPool,
            //     m_gasLimit, verify);
            blockExecutive = m_blockExecutiveFactory->build(std::move(block), this, 0,
                m_transactionSubmitResultFactory, false, m_blockFactory, m_txPool, m_gasLimit,
                getGasPrice(), verify);

            blockExecutive->setOnNeedSwitchEventHandler([this]() { triggerSwitch(); });
        }
        else
        {
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("preExeBlock")
                                 << BLOCK_NUMBER(block->blockHeaderConst()->number())
                                 << "Hit prepared block executive cache, use it.";
            blockExecutive->block()->setBlockHeader(block->blockHeader());
        }

        // acquire lock
        executeLock =
            std::make_shared<std::unique_lock<std::mutex>>(m_executeMutex, std::try_to_lock);
        if (!executeLock->owns_lock())
        {
            executeLock = nullptr;
            return;
        }

        m_blocks->emplace_back(blockExecutive);

        // blockExecutive = m_blocks->back();
    };

    // to execute the block
    auto whenQueueBack = [this, start, &executeLock, &blockExecutive, callback,
                             requestBlockNumber]() {
        if (!executeLock)
        {
            // if not acquire the lock, return error
            auto message =
                "Another block is executing, maybe consensus and sync execute same block";
            SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(requestBlockNumber) << "ExecuteBlock error, "
                                 << message;
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, message), nullptr, false);
            return;
        }

        SCHEDULER_LOG(INFO) << BLOCK_NUMBER(requestBlockNumber) << LOG_BADGE("BlockTrace")
                            << "ExecuteBlock start" << LOG_KV("time(ms)", utcTime() - start);
        auto startTime = utcTime();
        try
        {
            blockExecutive->asyncExecute(
                [this, startTime, requestBlockNumber, callback = std::move(callback), executeLock](
                    Error::UniquePtr error, protocol::BlockHeader::Ptr header, bool _sysBlock) {
                    if (!m_isRunning)
                    {
                        executeLock->unlock();
                        callback(BCOS_ERROR_UNIQUE_PTR(
                                     SchedulerError::Stopped, "Scheduler is not running"),
                            nullptr, false);
                        return;
                    }

                    if (error)
                    {
                        SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(requestBlockNumber)
                                             << "executeBlock error: " << error->what();
                        {
                            std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
                            if (!m_blocks->empty() &&
                                m_blocks->back()->number() == requestBlockNumber)
                            {
                                m_blocks->pop_back();
                            }
                            blocksLock.unlock();
                        }
                        executeLock->unlock();
                        callback(BCOS_ERROR_WITH_PREV_PTR(
                                     SchedulerError::UnknownError, "executeBlock error:", *error),
                            nullptr, _sysBlock);
                        return;
                    }
                    auto signature = header->signatureList();
                    SCHEDULER_LOG(INFO)
                        << BLOCK_NUMBER(header->number()) << LOG_BADGE("BlockTrace")
                        << "ExecuteBlock success" << LOG_KV("hash", header->hash().abridged())
                        << LOG_KV("stateRoot", header->stateRoot().hex())
                        << LOG_KV("receiptRoot", header->receiptsRoot().hex())
                        << LOG_KV("txsRoot", header->txsRoot().abridged())
                        << LOG_KV("gasUsed", header->gasUsed())
                        << LOG_KV("signatureSize", signature.size())
                        << LOG_KV("timeCost", utcTime() - startTime)
                        << LOG_KV("blockVersion", header->version());

                    m_lastExecuteFinishTime = utcTime();
                    executeLock->unlock();
                    callback(std::move(error), std::move(header), _sysBlock);
                });
        }
        catch (std::exception const& e)
        {
            executeLock->unlock();
            SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(requestBlockNumber)
                                 << "whenQueueBack asyncExecute exception"
                                 << boost::diagnostic_information(e);
            throw e;
        }
    };

    // run all handler
    handleBlockQueue(requestBlockNumber, whenOlder, whenQueueFront, afterFront, whenNotFrontInQueue,
        beforeBack, whenQueueBack, whenNewer, whenException);
}

void SchedulerImpl::commitBlock(bcos::protocol::BlockHeader::Ptr header,
    std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> _callback)
{
    __itt_frame_begin_v3(ITT_DOMAIN_SCHEDULER_COMMIT, nullptr);
    SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(header->number()) << "CommitBlock request";

    auto requestBlockNumber = header->number();
    auto callback = [requestBlockNumber, _callback = std::move(_callback)](
                        bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config) {
        __itt_frame_end_v3(ITT_DOMAIN_SCHEDULER_COMMIT, nullptr);
        SCHEDULER_LOG(DEBUG) << METRIC << BLOCK_NUMBER(requestBlockNumber) << "CommitBlock response"
                             << LOG_KV(error ? "failed" : "ok", error ? error->what() : "ok");
        _callback(error == nullptr ? nullptr : std::move(error), std::move(config));
    };

    auto whenOlder = [requestBlockNumber, callback](
                         bcos::protocol::BlockNumber currentBlockNumber) {
        auto message = (boost::format("commit an history block %d, current number is %d") %
                        requestBlockNumber % currentBlockNumber)
                           .str();
        SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(requestBlockNumber) << message;
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlocks, message), nullptr);
    };

    auto whenAfterFront = [this, requestBlockNumber, callback]() {
        auto message =
            "A smaller block need to commit, requestBlockNumber " +
            std::to_string(requestBlockNumber) +
            +", need to commit blockNumber: " + std::to_string(getCurrentBlockNumber() + 1);
        SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(requestBlockNumber) << "CommitBlock error, "
                             << message;

        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlockNumber, message), nullptr);
    };

    auto whenNotFrontInQueue = [whenAfterFront](BlockExecutive::Ptr) { whenAfterFront(); };

    auto beforeBack = []() {
        // do nothing
    };

    auto whenQueueBack = [whenAfterFront]() { whenAfterFront(); };

    auto whenNewer = [whenAfterFront](bcos::protocol::BlockNumber) { whenAfterFront(); };
    auto whenException = [callback, requestBlockNumber](std::exception const& e) {
        auto message = (boost::format("CommitBlock exception %s") % e.what()).str();
        SCHEDULER_LOG(WARNING) << BLOCK_NUMBER(requestBlockNumber) << message;
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::UnknownError, message), nullptr);
    };

    auto whenQueueFront = [this, requestBlockNumber, header = std::move(header), callback](
                              BlockExecutive::Ptr blockExecutive) {
        // acquire lock
        std::shared_ptr<std::unique_lock<std::timed_mutex>> commitLock =
            std::make_shared<std::unique_lock<std::timed_mutex>>(m_commitMutex, std::try_to_lock);

        if (!commitLock->owns_lock() && !commitLock->try_lock_for(std::chrono::seconds(1)))
        {
            std::string message =
                (boost::format("commitBlock: Another block is committing! Block to commit "
                               "number: %ld, hash: %s, waiting for it") %
                    requestBlockNumber % header->hash().abridged())
                    .str();


            SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(requestBlockNumber) << "CommitBlock error, "
                                 << message;
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::BlockIsCommitting, message), nullptr);
            return;
        }

        auto currentBlockNumber = getCurrentBlockNumber();
        if (currentBlockNumber + 1 != requestBlockNumber)
        {
            // happens in some multi-thread scenario, the block has been commited
            auto message = (boost::format("commit an committed block %d, current number is %d") %
                            requestBlockNumber % currentBlockNumber)
                               .str();
            SCHEDULER_LOG(ERROR) << message;
            commitLock->unlock();
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlocks, message), nullptr);
            return;
        }

        if (!blockExecutive->result())
        {
            auto message = "Block is executing, number=" + std::to_string(blockExecutive->number());
            SCHEDULER_LOG(ERROR) << "CommitBlock error, " << message;

            commitLock->unlock();
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::BlockIsCommitting, message), nullptr);
            return;
        }

        // Note: only when the signatureList is empty need to reset the header
        // in case of the signatureList of the header is accessing by the sync module while
        // frontBlock is setting newBlockHeader, which will cause the signatureList ilegal
        auto executedHeader = blockExecutive->block()->blockHeader();
        auto signature = executedHeader->signatureList();
        if (signature.empty())
        {
            blockExecutive->block()->setBlockHeader(std::move(header));
        }

        SCHEDULER_LOG(INFO) << BLOCK_NUMBER(requestBlockNumber) << LOG_BADGE("BlockTrace")
                            << "CommitBlock start";
        auto startTime = utcTime();
        blockExecutive->asyncCommit([this, startTime, callback = std::move(callback),
                                        blockExecutive, block = blockExecutive->block(),
                                        commitLock](Error::UniquePtr&& error) mutable {
            if (!m_isRunning)
            {
                commitLock->unlock();
                callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "Scheduler is not running"),
                    nullptr);
                return;
            }

            if (error)
            {
                bcos::protocol::BlockNumber currentNumber = -1;
                SCHEDULER_LOG(ERROR) << "CommitBlock error, " << error->errorMessage();
                {
                    std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
                    // refresh block cache
                    currentNumber = getBlockNumberFromStorage();
                    // note that genesis sysBlock is blockNumber 0, we need to ignore it
                    while (!m_blocks->empty() && currentNumber >= m_blocks->front()->number() &&
                           currentNumber != 0)
                    {
                        SCHEDULER_LOG(DEBUG) << "Remove committed block on commit failed : "
                                             << m_blocks->front()->number() << " success";
                        m_blocks->pop_front();
                    }
                }
                fetchConfig(currentNumber);
                commitLock->unlock();
                callback(BCOS_ERROR_UNIQUE_PTR(
                             error->errorCode(), "CommitBlock error: " + error->errorMessage()),
                    nullptr);
                return;
            }
            {
                std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
                auto number = block->blockHeaderConst()->number();
                if (m_blocks && !m_blocks->empty() && m_blocks->front()->number() == number)
                {
                    m_blocks->pop_front();
                    SCHEDULER_LOG(DEBUG)
                        << "Remove committed block after commit: " << number << " success";
                }
                removeAllOldPreparedBlock(number);
            }

            task::wait([](decltype(this) self, decltype(startTime) startTime,
                           decltype(commitLock) commitLock, decltype(blockExecutive) blockExecutive,
                           decltype(callback) callback) -> task::Task<void> {
                try
                {
                    auto ledgerConfig = co_await ledger::getLedgerConfig(*self->m_ledger);

                    if (!self->m_isRunning)
                    {
                        commitLock->unlock();
                        callback(BCOS_ERROR_UNIQUE_PTR(
                                     SchedulerError::Stopped, "Scheduler is not running"),
                            nullptr);
                        co_return;
                    }

                    auto blockNumber = ledgerConfig->blockNumber();
                    auto gasLimitAndEnableNumber = ledgerConfig->gasLimit();
                    auto gasPriceAndEnableNumber = ledgerConfig->gasPrice();
                    // Note: takes effect in next block. we query the enableNumber of blockNumber
                    // + 1.
                    if (std::get<1>(gasLimitAndEnableNumber) <= (blockNumber + 1))
                    {
                        self->m_gasLimit = std::get<0>(gasLimitAndEnableNumber);
                    }

                    if (std::get<1>(gasPriceAndEnableNumber) <= (blockNumber + 1))
                    {
                        self->setGasPrice(std::get<0>(gasPriceAndEnableNumber));
                    }

                    self->m_blockVersion = ledgerConfig->compatibilityVersion();

                    if (blockExecutive->isSysBlock())
                    {
                        self->removeAllPreparedBlock();  // must clear prepared cache
                    }

                    SCHEDULER_LOG(INFO)
                        << BLOCK_NUMBER(blockNumber) << LOG_BADGE("BlockTrace")
                        << "CommitBlock success" << LOG_KV("gas limit", self->m_gasLimit)
                        << LOG_KV("timeCost", utcTime() - startTime);
                    self->m_ledgerConfig = ledgerConfig;
                    commitLock->unlock();  // just unlock here

                    self->m_ledger->removeExpiredNonce(blockNumber, false);
                    // Note: blockNumber = 0, means system deploy, and tx is not existed in
                    // txpool. So it should not exec tx notifier
                    if (self->m_txNotifier && blockNumber != 0)
                    {
                        SCHEDULER_LOG(DEBUG) << "Start notify block result: " << blockNumber;
                        blockExecutive->asyncNotify(self->m_txNotifier,
                            [self, blockNumber, callback = std::move(callback),
                                ledgerConfig = std::move(ledgerConfig)](Error::Ptr _error) mutable {
                                if (!self->m_isRunning)
                                {
                                    callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped,
                                                 "Scheduler is not running"),
                                        nullptr);
                                    return;
                                }

                                if (self->m_blockNumberReceiver)
                                {
                                    self->m_blockNumberReceiver(blockNumber);
                                }

                                SCHEDULER_LOG(INFO)
                                    << LOG_BADGE("BlockTrace") << "Notify block result success"
                                    << LOG_KV("blockNumber", blockNumber);
                                // Note: only after the block notify finished can call the callback
                                callback(std::move(_error), std::move(ledgerConfig));
                            });
                    }
                    else
                    {
                        callback(nullptr, std::move(ledgerConfig));
                    }
                }
                catch (Error& error)
                {
                    SCHEDULER_LOG(ERROR) << "Get system config error, " << error.errorMessage();

                    commitLock->unlock();
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                 SchedulerError::UnknownError, "Get system config error", error),
                        nullptr);
                    co_return;
                }
            }(this, startTime, std::move(commitLock), std::move(blockExecutive),
                                                            std::move(callback)));
        });
    };

    auto afterFront = []() {
        // do nothing
    };

    // run all handler
    handleBlockQueue(requestBlockNumber, whenOlder, whenQueueFront, afterFront, whenNotFrontInQueue,
        beforeBack, whenQueueBack, whenNewer, whenException);
}

void SchedulerImpl::status(
    std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)> callback)
{
    (void)callback;
}

void SchedulerImpl::call(protocol::Transaction::Ptr tx,
    std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)> callback)
{
    // call but to is empty,
    // it will cause tx message be marked as 'create' falsely when asyncExecute tx
    if (tx->to().empty())
    {
        SCHEDULER_LOG(DEBUG) << LOG_BADGE("call") << LOG_DESC("call address empty");
        callback(BCOS_ERROR_PTR(SchedulerError::UnknownError, "Call address is empty"), nullptr);
        return;
    }

    auto blockNumber = getCurrentBlockNumber();
    SCHEDULER_LOG(DEBUG) << "Call request: " << LOG_KV("blockNumber", blockNumber)
                         << LOG_KV("address", tx->to());

    // set attribute before call
    tx->setAttribute(m_isWasm ? bcos::protocol::Transaction::Attribute::LIQUID_SCALE_CODEC :
                                bcos::protocol::Transaction::Attribute::EVM_ABI_CODEC);
    // Create temp block
    auto block = m_blockFactory->createBlock();
    block->blockHeader()->setNumber(blockNumber);
    block->blockHeader()->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());
    block->appendTransaction(std::move(tx));
    block->blockHeader()->setVersion(m_blockVersion);

    // Create temp executive

    // auto blockExecutive = std::make_shared<SerialBlockExecutive>(std::move(block), this,
    //     m_calledContextID.fetch_add(1), m_transactionSubmitResultFactory, true, m_blockFactory,
    //     m_txPool, m_gasLimit, false);
    auto blockExecutive = m_blockExecutiveFactory->build(std::move(block), this,
        m_calledContextID.fetch_add(1), m_transactionSubmitResultFactory, true, m_blockFactory,
        m_txPool, m_gasLimit, getGasPrice(), false);
    blockExecutive->setOnNeedSwitchEventHandler([this]() { triggerSwitch(); });

    blockExecutive->asyncCall([callback = std::move(callback)](Error::UniquePtr&& error,
                                  protocol::TransactionReceipt::Ptr&& receipt) {
        if (error)
        {
            std::string errorMessage = "asyncCall error: " + error->errorMessage();
            SCHEDULER_LOG(DEBUG) << errorMessage;
            callback(BCOS_ERROR_WITH_PREV_PTR(error->errorCode(), errorMessage, *error), nullptr);
            return;
        }
        SCHEDULER_LOG(DEBUG) << "Call success";
        callback(nullptr, std::move(receipt));
    });
}

void SchedulerImpl::reset(std::function<void(Error::Ptr&&)> callback)
{
    (void)callback;
}
// register a blockNumber receiver
void SchedulerImpl::registerBlockNumberReceiver(
    std::function<void(protocol::BlockNumber blockNumber)> callback)
{
    m_blockNumberReceiver = [callback = std::move(callback)](
                                protocol::BlockNumber blockNumber) { callback(blockNumber); };
}

void SchedulerImpl::getCode(
    std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback)
{
    auto executor = m_executorManager->dispatchExecutor(contract);
    executor->getCode(contract, [this, callback = std::move(callback)](
                                    Error::Ptr error, bcos::bytes code) {
        if (error && error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
        {
            triggerSwitch();
        }
        callback(std::move(error), std::move(code));
    });
}

void SchedulerImpl::getABI(
    std::string_view contract, std::function<void(Error::Ptr, std::string)> callback)
{
    auto executor = m_executorManager->dispatchExecutor(contract);
    executor->getABI(contract, [this, callback = std::move(callback)](
                                   Error::Ptr error, std::string abi) {
        if (error && error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
        {
            triggerSwitch();
        }
        callback(std::move(error), std::move(abi));
    });
}

void SchedulerImpl::registerTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
        bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)>
        txNotifier)
{
    m_txNotifier = [callback = std::move(txNotifier)](bcos::protocol::BlockNumber blockNumber,
                       bcos::protocol::TransactionSubmitResultsPtr resultsPtr,
                       std::function<void(Error::Ptr)> _callback) {
        callback(blockNumber, resultsPtr, std::move(_callback));
    };
}

BlockExecutive::Ptr SchedulerImpl::getPreparedBlock(
    bcos::protocol::BlockNumber blockNumber, int64_t timestamp)
{
    bcos::ReadGuard readGuard(x_preparedBlockMutex);

    if (m_preparedBlocks.count(blockNumber) != 0 &&
        m_preparedBlocks[blockNumber].count(timestamp) != 0)
    {
        return m_preparedBlocks[blockNumber][timestamp];
    }

    return nullptr;
}


void SchedulerImpl::setPreparedBlock(
    bcos::protocol::BlockNumber blockNumber, int64_t timestamp, BlockExecutive::Ptr blockExecutive)
{
    bcos::WriteGuard writeGuard(x_preparedBlockMutex);

    m_preparedBlocks[blockNumber][timestamp] = blockExecutive;
}

void SchedulerImpl::removeAllOldPreparedBlock(bcos::protocol::BlockNumber oldBlockNumber)
{
    bcos::WriteGuard writeGuard(x_preparedBlockMutex);

    // erase all preparedBlock <= oldBlockNumber
    for (auto itr = m_preparedBlocks.begin(); itr != m_preparedBlocks.end();)
    {
        if (itr->first <= oldBlockNumber)
        {
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("prepareBlockExecutive")
                                 << LOG_DESC("removeAllOldPreparedBlock")
                                 << LOG_KV("blockNumber", itr->first);
            itr = m_preparedBlocks.erase(itr);
        }
        else
        {
            itr++;
        }
    }
}

void SchedulerImpl::removeAllPreparedBlock()
{
    bcos::WriteGuard writeGuard(x_preparedBlockMutex);
    m_preparedBlocks.clear();

    SCHEDULER_LOG(DEBUG) << LOG_BADGE("prepareBlockExecutive")
                         << LOG_DESC("removeAllPreparedBlock");
}

void SchedulerImpl::preExecuteBlock(
    bcos::protocol::Block::Ptr block, bool verify, std::function<void(Error::Ptr&&)> _callback)
{
    auto startT = utcTime();
    SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(block->blockHeaderConst()->number())
                         << "preExeBlock request"
                         << LOG_KV("txCount",
                                block->transactionsSize() + block->transactionsMetaDataSize())
                         << LOG_KV("startT(ms)", startT);

    auto callback = [startT, _callback = std::move(_callback),
                        number = block->blockHeaderConst()->number()](bcos::Error::Ptr&& error) {
        SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number) << METRIC << "preExeBlock response"
                             << LOG_KV("message", error ? error->what() : "ok")
                             << LOG_KV("cost(ms)", utcTime() - startT);
        _callback(error == nullptr ? nullptr : std::move(error));
    };

    try
    {
        auto blockNumber = block->blockHeaderConst()->number();
        int64_t timestamp = block->blockHeaderConst()->timestamp();
        BlockExecutive::Ptr blockExecutive = getPreparedBlock(blockNumber, timestamp);

        if (blockExecutive != nullptr)
        {
            SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(blockNumber) << LOG_BADGE("prepareBlockExecutive")
                                 << "Duplicate block to prepare, dropped."
                                 << LOG_KV("blockHeader.timestamp", timestamp);
            callback(nullptr);  // also success
            return;
        }

        // blockExecutive = std::make_shared<SerialBlockExecutive>(std::move(block), this,
        // 0,
        //     m_transactionSubmitResultFactory, false, m_blockFactory, m_txPool,
        //     m_gasLimit, verify);
        fetchConfig(blockNumber);
        // Note: must build blockExecutive before enqueue() for executeBlock use the same
        // blockExecutive
        blockExecutive = m_blockExecutiveFactory->build(std::move(block), this, 0,
            m_transactionSubmitResultFactory, false, m_blockFactory, m_txPool, m_gasLimit,
            getGasPrice(), verify);

        blockExecutive->setOnNeedSwitchEventHandler([this]() { triggerSwitch(); });

        setPreparedBlock(blockNumber, timestamp, blockExecutive);

        m_preExeWorker.enqueue(
            [this, blockNumber, timestamp, blockExecutive, callback = std::move(callback)]() {
                try
                {
                    if (!m_isRunning)
                    {
                        return;
                    }

                    auto currentBlockNumber = getCurrentBlockNumber();
                    if (blockNumber <= currentBlockNumber)
                    {
                        SCHEDULER_LOG(DEBUG) << "preExeBlock: block has executed. "
                                             << LOG_KV("needNumber", blockNumber)
                                             << LOG_KV("currentNumber", currentBlockNumber)
                                             << LOG_KV("timestamp", timestamp);
                        callback(nullptr);
                        return;
                    }

                    blockExecutive->prepare();

                    callback(nullptr);
                }
                catch (bcos::Error& e)
                {
                    SCHEDULER_LOG(WARNING)
                        << "preExeBlock in worker exception: " << LOG_KV("code", e.errorCode())
                        << LOG_KV("message", e.errorMessage());
                    callback(BCOS_ERROR_PTR(e.errorCode(), e.errorMessage()));
                }
            });
    }
    catch (bcos::Error& e)
    {
        SCHEDULER_LOG(WARNING) << "preExeBlock exception: " << LOG_KV("code", e.errorCode())
                               << LOG_KV("message", e.errorMessage());
        callback(BCOS_ERROR_PTR(e.errorCode(), e.errorMessage()));
    }
}

bcos::protocol::BlockNumber SchedulerImpl::getBlockNumberFromStorage()
{
    std::promise<protocol::BlockNumber> blockNumberFuture;
    m_ledger->asyncGetBlockNumber(
        [&blockNumberFuture](Error::Ptr error, protocol::BlockNumber number) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Get blockNumber from storage failed" << LOG_KV("msg", error->errorMessage())
                    << LOG_KV("code", error->errorCode());
                blockNumberFuture.set_value(-1);
            }
            else
            {
                blockNumberFuture.set_value(number);
            }
        });

    return blockNumberFuture.get_future().get();
}

bcos::protocol::BlockNumber SchedulerImpl::getCurrentBlockNumber()
{
    std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
    if (m_blocks->empty())
    {
        blocksLock.unlock();
        return getBlockNumberFromStorage();
    }
    return m_blocks->front()->number() - 1;
}

void SchedulerImpl::fetchConfig(protocol::BlockNumber _number)
{
    if (m_gasLimit > 0 && m_blockVersion > 0)
    {
        return;
    }

    {
        std::promise<std::tuple<Error::Ptr, std::string>> p;
        m_ledger->asyncGetSystemConfigByKey(ledger::SYSTEM_KEY_TX_GAS_PRICE,
            [&p](Error::Ptr _e, std::string _value, protocol::BlockNumber) {
                p.set_value(std::make_tuple(std::move(_e), std::move(_value)));
                return;
            });
        auto [e, value] = p.get_future().get();
        if (e)
        {
            SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(_number) << LOG_DESC("fetchGasPrice failed")
                                 << LOG_KV("code", e->errorCode())
                                 << LOG_KV("message", e->errorMessage());
            // BOOST_THROW_EXCEPTION(
            //     BCOS_ERROR(SchedulerError::fetchGasLimitError, e->errorMessage()));
            value = "0x0";
        }

        setGasPrice(value);  // must use function to acquire lock
    }

    SCHEDULER_LOG(INFO) << LOG_DESC("fetch gas limit from storage before execute block")
                        << LOG_KV("requestBlockNumber", _number);

    {
        std::promise<std::tuple<Error::Ptr, std::string>> p;
        m_ledger->asyncGetSystemConfigByKey(ledger::SYSTEM_KEY_TX_GAS_LIMIT,
            [&p](Error::Ptr _e, std::string _value, protocol::BlockNumber) {
                p.set_value(std::make_tuple(std::move(_e), std::move(_value)));
                return;
            });
        auto [e, value] = p.get_future().get();
        if (e)
        {
            SCHEDULER_LOG(WARNING)
                << BLOCK_NUMBER(_number) << LOG_DESC("fetchGasLimit failed")
                << LOG_KV("code", e->errorCode()) << LOG_KV("message", e->errorMessage());
            BOOST_THROW_EXCEPTION(
                BCOS_ERROR(SchedulerError::fetchGasLimitError, e->errorMessage()));
        }

        // cast must be success
        m_gasLimit = boost::lexical_cast<uint64_t>(value);
    }
    {
        std::promise<std::tuple<Error::Ptr, std::string>> p;
        m_ledger->asyncGetSystemConfigByKey(ledger::SYSTEM_KEY_COMPATIBILITY_VERSION,
            [&p](Error::Ptr _e, std::string _value, protocol::BlockNumber) {
                p.set_value(std::make_tuple(std::move(_e), std::move(_value)));
                return;
            });
        auto [e, value] = p.get_future().get();
        if (e)
        {
            SCHEDULER_LOG(WARNING)
                << BLOCK_NUMBER(_number) << LOG_DESC("fetchCompatibilityVersion failed")
                << LOG_KV("code", e->errorCode()) << LOG_KV("message", e->errorMessage());
            BOOST_THROW_EXCEPTION(
                BCOS_ERROR(SchedulerError::InvalidBlockVersion, e->errorMessage()));
        }

        // cast must be success
        m_blockVersion = bcos::tool::toVersionNumber(value);
    }
}

BlockExecutive::Ptr SchedulerImpl::getLatestPreparedBlock(bcos::protocol::BlockNumber blockNumber)
{
    bcos::ReadGuard readGuard(x_preparedBlockMutex);

    auto needBlocksMapIt = m_preparedBlocks.find(blockNumber);
    if (needBlocksMapIt == m_preparedBlocks.end())
    {
        return nullptr;
    }
    else
    {
        // get the biggest timestamp block
        auto blockIt = needBlocksMapIt->second.end();
        blockIt--;
        return blockIt->second;
    }
}

void SchedulerImpl::tryExecuteBlock(
    bcos::protocol::BlockNumber number, bcos::crypto::HashType parentHash)
{
    return;  // TODO: Fix blockHash bug here

    m_exeWorker.enqueue([this, number, &parentHash]() {
        if (!m_isRunning)
        {
            return;
        }
        auto blockExecutive = getLatestPreparedBlock(number);
        if (!blockExecutive)
        {
            return;
        }
        auto block = blockExecutive->block();
        if (!block)
        {
            return;
        }
        bcos::protocol::ParentInfoList parentInfoList;
        bcos::protocol::ParentInfo parentInfo{number, std::move(parentHash)};
        parentInfoList.push_back(parentInfo);
        block->blockHeader()->setParentInfo(parentInfoList);
        block->blockHeader()->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());

        auto timestamp = block->blockHeaderConst()->timestamp();
        SCHEDULER_LOG(INFO) << "tryExecuteBlock request" << LOG_KV("number", number)
                            << LOG_KV("timestamp", timestamp);
        executeBlock(block, false,
            [number, timestamp](bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&& blockHeader,
                bool _sysBlock) {
                SCHEDULER_LOG(INFO) << "tryExecuteBlock success" << LOG_KV("number", number)
                                    << LOG_KV("timestamp", timestamp);
            });
    });
}
