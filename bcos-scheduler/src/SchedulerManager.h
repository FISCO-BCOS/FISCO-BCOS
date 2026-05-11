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
        int64_t schedulerSeq, SchedulerFactory::Ptr factory, ExecutorManager::Ptr executorManager);

    // by pbft & sync
    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool _sysBlock)>
            callback) override;
    // by pbft & sync
    void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override;
    void status(
        std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)> callback) override;
    void call(protocol::Transaction::Ptr tx,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override;
    void reset(std::function<void(Error::Ptr)> callback) override;
    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override;
    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override;

    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(std::string_view address,
        std::string_view key, bcos::protocol::BlockNumber number) override;

    void preExecuteBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(Error::Ptr)> callback) override;

    void asyncSwitchTerm(int64_t schedulerSeq, std::function<void(Error::Ptr&&)> callback);

    void initSchedulerIfNotExist();

    void registerOnSwitchTermHandler(std::function<void(bcos::protocol::BlockNumber)> onSwitchTerm);

    void handleNeedSwitchEvent(int64_t oldSchedulerTermID);

    void testTriggerSwitch();

    SchedulerFactory::Ptr getFactory();

    class SchedulerTerm
    {
    public:
        SchedulerTerm(int64_t schedulerSeq);

        SchedulerTerm next();
        int64_t getSchedulerTermID();


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
    void stop() override;

    void triggerSwitch();

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
