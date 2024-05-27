#pragma once
#include "bcos-scheduler/src/SchedulerFactory.h"
#include "bcos-scheduler/src/SchedulerImpl.h"
#include <bcos-utilities/ThreadPool.h>

namespace bcos::scheduler
{
class SchedulerManager : public SchedulerInterface
{
public:
    SchedulerManager(
        int64_t schedulerSeq, SchedulerFactory::Ptr factory, ExecutorManager::Ptr executorManager)
      : m_factory(factory),
        m_schedulerTerm(schedulerSeq),
        m_executorManager(executorManager),
        m_pool("SchedulerManager", 1),  // Must set to 1 for serial execution
        m_status(INITIALING)
    {
        executorManager->setExecutorChangeHandler([this]() {
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

    void handleNeedSwitchEvent(int64_t oldSchedulerTermID);

    void testTriggerSwitch();

    SchedulerFactory::Ptr getFactory() { return m_factory; }

    class SchedulerTerm
    {
    public:
        SchedulerTerm(int64_t schedulerSeq)
          : m_schedulerSeq(schedulerSeq), m_executorSeq(utcTime() / 1000)
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
            BCOS_LOG(DEBUG) << "Build SchedulerTermID" << LOG_KV("m_schedulerSeq", m_schedulerSeq)
                            << LOG_KV("m_executorSeq", m_executorSeq)
                            << LOG_KV("SchedulerTermID", id);

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
        STOPPED = 4,
    };

    std::pair<bool, std::string> checkAndInit();
    void stop() override
    {
        if (m_status == STOPPED)
        {
            SCHEDULER_LOG(INFO) << "scheduler has just stopped." << std::endl;
            return;
        }

        SCHEDULER_LOG(INFO) << "Try to stop SchedulerManager";

        m_status.store(STOPPED);

        if (m_scheduler)
        {
            m_scheduler->stop();
        }

        if (m_executorManager)
        {
            m_executorManager->stop();
        }

        // waiting for stopped
        int32_t waitCount = 20;
        while (m_scheduler.use_count() > 1 && waitCount-- > 0)
        {
            SCHEDULER_LOG(DEBUG) << "Scheduler is stopping.. "
                                 << LOG_KV("unfinishedTaskNum", m_scheduler.use_count() - 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        m_factory->stop();
        SCHEDULER_LOG(INFO) << "scheduler has stopped.";
        m_scheduler = nullptr;
    }

    void triggerSwitch() { asyncSelfSwitchTerm(); };

private:
    void updateScheduler(int64_t schedulerTermId);
    void switchTerm(int64_t schedulerSeq);
    void selfSwitchTerm(bool needCheckSwitching = true);
    void asyncSelfSwitchTerm();
    void onSwitchTermNotify();

private:
    SchedulerImpl::Ptr m_scheduler;
    SchedulerFactory::Ptr m_factory;
    SchedulerTerm m_schedulerTerm;
    ExecutorManager::Ptr m_executorManager;
    std::vector<std::function<void(bcos::protocol::BlockNumber)>> m_onSwitchTermHandlers;

    bcos::ThreadPool m_pool;

    std::atomic<Status> m_status;
};

}  // namespace bcos::scheduler
