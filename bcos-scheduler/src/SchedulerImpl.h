#pragma once

#include "BlockExecutive.h"
#include "BlockExecutiveFactory.h"
#include "ExecutorManager.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/executor/ParallelTransactionExecutorInterface.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-utilities/ThreadPool.h>
#include <tbb/concurrent_hash_map.h>
#include <list>

namespace bcos::scheduler
{

enum class NodeListType
{
    ConsensusNodeList,
    ObserverNodeList,
    CandidateSealerNodeList,
};

enum class ConfigType
{
    BlockTxCountLimit,
    LeaderSwitchPeriod,
    GasLimit,
    VersionNumber,
    ConsensusType,
    EpochSealerNum,
    EpochBlockNum,
    NotifyRotateFlag
};

class SchedulerImpl : public SchedulerInterface, public std::enable_shared_from_this<SchedulerImpl>
{
public:
    friend class BlockExecutive;
    using Ptr = std::shared_ptr<SchedulerImpl>;
    SchedulerImpl(ExecutorManager::Ptr executorManager, bcos::ledger::LedgerInterface::Ptr ledger,
        bcos::storage::TransactionalStorageInterface::Ptr storage,
        bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::protocol::BlockFactory::Ptr blockFactory, bcos::txpool::TxPoolInterface::Ptr txPool,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isAuthCheck, bool isWasm, int64_t schedulerTermId,
        size_t keyPageSize);

    SchedulerImpl(ExecutorManager::Ptr executorManager, bcos::ledger::LedgerInterface::Ptr ledger,
        bcos::storage::TransactionalStorageInterface::Ptr storage,
        bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::protocol::BlockFactory::Ptr blockFactory, bcos::txpool::TxPoolInterface::Ptr txPool,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isAuthCheck, bool isWasm, bool isSerialExecute,
        int64_t schedulerTermId, size_t keyPageSize);

    SchedulerImpl(const SchedulerImpl&) = delete;
    SchedulerImpl(SchedulerImpl&&) = delete;
    SchedulerImpl& operator=(const SchedulerImpl&) = delete;
    SchedulerImpl& operator=(SchedulerImpl&&) = delete;

    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool _sysBlock)>
            callback) override;

    void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> callback)
        override;

    void status(
        std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)> callback) override;

    void call(protocol::Transaction::Ptr tx,
        std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)>) override;

    void reset(std::function<void(Error::Ptr&&)> callback) override;
    // register a block number receiver
    virtual void registerBlockNumberReceiver(
        std::function<void(protocol::BlockNumber blockNumber)> callback);

    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override;

    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override;

    void registerTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
            bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)>
            txNotifier);

    void preExecuteBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(Error::Ptr&&)> callback) override;

    ExecutorManager::Ptr executorManager() { return m_executorManager; }

    void fetchConfig(protocol::BlockNumber _number = -1);

    int64_t getSchedulerTermId() { return m_schedulerTermId; }

    void start()
    {
        m_isRunning = true;
        for (auto& blockExecutive : *m_blocks)
        {
            blockExecutive->start();
        }

        SCHEDULER_LOG(DEBUG) << LOG_BADGE("Switch")
                             << "Start with termId: " << getSchedulerTermId();
    }
    void stop() override
    {
        SCHEDULER_LOG(INFO) << "Try to stop SchedulerImpl";
        m_isRunning = false;
        std::unique_lock<std::mutex> blocksLock(m_blocksMutex);
        for (auto& blockExecutive : *m_blocks)
        {
            blockExecutive->stop();
        }
    }

    void setBlockExecutiveFactory(bcos::scheduler::BlockExecutiveFactory::Ptr blockExecutiveFactory)
    {
        m_blockExecutiveFactory = blockExecutiveFactory;
    }

    void setOnNeedSwitchEventHandler(std::function<void(int64_t)> onNeedSwitchEvent)
    {
        f_onNeedSwitchEvent = std::move(onNeedSwitchEvent);
    }

    void triggerSwitch()
    {
        if (f_onNeedSwitchEvent)
        {
            f_onNeedSwitchEvent(m_schedulerTermId);
        }
    }

    bcos::crypto::Hash::Ptr getHashImpl() { return m_hashImpl; }
    const ledger::LedgerConfig& ledgerConfig() const { return *m_ledgerConfig; }

private:
    void handleBlockQueue(bcos::protocol::BlockNumber requestBlockNumber,
        std::function<void(bcos::protocol::BlockNumber)> whenOlder,  // whenOlder(frontNumber)
        std::function<void(BlockExecutive::Ptr)> whenQueueFront, std::function<void()> afterFront,
        std::function<void(BlockExecutive::Ptr)> whenNotFrontInQueue,
        std::function<void()> beforeBack, std::function<void()> whenQueueBack,
        std::function<void(bcos::protocol::BlockNumber)> whenNewer,  // whenNewer(backNumber)
        std::function<void(std::exception const&)> whenException);

    void executeBlockInternal(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool _sysBlock)>
            callback);

    bcos::protocol::BlockNumber getCurrentBlockNumber();

    BlockExecutive::Ptr getLatestPreparedBlock(bcos::protocol::BlockNumber blockNumber);
    void tryExecuteBlock(bcos::protocol::BlockNumber number, bcos::crypto::HashType parentHash);

    BlockExecutive::Ptr getPreparedBlock(
        bcos::protocol::BlockNumber blockNumber, int64_t timestamp);

    void setPreparedBlock(bcos::protocol::BlockNumber blockNumber, int64_t timestamp,
        BlockExecutive::Ptr blockExecutive);

    // remove prepared all block <= oldBlockNumber
    void removeAllOldPreparedBlock(bcos::protocol::BlockNumber oldBlockNumber);
    void removeAllPreparedBlock();

    bcos::protocol::BlockNumber getBlockNumberFromStorage();

    std::string getGasPrice()
    {
        bcos::ReadGuard lock(x_gasPrice);
        return m_gasPrice;
    }

    void setGasPrice(std::string const& _gasPrice)
    {
        bcos::WriteGuard lock(x_gasPrice);
        m_gasPrice = _gasPrice;
    }

    std::shared_ptr<std::list<BlockExecutive::Ptr>> m_blocks =
        std::make_shared<std::list<BlockExecutive::Ptr>>();

    std::shared_ptr<std::list<BlockExecutive>> m_stoppedBlockExecutives;

    std::map<bcos::protocol::BlockNumber, std::map<int64_t, BlockExecutive::Ptr>>
        m_preparedBlocks;  // blockNumber -> <timestamp -> BlockExecutive>
    mutable SharedMutex x_preparedBlockMutex;

    std::mutex m_blocksMutex;

    std::mutex m_executeMutex;
    std::timed_mutex m_commitMutex;

    std::atomic_int64_t m_calledContextID = 1;

    std::string m_gasPrice = std::string("0x0");
    mutable bcos::SharedMutex x_gasPrice;

    uint64_t m_gasLimit = 0;
    uint32_t m_blockVersion = 0;

    ExecutorManager::Ptr m_executorManager;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    // BlockExecutive will use the storage of scheduler
    bcos::storage::TransactionalStorageInterface::Ptr m_storage;
    bcos::protocol::ExecutionMessageFactory::Ptr m_executionMessageFactory;
    bcos::scheduler::BlockExecutiveFactory::Ptr m_blockExecutiveFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::txpool::TxPoolInterface::Ptr m_txPool;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_transactionSubmitResultFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    bool m_isAuthCheck = false;
    bool m_isWasm = false;
    bool m_isSerialExecute = false;

    std::function<void(protocol::BlockNumber blockNumber)> m_blockNumberReceiver;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(Error::Ptr)>)>
        m_txNotifier;
    uint64_t m_lastExecuteFinishTime = 0;

    int64_t m_schedulerTermId;

    bool m_isRunning = false;

    std::function<void(int64_t)> f_onNeedSwitchEvent;

    bcos::ThreadPool m_preExeWorker;
    bcos::ThreadPool m_exeWorker;
    ledger::LedgerConfig::Ptr m_ledgerConfig;
};
}  // namespace bcos::scheduler
