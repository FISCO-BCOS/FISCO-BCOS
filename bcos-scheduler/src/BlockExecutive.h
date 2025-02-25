#pragma once

#include "Executive.h"
#include "GraphKeyLocks.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/TransactionSubmitResultFactory.h"
#include "bcos-framework/storage/StorageInterface.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-utilities/Error.h>
#include <tbb/concurrent_unordered_map.h>
#include <chrono>
#include <utility>

namespace bcos::scheduler
{
class SchedulerImpl;
class DmcExecutor;
class DmcStepRecorder;

class BlockExecutive : public std::enable_shared_from_this<BlockExecutive>
{
public:
    using UniquePtr = std::unique_ptr<BlockExecutive>;
    using Ptr = std::shared_ptr<BlockExecutive>;

    BlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
        size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::txpool::TxPoolInterface::Ptr _txPool);

    BlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
        size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::txpool::TxPoolInterface::Ptr _txPool, uint64_t _gasLimit, std::string& _gasPrice,
        bool _syncBlock)
      : BlockExecutive(std::move(block), scheduler, startContextID,
            std::move(transactionSubmitResultFactory), staticCall, std::move(_blockFactory),
            std::move(_txPool))
    {
        m_syncBlock = _syncBlock;
        m_gasLimit = _gasLimit;
        m_gasPrice = _gasPrice;
    }

    BlockExecutive(const BlockExecutive&) = delete;
    BlockExecutive(BlockExecutive&&) = delete;
    BlockExecutive& operator=(const BlockExecutive&) = delete;
    BlockExecutive& operator=(BlockExecutive&&) = delete;

    virtual ~BlockExecutive() { stop(); };

    virtual void prepare();
    virtual void asyncExecute(
        std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback);
    virtual void asyncCall(
        std::function<void(Error::UniquePtr&&, protocol::TransactionReceipt::Ptr&&)> callback);
    virtual void asyncCommit(std::function<void(Error::UniquePtr)> callback);

    virtual void asyncNotify(
        std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
            std::function<void(Error::Ptr)>)>& notifier,
        std::function<void(Error::Ptr)> _callback);

    virtual void saveMessage(
        std::string address, protocol::ExecutionMessage::UniquePtr message, bool withDAG);

    inline bcos::protocol::BlockNumber number() { return m_blockHeader->number(); }

    inline bcos::protocol::Block::Ptr block() { return m_block; }
    inline auto blockHeader() const noexcept { return m_blockHeader; }
    inline bcos::protocol::BlockHeader::Ptr result() { return m_result; }

    bool isCall() { return m_staticCall; }
    bool sysBlock() const { return m_isSysBlock; }

    void start() { m_isRunning = true; }
    void stop() { m_isRunning = false; }

    void setOnNeedSwitchEventHandler(std::function<void()> onNeedSwitchEvent)
    {
        f_onNeedSwitchEvent = std::move(onNeedSwitchEvent);
    }

    void triggerSwitch()
    {
        if (f_onNeedSwitchEvent)
        {
            f_onNeedSwitchEvent();
        }
    }

    bool isSysBlock() { return m_isSysBlock; }

    virtual size_t getExecutorSize();

    virtual void forEachExecutor(
        std::function<void(std::string, bcos::executor::ParallelTransactionExecutorInterface::Ptr)>
            handleExecutor);

protected:
    struct CommitStatus
    {
        std::atomic_size_t total;
        std::atomic_size_t success = 0;
        std::atomic_size_t failed = 0;
        uint64_t startTS = 0;
        std::function<void(const CommitStatus&)> checkAndCommit;
        mutable SharedMutex x_lock;
    };
    void batchNextBlock(std::function<void(Error::UniquePtr)> callback);
    void batchGetHashes(std::function<void(Error::UniquePtr, crypto::HashType)> callback);
    void batchBlockCommit(uint64_t rollbackVersion, std::function<void(Error::UniquePtr)> callback);
    void batchBlockRollback(uint64_t version, std::function<void(Error::UniquePtr)> callback);

    virtual bool needPrepareExecutor() { return !m_hasDAG; }

    struct BatchStatus  // Batch state per batch
    {
        using Ptr = std::shared_ptr<BatchStatus>;
        std::atomic_size_t total = 0;
        std::atomic_size_t paused = 0;
        std::atomic_size_t finished = 0;
        std::atomic_size_t error = 0;

        std::atomic_bool callbackExecuted = false;
        mutable SharedMutex x_lock;
        std::string errorMessage;
    };

    void DAGExecute(std::function<void(Error::UniquePtr)> callback);  // only used for DAG
    std::map<std::tuple<std::string, ContextID>, ExecutiveState::Ptr, std::less<>>
        m_executiveStates;  // only used for DAG

    void DMCExecute(
        std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback);
    virtual std::shared_ptr<DmcExecutor> registerAndGetDmcExecutor(std::string contractAddress);
    virtual std::shared_ptr<DmcExecutor> buildDmcExecutor(const std::string& name,
        const std::string& contractAddress,
        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor);
    void scheduleExecutive(ExecutiveState::Ptr executiveState);
    void onTxFinish(bcos::protocol::ExecutionMessage::UniquePtr output);
    void onDmcExecuteFinish(
        std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback);
    virtual void onExecuteFinish(
        std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback);

    bcos::protocol::ExecutionMessage::UniquePtr buildMessage(
        ContextID contextID, bcos::protocol::Transaction::ConstPtr tx);
    void buildExecutivesFromMetaData();
    void buildExecutivesFromNormalTransaction();

    bcos::storage::TransactionalStorageInterface::Ptr getStorage();

    virtual void serialPrepareExecutor();

    bcos::protocol::ConstTransactionsPtr fetchBlockTxsFromTxPool(
        bcos::protocol::Block::Ptr block, bcos::txpool::TxPoolInterface::Ptr txPool);
    std::string preprocessAddress(const std::string_view& address);
    void updateMultiExecutorsNonce();
    void updateWeb3NonceMap(protocol::ExecutionMessage::UniquePtr const& msg);

    std::map<std::string, std::shared_ptr<DmcExecutor>, std::less<>> m_dmcExecutors;
    std::shared_ptr<DmcStepRecorder> m_dmcRecorder;
    std::unordered_map<std::string, u256> m_web3NonceMap;

    std::vector<ExecutiveResult> m_executiveResults;

    std::atomic<size_t> m_gasUsed = 0;

    GraphKeyLocks::Ptr m_keyLocks = std::make_shared<GraphKeyLocks>();

    std::chrono::system_clock::time_point m_currentTimePoint;

    std::chrono::milliseconds m_executeElapsed;
    std::chrono::milliseconds m_hashElapsed;
    std::chrono::milliseconds m_commitElapsed;

    bcos::protocol::Block::Ptr m_block;
    bcos::protocol::BlockHeader::ConstPtr m_blockHeader;
    bcos::protocol::ConstTransactionsPtr m_blockTxs;

    bcos::protocol::BlockHeader::Ptr m_result;
    SchedulerImpl* m_scheduler;
    int64_t m_schedulerTermId;
    size_t m_startContextID;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_transactionSubmitResultFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::txpool::TxPoolInterface::Ptr m_txPool;

    size_t m_gasLimit = TRANSACTION_GAS;
    std::string m_gasPrice = std::string("0x0");
    std::atomic_bool m_isSysBlock = false;

    bool m_staticCall = false;
    bool m_syncBlock = false;
    bool m_hasPrepared = false;
    bool m_hasDAG = false;
    mutable SharedMutex x_prepareLock;
    mutable SharedMutex x_dmcExecutorLock;

    bool m_isRunning = false;

    std::function<void()> f_onNeedSwitchEvent;
    crypto::Hash::Ptr m_hashImpl;
};

}  // namespace bcos::scheduler
