#pragma once

#include "Executive.h"
#include "ExecutorManager.h"
#include "GraphKeyLocks.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/BlockHeader.h"
#include "bcos-framework/interfaces/protocol/BlockHeaderFactory.h"
#include "bcos-framework/interfaces/protocol/ProtocolTypeDef.h"
#include "bcos-framework/interfaces/protocol/TransactionMetaData.h"
#include "bcos-framework/interfaces/protocol/TransactionReceiptFactory.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/interfaces/protocol/BlockFactory.h>
#include <bcos-utilities/Error.h>
#include <tbb/concurrent_unordered_map.h>
#include <boost/iterator/iterator_categories.hpp>
#include <boost/range/any_range.hpp>
#include <chrono>
#include <forward_list>
#include <mutex>
#include <ratio>
#include <stack>
#include <thread>

namespace bcos::scheduler
{
class SchedulerImpl;
class DmcExecutor;

class BlockExecutive : public std::enable_shared_from_this<BlockExecutive>
{
public:
    using UniquePtr = std::unique_ptr<BlockExecutive>;
    using Ptr = std::shared_ptr<BlockExecutive>;

    BlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
        size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory)
      : m_block(std::move(block)),
        m_scheduler(scheduler),
        m_startContextID(startContextID),
        m_transactionSubmitResultFactory(std::move(transactionSubmitResultFactory)),
        m_blockFactory(_blockFactory),
        m_staticCall(staticCall)
    {}

    BlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
        size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory, bool _syncBlock)
      : BlockExecutive(block, scheduler, startContextID, transactionSubmitResultFactory, staticCall,
            _blockFactory)
    {
        m_syncBlock = _syncBlock;
    }

    BlockExecutive(const BlockExecutive&) = delete;
    BlockExecutive(BlockExecutive&&) = delete;
    BlockExecutive& operator=(const BlockExecutive&) = delete;
    BlockExecutive& operator=(BlockExecutive&&) = delete;

    void prepare();
    void asyncExecute(std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr)> callback);
    void asyncCall(
        std::function<void(Error::UniquePtr&&, protocol::TransactionReceipt::Ptr&&)> callback);
    void asyncCommit(std::function<void(Error::UniquePtr)> callback);

    void asyncNotify(
        std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
            std::function<void(Error::Ptr)>)>& notifier,
        std::function<void(Error::Ptr)> _callback);

    bcos::protocol::BlockNumber number() { return m_block->blockHeaderConst()->number(); }

    bcos::protocol::Block::Ptr block() { return m_block; }
    bcos::protocol::BlockHeader::Ptr result() { return m_result; }

    bool isCall() { return m_staticCall; }

    void removeAllState();

private:
    struct CommitStatus
    {
        std::atomic_size_t total;
        std::atomic_size_t success = 0;
        std::atomic_size_t failed = 0;
        std::function<void(const CommitStatus&)> checkAndCommit;
    };
    void batchNextBlock(std::function<void(Error::UniquePtr)> callback);
    void batchGetHashes(std::function<void(Error::UniquePtr, crypto::HashType)> callback);
    void batchBlockCommit(std::function<void(Error::UniquePtr)> callback);
    void batchBlockRollback(std::function<void(Error::UniquePtr)> callback);

    struct BatchStatus  // Batch state per batch
    {
        using Ptr = std::shared_ptr<BatchStatus>;
        std::atomic_size_t total = 0;
        std::atomic_size_t paused = 0;
        std::atomic_size_t finished = 0;
        std::atomic_size_t error = 0;

        std::atomic_bool callbackExecuted = false;
        mutable SharedMutex x_lock;
    };

    void DAGExecute(std::function<void(Error::UniquePtr)> callback);  // only used for DAG
    std::map<std::tuple<std::string, ContextID>, ExecutiveState::Ptr, std::less<>>
        m_executiveStates;  // only used for DAG

    void DMCExecute(std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr)> callback);
    std::shared_ptr<DmcExecutor> registerAndGetDmcExecutor(std::string contractAddress);
    void scheduleExecutive(ExecutiveState::Ptr executiveState);
    void onTxFinish(bcos::protocol::ExecutionMessage::UniquePtr output);
    void onDmcExecuteFinish(
        std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr)> callback);
    void serialPrepareExecutor();
    std::string preprocessAddress(const std::string_view& address);

    std::map<std::string, std::shared_ptr<DmcExecutor>, std::less<>> m_dmcExecutors;
    std::vector<ExecutiveResult> m_executiveResults;

    size_t m_gasUsed = 0;

    GraphKeyLocks::Ptr m_keyLocks = std::make_shared<GraphKeyLocks>();

    std::chrono::system_clock::time_point m_currentTimePoint;

    std::chrono::milliseconds m_executeElapsed;
    std::chrono::milliseconds m_hashElapsed;
    std::chrono::milliseconds m_commitElapsed;

    bcos::protocol::Block::Ptr m_block;
    bcos::protocol::BlockHeader::Ptr m_result;
    SchedulerImpl* m_scheduler;
    size_t m_startContextID;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_transactionSubmitResultFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bool m_staticCall = false;
    bool m_syncBlock = false;
    bool m_hasPrepared = false;
    bool m_withDAG = false;
    mutable SharedMutex x_dmcExecutorLock;
};

}  // namespace bcos::scheduler