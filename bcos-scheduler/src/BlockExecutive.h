#pragma once

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

class BlockExecutive : public std::enable_shared_from_this<BlockExecutive>
{
public:
    using UniquePtr = std::unique_ptr<BlockExecutive>;

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

private:
    void DAGExecute(std::function<void(Error::UniquePtr)> error);
    void DMTExecute(std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr)> callback);

    enum TraverseHint : int8_t
    {
        PASS = 0,
        DELETE,
        SKIP,
        UPDATE,
        END,
    };

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
        std::atomic_size_t total = 0;
        std::atomic_size_t received = 0;
        std::atomic_size_t error = 0;

        std::function<void(Error::UniquePtr)> callback;
        std::atomic_bool callbackExecuted = false;
        std::atomic_bool allSended = false;
    };
    void startBatch(std::function<void(Error::UniquePtr)> callback);
    void checkBatch(BatchStatus& status);

    std::string newEVMAddress(int64_t blockNumber, ContextID contextID, Seq seq);
    std::string newEVMAddress(
        const std::string_view& _sender, bytesConstRef _init, u256 const& _salt);

    std::string preprocessAddress(const std::string_view& address);

    struct ExecutiveState  // Executive state per tx
    {
        ExecutiveState(int64_t _contextID, bcos::protocol::ExecutionMessage::UniquePtr _message,
            bool _enableDAG)
          : contextID(_contextID), message(std::move(_message)), enableDAG(_enableDAG)
        {}

        int64_t contextID;
        std::stack<int64_t, std::list<int64_t>> callStack;
        bcos::protocol::ExecutionMessage::UniquePtr message;
        bcos::Error::UniquePtr error;
        int64_t currentSeq = 0;
        bool enableDAG;
        bool skip = false;
    };

    std::map<std::tuple<std::string, ContextID>, ExecutiveState, std::less<>> m_executiveStates;
    void traverseExecutive(std::function<TraverseHint(ExecutiveState&)> callback);

    struct ExecutiveResult
    {
        bcos::protocol::TransactionReceipt::Ptr receipt;
        bcos::crypto::HashType transactionHash;
        std::string source;
    };
    std::vector<ExecutiveResult> m_executiveResults;

    size_t m_gasUsed = 0;

    GraphKeyLocks m_keyLocks;

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
};

}  // namespace bcos::scheduler