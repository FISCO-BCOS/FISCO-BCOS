#include "SchedulerImpl.h"
#include "Common.h"
#include "bcos-framework/interfaces/ledger/LedgerConfig.h"
#include "bcos-framework/interfaces/protocol/ProtocolTypeDef.h"
#include "bcos-utilities/Error.h"
#include <boost/exception/diagnostic_information.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <memory>
#include <mutex>
#include <variant>

using namespace bcos::scheduler;

void SchedulerImpl::executeBlock(bcos::protocol::Block::Ptr block, bool verify,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&)> callback)
{
    auto signature = block->blockHeaderConst()->signatureList();
    SCHEDULER_LOG(INFO) << "ExecuteBlock request"
                        << LOG_KV("block number", block->blockHeaderConst()->number())
                        << LOG_KV("verify", verify) << LOG_KV("signatureSize", signature.size())
                        << LOG_KV("tx count", block->transactionsSize())
                        << LOG_KV("meta tx count", block->transactionsMetaDataSize());
    auto executeLock =
        std::make_shared<std::unique_lock<std::mutex>>(m_executeMutex, std::try_to_lock);
    if (!executeLock->owns_lock())
    {
        auto message = "Another block is executing!";
        SCHEDULER_LOG(ERROR) << "ExecuteBlock error, " << message;
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, message), nullptr);
        return;
    }

    std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
    // Note: if hit the cache, may return synced blockHeader with signatureList in some cases
    if (!m_blocks.empty())
    {
        auto requestNumber = block->blockHeaderConst()->number();
        auto& frontBlock = m_blocks.front();
        auto& backBlock = m_blocks.back();
        auto signature = block->blockHeaderConst()->signatureList();
        // Block already executed
        if (requestNumber >= frontBlock.number() && requestNumber <= backBlock.number())
        {
            SCHEDULER_LOG(INFO) << "ExecuteBlock success, return executed block"
                                << LOG_KV("block number", block->blockHeaderConst()->number())
                                << LOG_KV("signatureSize", signature.size())
                                << LOG_KV("verify", verify);

            auto it = m_blocks.begin();
            while (it->number() != requestNumber)
            {
                ++it;
            }

            SCHEDULER_LOG(TRACE) << "BlockHeader stateRoot: " << std::hex
                                 << it->result()->stateRoot();

            auto blockHeader = it->result();

            blocksLock.unlock();
            executeLock->unlock();
            callback(nullptr, std::move(blockHeader));
            return;
        }

        if (requestNumber - backBlock.number() != 1)
        {
            auto message =
                "Invalid block number: " +
                boost::lexical_cast<std::string>(block->blockHeaderConst()->number()) +
                " current last number: " + boost::lexical_cast<std::string>(backBlock.number());
            SCHEDULER_LOG(ERROR) << "ExecuteBlock error, " << message;

            blocksLock.unlock();
            executeLock->unlock();
            callback(
                BCOS_ERROR_PTR(SchedulerError::InvalidBlockNumber, std::move(message)), nullptr);

            return;
        }
    }
    else
    {
        auto lastExecutedNumber = m_lastExecutedBlockNumber.load();
        if (lastExecutedNumber != 0 && lastExecutedNumber >= block->blockHeaderConst()->number())
        {
            auto message =
                (boost::format("Try to execute an executed block: %ld, last executed number: %ld") %
                    block->blockHeaderConst()->number() % lastExecutedNumber)
                    .str();
            SCHEDULER_LOG(ERROR) << "ExecuteBlock error, " << message;
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlockNumber, message), nullptr);
            return;
        }
    }
    m_blocks.emplace_back(
        std::move(block), this, 0, m_transactionSubmitResultFactory, false, m_blockFactory, verify);
    auto& blockExecutive = m_blocks.back();

    blocksLock.unlock();
    blockExecutive.asyncExecute([this, callback = std::move(callback), executeLock](
                                    Error::UniquePtr error, protocol::BlockHeader::Ptr header) {
        if (error)
        {
            SCHEDULER_LOG(ERROR) << "Unknown error, " << boost::diagnostic_information(*error);
            {
                std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
                m_blocks.pop_front();
            }
            executeLock->unlock();
            callback(
                BCOS_ERROR_WITH_PREV_PTR(SchedulerError::UnknownError, "Unknown error", *error),
                nullptr);
            return;
        }
        auto signature = header->signatureList();
        SCHEDULER_LOG(INFO) << "ExecuteBlock success" << LOG_KV("block number", header->number())
                            << LOG_KV("hash", header->hash().abridged())
                            << LOG_KV("state root", header->stateRoot().hex())
                            << LOG_KV("receiptRoot", header->receiptsRoot().hex())
                            << LOG_KV("txsRoot", header->txsRoot().abridged())
                            << LOG_KV("gasUsed", header->gasUsed())
                            << LOG_KV("signatureSize", signature.size());

        m_lastExecutedBlockNumber.store(header->number());

        executeLock->unlock();
        callback(std::move(error), std::move(header));
    });
}

void SchedulerImpl::commitBlock(bcos::protocol::BlockHeader::Ptr header,
    std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> callback)
{
    SCHEDULER_LOG(INFO) << "CommitBlock request" << LOG_KV("block number", header->number());

    auto commitLock =
        std::make_shared<std::unique_lock<std::mutex>>(m_commitMutex, std::try_to_lock);
    if (!commitLock->owns_lock())
    {
        std::string message;
        {
            std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
            if (m_blocks.empty())
            {
                message = (boost::format("commitBlock: empty block queue, maybe the block has been "
                                         "committed! Block number: %ld, hash: %s") %
                           header->number() % header->hash().abridged())
                              .str();
            }
            else
            {
                auto& frontBlock = m_blocks.front();
                message =
                    (boost::format(
                         "commitBlock: Another block is committing! Block number: %ld, hash: %s") %
                        frontBlock.block()->blockHeaderConst()->number() %
                        frontBlock.block()->blockHeaderConst()->hash().abridged())
                        .str();
            }
        }
        SCHEDULER_LOG(ERROR) << "CommitBlock error, " << message;
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, message), nullptr);
        return;
    }

    if (m_blocks.empty())
    {
        auto message = "No uncommitted block";
        SCHEDULER_LOG(ERROR) << "CommitBlock error, " << message;

        commitLock->unlock();
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlocks, message), nullptr);
        return;
    }

    auto& frontBlock = m_blocks.front();
    if (!frontBlock.result())
    {
        auto message = "Block is executing";
        SCHEDULER_LOG(ERROR) << "CommitBlock error, " << message;

        commitLock->unlock();
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, message), nullptr);
        return;
    }

    if (header->number() != frontBlock.number())
    {
        auto message = "Invalid block number, available block number: " +
                       boost::lexical_cast<std::string>(frontBlock.number());
        SCHEDULER_LOG(ERROR) << "CommitBlock error, " << message;

        commitLock->unlock();
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidBlockNumber, message), nullptr);
        return;
    }
    // Note: only when the signatureList is empty need to reset the header
    // in case of the signatureList of the header is accessing by the sync module while frontBlock
    // is setting newBlockHeader, which will cause the signatureList ilegal
    auto executedHeader = frontBlock.block()->blockHeader();
    auto signature = executedHeader->signatureList();
    if (signature.size() == 0)
    {
        frontBlock.block()->setBlockHeader(std::move(header));
    }
    frontBlock.asyncCommit([this, callback = std::move(callback), block = frontBlock.block(),
                               commitLock](Error::UniquePtr&& error) {
        if (error)
        {
            SCHEDULER_LOG(ERROR) << "CommitBlock error, " << boost::diagnostic_information(*error);

            commitLock->unlock();
            callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                         SchedulerError::UnknownError, "CommitBlock error", *error),
                nullptr);
            return;
        }

        asyncGetLedgerConfig([this, commitLock = std::move(commitLock),
                                 callback = std::move(callback)](
                                 Error::Ptr error, ledger::LedgerConfig::Ptr ledgerConfig) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Get system config error, " << boost::diagnostic_information(*error);

                commitLock->unlock();
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             SchedulerError::UnknownError, "Get system config error", *error),
                    nullptr);
                return;
            }

            SCHEDULER_LOG(INFO) << "CommitBlock success"
                                << LOG_KV("block number", ledgerConfig->blockNumber());

            auto& frontBlock = m_blocks.front();
            auto blockNumber = ledgerConfig->blockNumber();

            if (m_txNotifier)
            {
                SCHEDULER_LOG(INFO) << "Start notify block result: " << blockNumber;
                frontBlock.asyncNotify(m_txNotifier,
                    [this, blockNumber, callback = std::move(callback),
                        ledgerConfig = std::move(ledgerConfig),
                        commitLock = std::move(commitLock)](Error::Ptr _error) mutable {
                        if (m_blockNumberReceiver)
                        {
                            m_blockNumberReceiver(blockNumber);
                        }

                        SCHEDULER_LOG(INFO) << "End notify block result: " << blockNumber;

                        {
                            std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
                            m_blocks.pop_front();
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
                    m_blocks.pop_front();
                    SCHEDULER_LOG(DEBUG) << "Remove committed block: " << blockNumber << " success";
                }

                commitLock->unlock();
                callback(nullptr, std::move(ledgerConfig));
            }
        });
    });
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
        callback(BCOS_ERROR_PTR(SchedulerError::UnknownError, "Call address is empty"), nullptr);
        return;
    }
    // Create temp block
    auto block = m_blockFactory->createBlock();
    block->appendTransaction(std::move(tx));

    // Create temp executive
    auto blockExecutive = std::make_shared<BlockExecutive>(std::move(block), this,
        m_calledContextID++, m_transactionSubmitResultFactory, true, m_blockFactory, false);

    blockExecutive->asyncCall([callback = std::move(callback)](Error::UniquePtr&& error,
                                  protocol::TransactionReceipt::Ptr&& receipt) {
        if (error)
        {
            SCHEDULER_LOG(ERROR) << "Unknown error, " << boost::diagnostic_information(*error);
            callback(
                BCOS_ERROR_WITH_PREV_PTR(SchedulerError::UnknownError, "Unknown error", *error),
                nullptr);
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

void SchedulerImpl::registerBlockNumberReceiver(
    std::function<void(protocol::BlockNumber blockNumber)> callback)
{
    m_blockNumberReceiver = std::move(callback);
}

void SchedulerImpl::getCode(
    std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback)
{
    auto executor = m_executorManager->dispatchExecutor(contract);
    executor->getCode(contract, std::move(callback));
}

void SchedulerImpl::registerTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
        bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)>
        txNotifier)
{
    m_txNotifier = std::move(txNotifier);
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
        std::make_shared<std::tuple<size_t, std::atomic_size_t, std::atomic_size_t>>(6, 0, 0);

    auto collecter = [summary = std::move(summary), ledgerConfig = std::move(ledgerConfig),
                         callback = std::move(callbackPtr)](Error::Ptr error,
                         std::variant<std::tuple<bool, consensus::ConsensusNodeListPtr>,
                             std::tuple<int, std::string>, bcos::protocol::BlockNumber,
                             bcos::crypto::HashType>&& result) mutable {
        auto& [total, success, failed] = *summary;

        if (error)
        {
            SCHEDULER_LOG(ERROR) << "Get ledger config with errors: "
                                 << boost::diagnostic_information(*error);
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
                    [&ledgerConfig](std::tuple<int, std::string> config) {
                        auto& [type, value] = config;
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
                        default:
                            BOOST_THROW_EXCEPTION(BCOS_ERROR(SchedulerError::UnknownError,
                                "Unknown type: " + boost::lexical_cast<std::string>(type)));
                            break;
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
        [collecter](Error::Ptr error, consensus::ConsensusNodeListPtr list) mutable {
            collecter(std::move(error), std::tuple{true, std::move(list)});
        });
    m_ledger->asyncGetNodeListByType(ledger::CONSENSUS_OBSERVER,
        [collecter](Error::Ptr error, consensus::ConsensusNodeListPtr list) mutable {
            collecter(std::move(error), std::tuple{false, std::move(list)});
        });
    m_ledger->asyncGetSystemConfigByKey(ledger::SYSTEM_KEY_TX_COUNT_LIMIT,
        [collecter](Error::Ptr error, std::string config, protocol::BlockNumber) mutable {
            collecter(std::move(error), std::tuple{0, std::move(config)});
        });
    m_ledger->asyncGetSystemConfigByKey(ledger::SYSTEM_KEY_CONSENSUS_LEADER_PERIOD,
        [collecter](Error::Ptr error, std::string config, protocol::BlockNumber) mutable {
            collecter(std::move(error), std::tuple{1, std::move(config)});
        });
    m_ledger->asyncGetBlockNumber(
        [collecter, ledger = m_ledger](Error::Ptr error, protocol::BlockNumber number) mutable {
            ledger->asyncGetBlockHashByNumber(
                number, [collecter](Error::Ptr error, const crypto::HashType& hash) mutable {
                    collecter(std::move(error), std::move(hash));
                });
            collecter(std::move(error), std::move(number));
        });
}