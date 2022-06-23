#pragma once
#include "TarsRemoteExecutorManager.h"
#include "bcos-scheduler/src/SchedulerFactory.h"
#include "bcos-scheduler/src/SchedulerImpl.h"
#include <bcos-utilities/ThreadPool.h>

namespace bcos::scheduler
{
class SchedulerManager : public SchedulerInterface
{
public:
    SchedulerManager(int64_t schedulerSeq, SchedulerFactory::Ptr factory,
        TarsRemoteExecutorManager::Ptr remoteExecutorManager)
      : m_factory(factory),
        m_schedulerTerm(schedulerSeq),
        m_remoteExecutorManager(remoteExecutorManager),
        m_pool("SchedulerManager", std::thread::hardware_concurrency()),
        m_status(INITIALING)
    {
        remoteExecutorManager->setRemoteExecutorChangeHandler([this]() {
            // trigger switch
            asyncSelfSwitchTerm();
        });

        // Notice: Not to initSchedulerIfNotExist here, because factory need to bind notifier after
        // this constructor
    }

    // by pbft & sync
    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool _sysBlock)>
            callback) override;
    // by pbft & sync
    void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> callback)
        override;
    void status(
        std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)> callback) override;
    void call(protocol::Transaction::Ptr tx,
        std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)> callback) override;
    void registerExecutor(std::string name,
        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor,
        std::function<void(Error::Ptr&&)> callback) override;
    void unregisterExecutor(
        const std::string& name, std::function<void(Error::Ptr&&)> callback) override;
    void reset(std::function<void(Error::Ptr&&)> callback) override;
    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override;
    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override;

    void preExecuteBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(Error::Ptr&&)> callback) override;

    void asyncSwitchTerm(int64_t schedulerSeq, std::function<void(Error::Ptr&&)> callback);


    void initSchedulerIfNotExist();

    void registerOnSwitchTermHandler(std::function<void(bcos::protocol::BlockNumber)> onSwitchTerm);

    void testTriggerSwitch();

    SchedulerFactory::Ptr getFactory() { return m_factory; }

    class SchedulerTerm
    {
    public:
        SchedulerTerm(int64_t schedulerSeq) : m_schedulerSeq(schedulerSeq), m_executorSeq(utcTime())
        {}

        SchedulerTerm next() { return SchedulerTerm(m_schedulerSeq); }
        int64_t getSchedulerTermID()
        {
            int64_t id = (m_schedulerSeq << 32) + m_executorSeq;
            if (id <= 0)
            {
                BCOS_LOG(FATAL) << "SchedulerTermID overflow!"
                                << LOG_KV("m_schedulerSeq", m_schedulerSeq)
                                << LOG_KV("m_executorSeq", m_executorSeq)
                                << LOG_KV("SchedulerTermID", id);
            }
            return id;
        }


    private:
        int64_t m_schedulerSeq;
        int64_t m_executorSeq;
    };

    enum Status : uint8_t
    {
        INITIALING = 1,
        RUNNING = 2,
        SWITCHING = 3,
    };

    std::pair<bool, std::string> checkAndInit();


private:
    void updateScheduler(int64_t schedulerTermId);
    void switchTerm(int64_t schedulerSeq);
    void selfSwitchTerm();
    void asyncSelfSwitchTerm();
    void onSwitchTermNotify();

private:
    SchedulerImpl::Ptr m_scheduler;
    SchedulerImpl::Ptr m_oldScheduler;  // TODO: no to use this
    SchedulerFactory::Ptr m_factory;
    SchedulerTerm m_schedulerTerm;
    TarsRemoteExecutorManager::Ptr m_remoteExecutorManager;
    std::vector<std::function<void(bcos::protocol::BlockNumber)>> m_onSwitchTermHandlers;

    bcos::ThreadPool m_pool;

    std::atomic<Status> m_status;
};

}  // namespace bcos::scheduler
