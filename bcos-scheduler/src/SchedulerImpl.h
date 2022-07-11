#pragma once

#include "BlockExecutive.h"
#include "ExecutorManager.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/executor/ParallelTransactionExecutorInterface.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <tbb/concurrent_hash_map.h>
#include <future>
#include <list>

namespace bcos::scheduler
{
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
        bcos::crypto::Hash::Ptr hashImpl, bool isAuthCheck, bool isWasm, int64_t schedulerTermId)
      : m_executorManager(std::move(executorManager)),
        m_ledger(std::move(ledger)),
        m_storage(std::move(storage)),
        m_executionMessageFactory(std::move(executionMessageFactory)),
        m_blockFactory(std::move(blockFactory)),
        m_txPool(txPool),
        m_transactionSubmitResultFactory(std::move(transactionSubmitResultFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_isAuthCheck(isAuthCheck),
        m_isWasm(isWasm),
        m_schedulerTermId(schedulerTermId)
    {
        start();
    }

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

    void registerExecutor(std::string name,
        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor,
        std::function<void(Error::Ptr&&)> callback) override;

    void unregisterExecutor(
        const std::string& name, std::function<void(Error::Ptr&&)> callback) override;

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

    inline void fetchGasLimit(protocol::BlockNumber _number = -1)
    {
        if (_number == -1)
        {
            std::promise<std::tuple<Error::Ptr, protocol::BlockNumber>> numberPromise;
            m_ledger->asyncGetBlockNumber(
                [&numberPromise](Error::Ptr _error, protocol::BlockNumber _number) {
                    numberPromise.set_value(std::make_tuple(std::move(_error), _number));
                });
            Error::Ptr error;
            std::tie(error, _number) = numberPromise.get_future().get();
            if (error)
            {
                return;
            }
        }

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
                << LOG_DESC("fetchGasLimit failed") << LOG_KV("code", e->errorCode())
                << LOG_KV("message", e->errorMessage());
            BOOST_THROW_EXCEPTION(
                BCOS_ERROR(SchedulerError::fetchGasLimitError, e->errorMessage()));
        }

        m_gasLimit = boost::lexical_cast<uint64_t>(value);
    }

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
    void stop()
    {
        m_isRunning = false;
        for (auto& blockExecutive : *m_blocks)
        {
            blockExecutive->stop();
        }
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

private:
    void handleBlockQueue(bcos::protocol::BlockNumber requestBlockNumber,
        std::function<void(bcos::protocol::BlockNumber)> whenOlder,  // whenOlder(frontNumber)
        std::function<void(BlockExecutive::Ptr)> whenQueueFront, std::function<void()> afterFront,
        std::function<void(BlockExecutive::Ptr)> whenNotFrontInQueue,
        std::function<void()> beforeBack, std::function<void()> whenQueueBack,
        std::function<void(bcos::protocol::BlockNumber)> whenNewer,  // whenNewer(backNumber)
        std::function<void(std::exception const&)> whenException);

    bcos::protocol::BlockNumber getCurrentBlockNumber();

    void asyncGetLedgerConfig(
        std::function<void(Error::Ptr, ledger::LedgerConfig::Ptr ledgerConfig)> callback);

    BlockExecutive::Ptr getPreparedBlock(
        bcos::protocol::BlockNumber blockNumber, int64_t timestamp);

    void setPreparedBlock(bcos::protocol::BlockNumber blockNumber, int64_t timestamp,
        BlockExecutive::Ptr blockExecutive);

    // remove prepared all block <= oldBlockNumber
    void removeAllOldPreparedBlock(bcos::protocol::BlockNumber oldBlockNumber);

    bcos::protocol::BlockNumber getBlockNumberFromStorage();

    std::shared_ptr<std::list<BlockExecutive::Ptr>> m_blocks =
        std::make_shared<std::list<BlockExecutive::Ptr>>();

    std::shared_ptr<std::list<BlockExecutive>> m_stoppedBlockExecutives;

    std::map<bcos::protocol::BlockNumber, std::map<int64_t, BlockExecutive::Ptr>>
        m_preparedBlocks;  // blockNumber -> <timestamp -> BlockExecutive>
    mutable SharedMutex x_preparedBlockMutex;

    std::mutex m_blocksMutex;

    std::mutex m_executeMutex;
    std::mutex m_commitMutex;

    std::atomic_int64_t m_calledContextID = 1;

    uint64_t m_gasLimit = TRANSACTION_GAS;

    ExecutorManager::Ptr m_executorManager;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::storage::TransactionalStorageInterface::Ptr m_storage;
    bcos::protocol::ExecutionMessageFactory::Ptr m_executionMessageFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::txpool::TxPoolInterface::Ptr m_txPool;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_transactionSubmitResultFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    bool m_isAuthCheck = false;
    bool m_isWasm = false;

    std::function<void(protocol::BlockNumber blockNumber)> m_blockNumberReceiver;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(Error::Ptr)>)>
        m_txNotifier;
    uint64_t m_lastExecuteFinishTime = 0;

    int64_t m_schedulerTermId;

    bool m_isRunning = false;

    std::function<void(int64_t)> f_onNeedSwitchEvent;
};
}  // namespace bcos::scheduler