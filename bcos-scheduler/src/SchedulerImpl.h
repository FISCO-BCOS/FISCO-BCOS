#pragma once

#include "BlockExecutive.h"
#include "ExecutorManager.h"
#include "bcos-framework/interfaces/dispatcher/SchedulerInterface.h"
#include "bcos-framework/interfaces/ledger/LedgerInterface.h"
#include "bcos-framework/interfaces/protocol/ProtocolTypeDef.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h>
#include <bcos-framework/interfaces/protocol/BlockFactory.h>
#include <bcos-framework/interfaces/rpc/RPCInterface.h>
#include <tbb/concurrent_hash_map.h>
#include <list>

namespace bcos::scheduler
{
class SchedulerImpl : public SchedulerInterface, public std::enable_shared_from_this<SchedulerImpl>
{
public:
    friend class BlockExecutive;

    SchedulerImpl(ExecutorManager::Ptr executorManager, bcos::ledger::LedgerInterface::Ptr ledger,
        bcos::storage::TransactionalStorageInterface::Ptr storage,
        bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::protocol::BlockFactory::Ptr blockFactory,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isAuthCheck, bool isWasm)
      : m_executorManager(std::move(executorManager)),
        m_ledger(std::move(ledger)),
        m_storage(std::move(storage)),
        m_executionMessageFactory(std::move(executionMessageFactory)),
        m_transactionSubmitResultFactory(std::move(transactionSubmitResultFactory)),
        m_blockFactory(std::move(blockFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_isAuthCheck(isAuthCheck),
        m_isWasm(isWasm)
    {}

    SchedulerImpl(const SchedulerImpl&) = delete;
    SchedulerImpl(SchedulerImpl&&) = delete;
    SchedulerImpl& operator=(const SchedulerImpl&) = delete;
    SchedulerImpl& operator=(SchedulerImpl&&) = delete;

    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&)> callback)
        override;

    void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> callback)
        override;

    void status(
        std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)> callback) override;

    void call(protocol::Transaction::Ptr tx,
        std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)>) override;

    void registerExecutor(std::string name,
        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor,
        std::function<void(Error::Ptr&&)> callback) override;

    void unregisterExecutor(
        const std::string& name, std::function<void(Error::Ptr&&)> callback) override;

    void reset(std::function<void(Error::Ptr&&)> callback) override;

    void registerBlockNumberReceiver(
        std::function<void(protocol::BlockNumber blockNumber)> callback) override;

    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override;

    void registerTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
            bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)>
            txNotifier);

    void preExecuteBlock(bcos::protocol::Block::Ptr block, bool verify) override;

    ExecutorManager::Ptr executorManager() { return m_executorManager; }

private:
    void asyncGetLedgerConfig(
        std::function<void(Error::Ptr, ledger::LedgerConfig::Ptr ledgerConfig)> callback);

    std::list<BlockExecutive::Ptr> m_blocks;
    std::map<bcos::protocol::BlockNumber, std::map<int64_t, BlockExecutive::Ptr>>
        m_preparedBlocks;  // blockNumber -> <timestamp -> BlockExecutive>

    mutable SharedMutex x_preparedBlockMutex;
    std::mutex m_blocksMutex;
    std::mutex m_executeMutex;
    std::mutex m_commitMutex;

    std::atomic_int64_t m_calledContextID = 0;

    std::atomic<bcos::protocol::BlockNumber> m_lastExecutedBlockNumber = 0;

    ExecutorManager::Ptr m_executorManager;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::storage::TransactionalStorageInterface::Ptr m_storage;
    bcos::protocol::ExecutionMessageFactory::Ptr m_executionMessageFactory;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_transactionSubmitResultFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    bool m_isAuthCheck = false;
    bool m_isWasm = false;

    std::function<void(protocol::BlockNumber blockNumber)> m_blockNumberReceiver;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(Error::Ptr)>)>
        m_txNotifier;

    BlockExecutive::Ptr getPreparedBlock(
        bcos::protocol::BlockNumber blockNumber, int64_t timestamp);

    void setPreparedBlock(bcos::protocol::BlockNumber blockNumber, int64_t timestamp,
        BlockExecutive::Ptr blockExecutive);

    // remove prepared all block <= oldBlockNumber
    void removeAllOldPreparedBlock(bcos::protocol::BlockNumber oldBlockNumber);
};
}  // namespace bcos::scheduler