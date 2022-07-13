#include "Common.h"
#include "SchedulerImpl.h"
#include "SerialBlockExecutive.h"
#include "bcos-framework/interfaces/executor/ExecuteError.h"
#include <bcos-framework/interfaces/ledger/LedgerConfig.h>
#include <bcos-framework/interfaces/protocol/GlobalConfig.h>
#include <bcos-framework/interfaces/protocol/ProtocolTypeDef.h>
#include <bcos-tool/VersionConverter.h>
#include <bcos-utilities/Error.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <memory>
#include <mutex>
#include <variant>


using namespace bcos::scheduler;

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

    std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
    try
    {
        if (m_blocks->empty())
        {
            bcos::protocol::BlockNumber number = getBlockNumberFromStorage();
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
                blocksLock.unlock();
                whenQueueFront(m_blocks->front());

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
        SCHEDULER_LOG(ERROR) << "handleBlockQueue exception" << e.what();
        blocksLock.unlock();
        whenException(e);
    }
}


void SchedulerImpl::executeBlock(bcos::protocol::Block::Ptr block, bool verify,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool _sysBlock)>
        _callback)
{
    if (block->blockHeader()->version() > (uint32_t)g_BCOSConfig.maxSupportedVersion())
    {
        auto errorMessage = "The block version is larger than maxSupportedVersion";
        SCHEDULER_LOG(WARNING) << errorMessage << LOG_KV("version", block->version())
                               << LOG_KV("maxSupportedVersion", g_BCOSConfig.maxSupportedVersion());
        _callback(std::make_shared<bcos::Error>(SchedulerError::InvalidBlockVersion, errorMessage),
            nullptr, false);
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
        fetchGasLimit(requestBlockNumber);
    }
    catch (std::exception& e)
    {
        SCHEDULER_LOG(ERROR) << "fetchGasLimit exception: " << boost::diagnostic_information(e);
        _callback(BCOS_ERROR_WITH_PREV_PTR(
                      SchedulerError::fetchGasLimitError, "etchGasLimit exception", e),
            nullptr, false);
        return;
    }

    SCHEDULER_LOG(INFO) << METRIC << "ExecuteBlock request"
                        << LOG_KV("blockNumber", requestBlockNumber)
                        << LOG_KV("gasLimit", m_gasLimit) << LOG_KV("verify", verify)
                        << LOG_KV("signatureSize", signature.size())
                        << LOG_KV("tx count", block->transactionsSize())
                        << LOG_KV("meta tx count", block->transactionsMetaDataSize())
                        << LOG_KV("version", (bcos::protocol::Version)(block->version()))
                        << LOG_KV("waitT", waitT);

    auto callback = [requestBlockNumber, _callback = std::move(_callback)](bcos::Error::Ptr&& error,
                        bcos::protocol::BlockHeader::Ptr&& blockHeader, bool _sysBlock) {
        SCHEDULER_LOG(INFO) << METRIC << "ExecuteBlock response"
                            << LOG_KV(error ? "error" : "ok", error ? error->what() : "ok")
                            << LOG_KV("block number", requestBlockNumber);
        _callback(error == nullptr ? nullptr : std::move(error), std::move(blockHeader), _sysBlock);
    };


    auto whenOlder = [requestBlockNumber, callback](bcos::protocol::BlockNumber number) {
        auto message = (boost::format("Execute an history block %d, current number is %d") %
                        requestBlockNumber % number)
                           .str();
        SCHEDULER_LOG(ERROR) << message;
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlocks, message), nullptr, false);
    };

    auto whenInQueue = [callback, requestBlockNumber, &signature, verify](
                           BlockExecutive::Ptr blockExecutive) {
        auto blockHeader = blockExecutive->result();

        if (blockHeader == nullptr)
        {
            auto message = "hit block cache, but block is executing!";
            SCHEDULER_LOG(ERROR) << "ExecuteBlock error, " << message;
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, message), nullptr, false);
        }
        else
        {
            SCHEDULER_LOG(INFO) << "ExecuteBlock success, return executed block"
                                << LOG_KV("blockNumber", requestBlockNumber)
                                << LOG_KV("signatureSize", signature.size())
                                << LOG_KV("verify", verify);
            callback(nullptr, std::move(blockHeader), blockExecutive->sysBlock());
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
        SCHEDULER_LOG(ERROR) << "ExecuteBlock error, " << message;
        callback(
            BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlockNumber, message), nullptr, false);
    };
    auto whenException = [callback](std::exception const& e) {
        auto message = (boost::format("ExecuteBlock exception %s") % e.what()).str();
        SCHEDULER_LOG(ERROR) << message;
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
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("preExecuteBlock")
                                 << "Not hit prepared block executive, create."
                                 << LOG_KV("blockNumber", block->blockHeaderConst()->number());
            // blockExecutive = std::make_shared<SerialBlockExecutive>(std::move(block), this, 0,
            //     m_transactionSubmitResultFactory, false, m_blockFactory, m_txPool, m_gasLimit,
            //     verify);
            blockExecutive = m_blockExecutiveFactory->build(std::move(block), this, 0,
                m_transactionSubmitResultFactory, false, m_blockFactory, m_txPool, m_gasLimit,
                verify);

            blockExecutive =
                blockExecutive->setOnNeedSwitchEventHandler([this]() { triggerSwitch(); });
        }
        else
        {
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("preExecuteBlock")
                                 << "Hit prepared block executive cache, use it."
                                 << LOG_KV("blockNumber", block->blockHeaderConst()->number());
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
    auto whenQueueBack = [this, &executeLock, &blockExecutive, callback]() {
        if (!executeLock)
        {
            // if not acquire the lock, return error
            auto message = "Another block is executing!";
            SCHEDULER_LOG(ERROR) << "ExecuteBlock error, " << message;
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, message), nullptr, false);
            return;
        }

        blockExecutive->asyncExecute([this, callback = std::move(callback), executeLock](
                                         Error::UniquePtr error, protocol::BlockHeader::Ptr header,
                                         bool _sysBlock) {
            if (!m_isRunning)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "Scheduler is not running"),
                    nullptr, false);
                return;
            }

            if (error)
            {
                SCHEDULER_LOG(ERROR) << "executeBlock error: " << error->what();
                {
                    std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
                    m_blocks->pop_back();
                    blocksLock.unlock();
                }
                executeLock->unlock();
                callback(BCOS_ERROR_WITH_PREV_PTR(
                             SchedulerError::UnknownError, "executeBlock error:", *error),
                    nullptr, _sysBlock);
                return;
            }
            auto signature = header->signatureList();
            SCHEDULER_LOG(INFO) << "ExecuteBlock success" << LOG_KV("blockNumber", header->number())
                                << LOG_KV("hash", header->hash().abridged())
                                << LOG_KV("state root", header->stateRoot().hex())
                                << LOG_KV("receiptRoot", header->receiptsRoot().hex())
                                << LOG_KV("txsRoot", header->txsRoot().abridged())
                                << LOG_KV("gasUsed", header->gasUsed())
                                << LOG_KV("signatureSize", signature.size());

            m_lastExecuteFinishTime = utcTime();
            executeLock->unlock();
            callback(std::move(error), std::move(header), _sysBlock);
        });
    };

    // run all handler
    handleBlockQueue(requestBlockNumber, whenOlder, whenQueueFront, afterFront, whenNotFrontInQueue,
        beforeBack, whenQueueBack, whenNewer, whenException);
}

void SchedulerImpl::commitBlock(bcos::protocol::BlockHeader::Ptr header,
    std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> _callback)
{
    SCHEDULER_LOG(INFO) << "CommitBlock request" << LOG_KV("blockNumber", header->number());

    auto requestBlockNumber = header->number();
    auto callback = [requestBlockNumber, _callback = std::move(_callback)](
                        bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config) {
        SCHEDULER_LOG(INFO) << METRIC << "CommitBlock response"
                            << LOG_KV(error ? "error" : "ok", error ? error->what() : "ok")
                            << LOG_KV("block number", requestBlockNumber);
        _callback(error == nullptr ? nullptr : std::move(error), std::move(config));
    };


    auto whenOlder = [requestBlockNumber, callback](
                         bcos::protocol::BlockNumber currentBlockNumber) {
        auto message = (boost::format("commit an history block %d, current number is %d") %
                        requestBlockNumber % currentBlockNumber)
                           .str();
        SCHEDULER_LOG(ERROR) << message;
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlocks, message), nullptr);
    };

    auto whenAfterFront = [this, requestBlockNumber, callback]() {
        auto message = "A smaller block need to commit, requestBlockNumber " +
                       std::to_string(requestBlockNumber) +
                       +", need to commit blockNumber: " + std::to_string(getCurrentBlockNumber());
        SCHEDULER_LOG(ERROR) << "CommitBlock error, " << message;

        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlockNumber, message), nullptr);
    };

    auto whenNotFrontInQueue = [whenAfterFront](BlockExecutive::Ptr) { whenAfterFront(); };

    auto beforeBack = []() {
        // do nothing
    };

    auto whenQueueBack = [whenAfterFront]() { whenAfterFront(); };

    auto whenNewer = [whenAfterFront](bcos::protocol::BlockNumber) { whenAfterFront(); };
    auto whenException = [callback](std::exception const& e) {
        auto message = (boost::format("CommitBlock exception %s") % e.what()).str();
        SCHEDULER_LOG(ERROR) << message;
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::UnknownError, message), nullptr);
    };

    auto whenQueueFront = [this, requestBlockNumber, header = std::move(header), callback](
                              BlockExecutive::Ptr blockExecutive) {
        // acquire lock
        std::shared_ptr<std::unique_lock<std::mutex>> commitLock =
            std::make_shared<std::unique_lock<std::mutex>>(m_commitMutex, std::try_to_lock);

        if (!commitLock->owns_lock())
        {
            std::string message =
                (boost::format("commitBlock: Another block is committing! Block "
                               "number: %ld, hash: %s") %
                    requestBlockNumber %
                    m_blocks->front()->block()->blockHeaderConst()->hash().abridged())
                    .str();


            SCHEDULER_LOG(ERROR) << "CommitBlock error, " << message;
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
        blockExecutive->asyncCommit([this, callback = std::move(callback), blockExecutive,
                                        block = blockExecutive->block(),
                                        commitLock](Error::UniquePtr&& error) {
            if (!m_isRunning)
            {
                commitLock->unlock();
                callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "Scheduler is not running"),
                    nullptr);
                return;
            }

            if (error)
            {
                SCHEDULER_LOG(ERROR) << "CommitBlock error, " << error->errorMessage();

                commitLock->unlock();
                callback(BCOS_ERROR_UNIQUE_PTR(
                             error->errorCode(), "CommitBlock error: " + error->errorMessage()),
                    nullptr);
                return;
            }

            {
                std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
                bcos::protocol::BlockNumber number = m_blocks->front()->number();
                if (number != block->blockHeaderConst()->number())
                {
                    SCHEDULER_LOG(FATAL)
                        << "The committed block is not equal to blockQueue front block";
                }
                removeAllOldPreparedBlock(number);
                m_blocks->pop_front();
                SCHEDULER_LOG(DEBUG) << "Remove committed block: " << number << " success";
            }

            asyncGetLedgerConfig([this, commitLock = std::move(commitLock), blockExecutive,
                                     callback = std::move(callback)](
                                     Error::Ptr error, ledger::LedgerConfig::Ptr ledgerConfig) {
                if (!m_isRunning)
                {
                    commitLock->unlock();
                    callback(
                        BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "Scheduler is not running"),
                        nullptr);
                    return;
                }
                if (error)
                {
                    SCHEDULER_LOG(ERROR) << "Get system config error, " << error->errorMessage();

                    commitLock->unlock();
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                 SchedulerError::UnknownError, "Get system config error", *error),
                        nullptr);
                    return;
                }

                auto blockNumber = ledgerConfig->blockNumber();
                auto gasNumber = ledgerConfig->gasLimit();
                // Note: takes effect in next block. we query the enableNumber of blockNumber
                // + 1.
                if (std::get<1>(gasNumber) <= (blockNumber + 1))
                {
                    m_gasLimit = std::get<0>(gasNumber);
                }

                SCHEDULER_LOG(INFO) << "CommitBlock success" << LOG_KV("blockNumber", blockNumber)
                                    << LOG_KV("gas limit", m_gasLimit);

                // Note: blockNumber = 0, means system deploy, and tx is not existed in txpool.
                // So it should not exec tx notifier
                if (m_txNotifier && blockNumber != 0)
                {
                    SCHEDULER_LOG(INFO) << "Start notify block result: " << blockNumber;
                    blockExecutive->asyncNotify(m_txNotifier,
                        [this, blockNumber, callback = std::move(callback),
                            ledgerConfig = std::move(ledgerConfig),
                            commitLock = std::move(commitLock)](Error::Ptr _error) mutable {
                            if (!m_isRunning)
                            {
                                callback(BCOS_ERROR_UNIQUE_PTR(
                                             SchedulerError::Stopped, "Scheduler is not running"),
                                    nullptr);
                                return;
                            }

                            if (m_blockNumberReceiver)
                            {
                                m_blockNumberReceiver(blockNumber);
                            }

                            SCHEDULER_LOG(INFO) << "End notify block result: " << blockNumber;


                            commitLock->unlock();
                            // Note: only after the block notify finished can call the callback
                            callback(std::move(_error), std::move(ledgerConfig));
                        });
                }
                else
                {
                    commitLock->unlock();
                    callback(nullptr, std::move(ledgerConfig));
                }
            });
        });
    };

    auto afterFront = []() {
        // do nothing
    };

    // run all handler
    handleBlockQueue(requestBlockNumber, whenOlder, whenQueueFront, afterFront, whenNotFrontInQueue,
        beforeBack, whenQueueBack, whenNewer, whenException);
}

/*

void SchedulerImpl::executeBlock(bcos::protocol::Block::Ptr block, bool verify,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool _sysBlock)>
        _callback)
{
    if (block->blockHeader()->version() > (uint32_t)g_BCOSConfig.maxSupportedVersion())
    {
        auto errorMessage = "The block version is larger than maxSupportedVersion";
        SCHEDULER_LOG(WARNING) << errorMessage << LOG_KV("version", block->version())
                               << LOG_KV("maxSupportedVersion",
g_BCOSConfig.maxSupportedVersion());
        _callback(std::make_shared<bcos::Error>(SchedulerError::InvalidBlockVersion,
errorMessage), nullptr, false); return;
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
    try
    {
        fetchGasLimit(block->blockHeaderConst()->number());
    }
    catch (std::exception& e)
    {
        SCHEDULER_LOG(ERROR) << "fetchGasLimit exception: " << boost::diagnostic_information(e);
        _callback(BCOS_ERROR_WITH_PREV_PTR(
                      SchedulerError::fetchGasLimitError, "etchGasLimit exception", e),
            nullptr, false);
        return;
    }


    SCHEDULER_LOG(INFO) << METRIC << "ExecuteBlock request"
                        << LOG_KV("blockNumber", block->blockHeaderConst()->number())
                        << LOG_KV("gasLimit", m_gasLimit) << LOG_KV("verify", verify)
                        << LOG_KV("signatureSize", signature.size())
                        << LOG_KV("tx count", block->transactionsSize())
                        << LOG_KV("meta tx count", block->transactionsMetaDataSize())
                        << LOG_KV("version", (bcos::protocol::Version)(block->version()))
                        << LOG_KV("waitT", waitT);

    auto callback = [_callback = std::move(_callback)](bcos::Error::Ptr&& error,
                        bcos::protocol::BlockHeader::Ptr&& blockHeader, bool _sysBlock) {
        SCHEDULER_LOG(INFO) << METRIC << "ExecuteBlock response"
                            << LOG_KV(error ? "error" : "ok", error ? error->what() : "ok")
                            << LOG_KV("block number",
                                   blockHeader ? std::to_string(blockHeader->number()) :
"null"); _callback(error == nullptr ? nullptr : std::move(error), std::move(blockHeader),
_sysBlock);
    };


    std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
    // Note: if hit the cache, may return synced blockHeader with signatureList in some cases
    if (!m_blocks->empty())
    {
        auto requestNumber = block->blockHeaderConst()->number();
        auto& frontBlock = m_blocks->front();
        auto& backBlock = m_blocks->back();
        // Block already executed
        if (requestNumber >= frontBlock->number() && requestNumber <= backBlock->number())
        {
            auto it = m_blocks->begin();
            while (it->get()->number() != requestNumber)
            {
                ++it;
            }

            SCHEDULER_LOG(TRACE) << "BlockHeader stateRoot: " << std::hex
                                 << it->get()->result()->stateRoot();

            auto blockHeader = it->get()->result();

            if (blockHeader == nullptr)
            {
                auto message = "hit block cache, but block is executing!";
                SCHEDULER_LOG(ERROR) << "ExecuteBlock error, " << message;
                blocksLock.unlock();
                callback(
                    BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, message), nullptr,
false); return;
            }
            else
            {
                SCHEDULER_LOG(INFO)
                    << "ExecuteBlock success, return executed block"
                    << LOG_KV("blockNumber", block->blockHeaderConst()->number())
                    << LOG_KV("signatureSize", signature.size()) << LOG_KV("verify", verify);
                blocksLock.unlock();
                callback(nullptr, std::move(blockHeader), it->get()->sysBlock());
                return;
            }
        }

        if (requestNumber - backBlock->number() != 1)
        {
            auto message =
                "Invalid blockNumber: " +
                boost::lexical_cast<std::string>(block->blockHeaderConst()->number()) +
                " current last number: " +
boost::lexical_cast<std::string>(backBlock->number()); SCHEDULER_LOG(ERROR) << "ExecuteBlock
error, " << message;

            blocksLock.unlock();
            callback(BCOS_ERROR_PTR(SchedulerError::InvalidBlockNumber, std::move(message)),
                nullptr, false);

            return;
        }
    }
    else
    {
        auto currentBlockNumber = getBlockNumberInStorage();

        if (currentBlockNumber != 0 &&
            currentBlockNumber + 1 != block->blockHeaderConst()->number())
        {
            auto message =
                (boost::format(
                     "Try to execute an discontinuous block: %ld, last current blockNumber:
%ld") % block->blockHeaderConst()->number() % currentBlockNumber) .str(); SCHEDULER_LOG(ERROR)
<< "ExecuteBlock error, " << message; callback(
                BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlockNumber, message), nullptr,
false); return;
        }
    }

    BlockExecutive::Ptr blockExecutive = getPreparedBlock(
        block->blockHeaderConst()->number(), block->blockHeaderConst()->timestamp());

    if (blockExecutive == nullptr)
    {
        // the block has not been prepared, just make a new one here
        SCHEDULER_LOG(DEBUG) << LOG_BADGE("preExecuteBlock")
                             << "Not hit prepared block executive, create."
                             << LOG_KV("blockNumber", block->blockHeaderConst()->number());
        blockExecutive = std::make_shared<SerialBlockExecutive>(std::move(block), this, 0,
            m_transactionSubmitResultFactory, false, m_blockFactory, m_txPool, m_gasLimit,
verify); blockExecutive->setOnNeedSwitchEventHandler([this]() { triggerSwitch(); });
    }
    else
    {
        SCHEDULER_LOG(DEBUG) << LOG_BADGE("preExecuteBlock")
                             << "Hit prepared block executive cache, use it."
                             << LOG_KV("blockNumber", block->blockHeaderConst()->number());
        blockExecutive->block()->setBlockHeader(block->blockHeader());
    }

    auto executeLock =
        std::make_shared<std::unique_lock<std::mutex>>(m_executeMutex, std::try_to_lock);
    if (!executeLock->owns_lock())
    {
        auto message = "Another block is executing!";
        SCHEDULER_LOG(ERROR) << "ExecuteBlock error, " << message;
        blocksLock.unlock();
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, message), nullptr, false);
        return;
    }

    m_blocks->emplace_back(blockExecutive);

    blockExecutive = m_blocks->back();

    blocksLock.unlock();
    blockExecutive->asyncExecute(
        [this, callback = std::move(callback), executeLock](
            Error::UniquePtr error, protocol::BlockHeader::Ptr header, bool _sysBlock) {
            if (!m_isRunning)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "Scheduler is not
running"), nullptr, false); return;
            }

            if (error)
            {
                SCHEDULER_LOG(ERROR) << "executeBlock error: " << error->what();
                {
                    std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
                    m_blocks->pop_back();
                }
                executeLock->unlock();
                callback(BCOS_ERROR_WITH_PREV_PTR(
                             SchedulerError::UnknownError, "executeBlock error:", *error),
                    nullptr, _sysBlock);
                return;
            }
            auto signature = header->signatureList();
            SCHEDULER_LOG(INFO) << "ExecuteBlock success" << LOG_KV("blockNumber",
header->number())
                                << LOG_KV("hash", header->hash().abridged())
                                << LOG_KV("state root", header->stateRoot().hex())
                                << LOG_KV("receiptRoot", header->receiptsRoot().hex())
                                << LOG_KV("txsRoot", header->txsRoot().abridged())
                                << LOG_KV("gasUsed", header->gasUsed())
                                << LOG_KV("signatureSize", signature.size());

            m_lastExecuteFinishTime = utcTime();
            executeLock->unlock();
            callback(std::move(error), std::move(header), _sysBlock);
        });
}

void SchedulerImpl::commitBlock(bcos::protocol::BlockHeader::Ptr header,
    std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> _callback)
{
    SCHEDULER_LOG(INFO) << "CommitBlock request" << LOG_KV("blockNumber", header->number());

    auto callback = [blockNumber = header->number(), _callback = std::move(_callback)](
                        bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config) {
        SCHEDULER_LOG(INFO) << METRIC << "CommitBlock response"
                            << LOG_KV(error ? "error" : "ok", error ? error->what() : "ok")
                            << LOG_KV("block number", blockNumber);
        _callback(error == nullptr ? nullptr : std::move(error), std::move(config));
    };

    bcos::protocol::BlockNumber currentBlockNumber = getBlockNumberInStorage();
    // Note: skip check for sys contract deploy
    if (!isSysContractDeploy(header->number()) && header->number() <= currentBlockNumber)
    {
        SCHEDULER_LOG(DEBUG) << "Block has committed, just return"
                             << LOG_KV("toCommitNumber", header->number())
                             << LOG_KV("currentNumber", currentBlockNumber);
        asyncGetLedgerConfig([this, callback = std::move(callback)](
                                 Error::Ptr error, ledger::LedgerConfig::Ptr ledgerConfig) {
            if (!m_isRunning)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "Scheduler is not
running"), nullptr); return;
            }
            if (error)
            {
                SCHEDULER_LOG(ERROR) << "Get system config error, " << error->errorMessage();

                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             SchedulerError::UnknownError, "Get system config error", *error),
                    nullptr);
                return;
            }
            callback(nullptr, std::move(ledgerConfig));
        });

        return;
    }

    auto commitLock =
        std::make_shared<std::unique_lock<std::mutex>>(m_commitMutex, std::try_to_lock);
    if (!commitLock->owns_lock())
    {
        std::string message;
        SchedulerError errorStatus;
        {
            std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
            if (m_blocks->empty())
            {
                errorStatus = SchedulerError::InvalidStatus;
                message = (boost::format("commitBlock: empty block queue, maybe the block has
been " "committed! Block number: %ld, hash: %s") % header->number() % header->hash().abridged())
                              .str();
            }
            else
            {
                auto& frontBlock = m_blocks->front();
                errorStatus = SchedulerError::BlockIsCommitting;
                message =
                    (boost::format(
                         "commitBlock: Another block is committing! Block number: %ld, hash:
%s") % frontBlock->block()->blockHeaderConst()->number() %
                        frontBlock->block()->blockHeaderConst()->hash().abridged())
                        .str();
            }
        }
        SCHEDULER_LOG(ERROR) << "CommitBlock error, " << message;
        callback(BCOS_ERROR_UNIQUE_PTR(errorStatus, message), nullptr);
        return;
    }

    if (m_blocks->empty())
    {
        auto message = "No uncommitted block";
        SCHEDULER_LOG(ERROR) << "CommitBlock error, " << message;

        commitLock->unlock();
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlocks, message), nullptr);
        return;
    }

    auto& frontBlock = m_blocks->front();
    if (!frontBlock->result())
    {
        auto message = "Block is executing";
        SCHEDULER_LOG(ERROR) << "CommitBlock error, " << message;

        commitLock->unlock();
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::BlockIsCommitting, message), nullptr);
        return;
    }

    if (header->number() != frontBlock->number())
    {
        auto message = "Invalid blockNumber, available blockNumber: " +
                       boost::lexical_cast<std::string>(frontBlock->number());
        SCHEDULER_LOG(ERROR) << "CommitBlock error, " << message;

        commitLock->unlock();
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlockNumber, message), nullptr);
        return;
    }
    // Note: only when the signatureList is empty need to reset the header
    // in case of the signatureList of the header is accessing by the sync module while
frontBlock
    // is setting newBlockHeader, which will cause the signatureList ilegal
    auto executedHeader = frontBlock->block()->blockHeader();
    auto signature = executedHeader->signatureList();
    if (signature.empty())
    {
        frontBlock->block()->setBlockHeader(std::move(header));
    }
    frontBlock->asyncCommit([this, callback = std::move(callback), block = frontBlock->block(),
                                commitLock](Error::UniquePtr&& error) {
        if (!m_isRunning)
        {
            commitLock->unlock();
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "Scheduler is not running"),
                nullptr);
            return;
        }

        if (error)
        {
            SCHEDULER_LOG(ERROR) << "CommitBlock error, " << error->errorMessage();

            commitLock->unlock();
            callback(BCOS_ERROR_UNIQUE_PTR(
                         error->errorCode(), "CommitBlock error: " + error->errorMessage()),
                nullptr);
            return;
        }

        asyncGetLedgerConfig([this, commitLock = std::move(commitLock),
                                 callback = std::move(callback)](
                                 Error::Ptr error, ledger::LedgerConfig::Ptr ledgerConfig) {
            if (!m_isRunning)
            {
                commitLock->unlock();
                callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "Scheduler is not
running"), nullptr); return;
            }
            if (error)
            {
                SCHEDULER_LOG(ERROR) << "Get system config error, " << error->errorMessage();

                commitLock->unlock();
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             SchedulerError::UnknownError, "Get system config error", *error),
                    nullptr);
                return;
            }

            auto& frontBlock = m_blocks->front();
            auto blockNumber = ledgerConfig->blockNumber();
            auto gasNumber = ledgerConfig->gasLimit();
            // Note: takes effect in next block. we query the enableNumber of blockNumber + 1.
            if (std::get<1>(gasNumber) <= (blockNumber + 1))
            {
                m_gasLimit = std::get<0>(gasNumber);
            }

            SCHEDULER_LOG(INFO) << "CommitBlock success" << LOG_KV("blockNumber", blockNumber)
                                << LOG_KV("gas limit", m_gasLimit);

            // Note: blockNumber = 0, means system deploy, and tx is not existed in txpool.
            // So it should not exec tx notifier
            if (m_txNotifier && !isSysContractDeploy(blockNumber))
            {
                SCHEDULER_LOG(INFO) << "Start notify block result: " << blockNumber;
                frontBlock->asyncNotify(m_txNotifier,
                    [this, blockNumber, callback = std::move(callback),
                        ledgerConfig = std::move(ledgerConfig),
                        commitLock = std::move(commitLock)](Error::Ptr _error) mutable {
                        if (!m_isRunning)
                        {
                            callback(BCOS_ERROR_UNIQUE_PTR(
                                         SchedulerError::Stopped, "Scheduler is not running"),
                                nullptr);
                            return;
                        }

                        if (m_blockNumberReceiver)
                        {
                            m_blockNumberReceiver(blockNumber);
                        }

                        SCHEDULER_LOG(INFO) << "End notify block result: " << blockNumber;

                        {
                            std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
                            m_blocks->pop_front();
                            SCHEDULER_LOG(DEBUG)
                                << "Remove committed block: " << blockNumber << " success";
                        }

                        commitLock->unlock();
                        // Note: only after the block notify finished can call the callback
                        callback(std::move(_error), std::move(ledgerConfig));
                    });
            }
            else
            {
                {
                    std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
                    bcos::protocol::BlockNumber number = m_blocks->front()->number();
                    removeAllOldPreparedBlock(number);
                    m_blocks->pop_front();
                    SCHEDULER_LOG(DEBUG) << "Remove committed block: " << blockNumber << "
success";
                }

                commitLock->unlock();
                callback(nullptr, std::move(ledgerConfig));
            }
        });
    });
}


*/
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
        callback(BCOS_ERROR_PTR(SchedulerError::UnknownError, "Call address is empty"), nullptr);
        return;
    }
    // set attribute before call
    tx->setAttribute(m_isWasm ? bcos::protocol::Transaction::Attribute::LIQUID_SCALE_CODEC :
                                bcos::protocol::Transaction::Attribute::EVM_ABI_CODEC);
    // Create temp block
    auto block = m_blockFactory->createBlock();
    block->appendTransaction(std::move(tx));

    // Create temp executive

    // auto blockExecutive = std::make_shared<SerialBlockExecutive>(std::move(block), this,
    //     m_calledContextID.fetch_add(1), m_transactionSubmitResultFactory, true, m_blockFactory,
    //     m_txPool, m_gasLimit, false);
    auto blockExecutive =
        m_blockExecutiveFactory->build(std::move(block), this, m_calledContextID.fetch_add(1),
            m_transactionSubmitResultFactory, true, m_blockFactory, m_txPool, m_gasLimit, false);
    blockExecutive->setOnNeedSwitchEventHandler([this]() { triggerSwitch(); });

    blockExecutive->asyncCall([callback = std::move(callback)](Error::UniquePtr&& error,
                                  protocol::TransactionReceipt::Ptr&& receipt) {
        if (error)
        {
            std::string errorMessage = "asyncCall error: " + error->errorMessage();
            SCHEDULER_LOG(ERROR) << errorMessage;
            callback(BCOS_ERROR_WITH_PREV_PTR(error->errorCode(), errorMessage, *error), nullptr);
            return;
        }
        SCHEDULER_LOG(INFO) << "Call success";
        callback(nullptr, std::move(receipt));
    });
}

void SchedulerImpl::registerExecutor(std::string name,
    bcos::executor::ParallelTransactionExecutorInterface::Ptr executor,
    std::function<void(Error::Ptr&&)> callback)
{
    try
    {
        SCHEDULER_LOG(INFO) << "registerExecutor request: " << LOG_KV("name", name);
        m_executorManager->addExecutor(name, executor);
    }
    catch (std::exception& e)
    {
        SCHEDULER_LOG(ERROR) << "registerExecutor error: " << boost::diagnostic_information(e);
        callback(BCOS_ERROR_WITH_PREV_PTR(-1, "addExecutor error", e));
        return;
    }

    SCHEDULER_LOG(INFO) << "registerExecutor success";
    callback(nullptr);
}

void SchedulerImpl::unregisterExecutor(
    const std::string& name, std::function<void(Error::Ptr&&)> callback)
{
    (void)name;
    (void)callback;
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
    else
    {
        return nullptr;
    }
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

void SchedulerImpl::preExecuteBlock(
    bcos::protocol::Block::Ptr block, bool verify, std::function<void(Error::Ptr&&)> _callback)
{
    auto startT = utcTime();
    SCHEDULER_LOG(INFO) << "preExecuteBlock request"
                        << LOG_KV("blockNumber", block->blockHeaderConst()->number())
                        << LOG_KV("tx count",
                               block->transactionsSize() + block->transactionsMetaDataSize())
                        << LOG_KV("startT(ms)", startT);

    auto callback = [startT, _callback = std::move(_callback),
                        number = block->blockHeaderConst()->number()](bcos::Error::Ptr&& error) {
        SCHEDULER_LOG(INFO) << METRIC << "preExecuteBlock response"
                            << LOG_KV("message", error ? error->what() : "ok")
                            << LOG_KV("blockNumber", number)
                            << LOG_KV("cost(ms)", utcTime() - startT);
        _callback(error == nullptr ? nullptr : std::move(error));
    };

    auto blockNumber = block->blockHeaderConst()->number();
    int64_t timestamp = block->blockHeaderConst()->timestamp();
    BlockExecutive::Ptr blockExecutive = getPreparedBlock(blockNumber, timestamp);
    if (blockExecutive != nullptr)
    {
        SCHEDULER_LOG(DEBUG) << LOG_BADGE("prepareBlockExecutive")
                             << "Duplicate block to prepare, dropped."
                             << LOG_KV("blockHeader.timestamp", timestamp);
        callback(nullptr);  // also success
        return;
    }

    // blockExecutive = std::make_shared<SerialBlockExecutive>(std::move(block), this, 0,
    //     m_transactionSubmitResultFactory, false, m_blockFactory, m_txPool, m_gasLimit, verify);

    blockExecutive = m_blockExecutiveFactory->build(std::move(block), this, 0,
        m_transactionSubmitResultFactory, false, m_blockFactory, m_txPool, m_gasLimit, verify);

    blockExecutive->setOnNeedSwitchEventHandler([this]() { triggerSwitch(); });

    setPreparedBlock(blockNumber, timestamp, blockExecutive);

    blockExecutive->prepare();

    callback(nullptr);
}

template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20)
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

void SchedulerImpl::asyncGetLedgerConfig(
    std::function<void(Error::Ptr, ledger::LedgerConfig::Ptr ledgerConfig)> callback)
{
    auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));
    auto summary =
        std::make_shared<std::tuple<size_t, std::atomic_size_t, std::atomic_size_t>>(8, 0, 0);

    auto collector = [this, summary = std::move(summary), ledgerConfig = std::move(ledgerConfig),
                         callback = std::move(callbackPtr)](Error::Ptr error,
                         std::variant<std::tuple<bool, consensus::ConsensusNodeListPtr>,
                             std::tuple<int, std::string, bcos::protocol::BlockNumber>,
                             bcos::protocol::BlockNumber, bcos::crypto::HashType>&&
                             result) mutable {
        if (!m_isRunning)
        {
            (*callback)(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "Scheduler is not running"),
                nullptr);
            return;
        }

        auto& [total, success, failed] = *summary;

        if (error)
        {
            SCHEDULER_LOG(ERROR) << "Get ledger config with errors: " << error->errorMessage();
            ++failed;
        }
        else
        {
            std::visit(
                overloaded{
                    [&ledgerConfig](std::tuple<bool, consensus::ConsensusNodeListPtr>& nodeList) {
                        auto& [isSealer, list] = nodeList;

                        if (isSealer)
                        {
                            ledgerConfig->setConsensusNodeList(*list);
                        }
                        else
                        {
                            ledgerConfig->setObserverNodeList(*list);
                        }
                    },
                    [&ledgerConfig](std::tuple<int, std::string, protocol::BlockNumber> config) {
                        auto& [type, value, blockNumber] = config;
                        switch (type)
                        {
                        case 0:
                            ledgerConfig->setBlockTxCountLimit(
                                boost::lexical_cast<uint64_t>(value));
                            break;
                        case 1:
                            ledgerConfig->setLeaderSwitchPeriod(
                                boost::lexical_cast<uint64_t>(value));
                            break;
                        case 2:
                            ledgerConfig->setGasLimit(
                                std::make_tuple(boost::lexical_cast<uint64_t>(value), blockNumber));
                            break;
                        case 3:
                            try
                            {
                                auto version = bcos::tool::toVersionNumber(value);
                                ledgerConfig->setCompatibilityVersion(version);
                            }
                            catch (std::exception const& e)
                            {
                                SCHEDULER_LOG(WARNING) << LOG_DESC("invalidVersionNumber") << value;
                            }
                            break;
                        default:
                            BOOST_THROW_EXCEPTION(BCOS_ERROR(SchedulerError::UnknownError,
                                "Unknown type: " + boost::lexical_cast<std::string>(type)));
                        }
                    },
                    [&ledgerConfig](bcos::protocol::BlockNumber number) {
                        ledgerConfig->setBlockNumber(number);
                    },
                    [&ledgerConfig](bcos::crypto::HashType hash) { ledgerConfig->setHash(hash); }},
                result);

            ++success;
        }

        // Collect done
        if (success + failed == total)
        {
            if (failed > 0)
            {
                SCHEDULER_LOG(ERROR) << "Get ledger config with error: " << failed;
                (*callback)(
                    BCOS_ERROR_PTR(SchedulerError::UnknownError, "Get ledger config with error"),
                    nullptr);

                return;
            }

            (*callback)(nullptr, std::move(ledgerConfig));
        }
    };

    m_ledger->asyncGetNodeListByType(ledger::CONSENSUS_SEALER,
        [collector](Error::Ptr error, consensus::ConsensusNodeListPtr list) mutable {
            collector(std::move(error), std::tuple{true, std::move(list)});
        });
    m_ledger->asyncGetNodeListByType(ledger::CONSENSUS_OBSERVER,
        [collector](Error::Ptr error, consensus::ConsensusNodeListPtr list) mutable {
            collector(std::move(error), std::tuple{false, std::move(list)});
        });
    m_ledger->asyncGetSystemConfigByKey(ledger::SYSTEM_KEY_TX_COUNT_LIMIT,
        [collector](Error::Ptr error, std::string config, protocol::BlockNumber _number) mutable {
            collector(std::move(error), std::tuple{0, std::move(config), _number});
        });
    m_ledger->asyncGetSystemConfigByKey(ledger::SYSTEM_KEY_CONSENSUS_LEADER_PERIOD,
        [collector](Error::Ptr error, std::string config, protocol::BlockNumber _number) mutable {
            collector(std::move(error), std::tuple{1, std::move(config), _number});
        });
    m_ledger->asyncGetSystemConfigByKey(ledger::SYSTEM_KEY_TX_GAS_LIMIT,
        [collector](Error::Ptr error, std::string config, protocol::BlockNumber _number) mutable {
            collector(std::move(error), std::tuple{2, std::move(config), _number});
        });
    m_ledger->asyncGetBlockNumber(
        [collector, ledger = m_ledger](Error::Ptr error, protocol::BlockNumber number) mutable {
            ledger->asyncGetBlockHashByNumber(
                number, [collector](Error::Ptr error, const crypto::HashType& hash) mutable {
                    collector(std::move(error), std::move(hash));
                });
            collector(std::move(error), std::move(number));
        });

    // Note: The consensus module ensures serial execution and submit of system txs
    m_ledger->asyncGetSystemConfigByKey(ledger::SYSTEM_KEY_COMPATIBILITY_VERSION,
        [collector](Error::Ptr error, std::string config, protocol::BlockNumber _number) mutable {
            collector(std::move(error), std::tuple{3, std::move(config), _number});
        });
}

bcos::protocol::BlockNumber SchedulerImpl::getBlockNumberFromStorage()
{
    std::promise<protocol::BlockNumber> blockNumberFuture;
    m_ledger->asyncGetBlockNumber(
        [&blockNumberFuture](Error::Ptr error, protocol::BlockNumber number) {
            if (error)
            {
                SCHEDULER_LOG(ERROR) << "Get blockNumber from storage failed";
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
    if (m_blocks->empty())
    {
        return getBlockNumberFromStorage();
    }
    else
    {
        return m_blocks->front()->number() - 1;
    }
}
