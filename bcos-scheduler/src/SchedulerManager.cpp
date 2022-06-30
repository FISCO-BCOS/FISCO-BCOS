#include "SchedulerManager.h"

using namespace bcos::scheduler;

// by pbft & sync
void SchedulerManager::executeBlock(bcos::protocol::Block::Ptr block, bool verify,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool _sysBlock)>
        callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        callback(
            BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {}, false);
        return;
    }

    m_scheduler->executeBlock(block, verify, std::move(callback));
}

// by pbft & sync
void SchedulerManager::commitBlock(bcos::protocol::BlockHeader::Ptr header,
    std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {});
        return;
    }

    m_scheduler->commitBlock(header, std::move(callback));
}

// by console, query committed committing executing
void SchedulerManager::status(
    std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {});
        return;
    }
    m_scheduler->status(std::move(callback));
}

// by rpc
void SchedulerManager::call(protocol::Transaction::Ptr tx,
    std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {});
        return;
    }

    m_scheduler->call(tx, std::move(callback));
}

// by executor
void SchedulerManager::registerExecutor(std::string name,
    bcos::executor::ParallelTransactionExecutorInterface::Ptr executor,
    std::function<void(Error::Ptr&&)> callback)
{
    initSchedulerIfNotExist();
    m_scheduler->registerExecutor(name, executor, std::move(callback));
}

void SchedulerManager::unregisterExecutor(
    const std::string& name, std::function<void(Error::Ptr&&)> callback)
{
    initSchedulerIfNotExist();
    m_scheduler->unregisterExecutor(name, std::move(callback));
}

// clear all status
void SchedulerManager::reset(std::function<void(Error::Ptr&&)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message));
        return;
    }

    m_scheduler->reset(std::move(callback));
}

void SchedulerManager::getCode(
    std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {});
        return;
    }

    m_scheduler->getCode(contract, std::move(callback));
}

void SchedulerManager::getABI(
    std::string_view contract, std::function<void(Error::Ptr, std::string)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {});
        return;
    }

    m_scheduler->getABI(contract, std::move(callback));
}

void SchedulerManager::preExecuteBlock(
    bcos::protocol::Block::Ptr block, bool verify, std::function<void(Error::Ptr&&)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message));
        return;
    }

    m_scheduler->preExecuteBlock(block, verify, std::move(callback));
}

std::pair<bool, std::string> SchedulerManager::checkAndInit()
{
    initSchedulerIfNotExist();

    if (m_remoteExecutorManager->empty())
    {
        return {false, "No executor started!"};
    }

    if (m_status == INITIALING)
    {
        return {false, "Scheduler is initialing, please wait and retry"};
    }

    if (m_status == SWITCHING)
    {
        return {false, "Scheduler is switching, please wait and retry"};
    }

    return {true, "ok"};
}

void SchedulerManager::asyncSwitchTerm(
    int64_t schedulerSeq, std::function<void(Error::Ptr&&)> callback)
{
    // Will update scheduler session, clear all scheduler & executor block pipeline cache and
    // re-dispatch executor
    m_pool.enqueue([this, callback = std::move(callback), schedulerSeq]() {
        switchTerm(schedulerSeq);
        callback(nullptr);
    });
}


void SchedulerManager::initSchedulerIfNotExist()
{
    if (!m_scheduler || m_status == INITIALING)
    {
        static bcos::SharedMutex mutex;
        bcos::WriteGuard lock(mutex);
        if (!m_scheduler || m_status == INITIALING)
        {
            updateScheduler(m_schedulerTerm.getSchedulerTermID());
            m_status.store(RUNNING);
        }
    }

    // testTriggerSwitch();  // Just a test code, TODO: remove me
}

void SchedulerManager::registerOnSwitchTermHandler(
    std::function<void(bcos::protocol::BlockNumber)> onSwitchTerm)
{
    // onSwitchTerm(latest Uncommitted blockNumber)
    m_onSwitchTermHandlers.push_back(std::move(onSwitchTerm));
}

void SchedulerManager::handleNeedSwitchEvent(int64_t oldSchedulerTermID)
{
    auto currentSchedulerTermID = m_schedulerTerm.getSchedulerTermID();
    if (m_status == SWITCHING)
    {
        SCHEDULER_LOG(DEBUG) << LOG_BADGE("Switch")
                             << "handleNeedSwitchEvent: SchedulerManager is switching. Ignore."
                             << LOG_KV("currentSchedulerTermID", currentSchedulerTermID)
                             << LOG_KV("oldSchedulerTermID", oldSchedulerTermID);
        return;
    }
    else if (currentSchedulerTermID > oldSchedulerTermID)
    {
        SCHEDULER_LOG(DEBUG) << LOG_BADGE("Switch")
                             << "handleNeedSwitchEvent: Ignore outdated oldSchedulerTermID"
                             << LOG_KV("currentSchedulerTermID", currentSchedulerTermID)
                             << LOG_KV("oldSchedulerTermID", oldSchedulerTermID);
        return;
    }
    else
    {
        SCHEDULER_LOG(DEBUG) << LOG_BADGE("Switch") << "handleNeedSwitchEvent: Trigger switch "
                             << LOG_KV("currentSchedulerTermID", currentSchedulerTermID)
                             << LOG_KV("oldSchedulerTermID", oldSchedulerTermID);


        asyncSelfSwitchTerm();
    }
}

void SchedulerManager::testTriggerSwitch()
{
    static std::set<int64_t> blockNumberHasSwitch;
    if (utcTime() - m_scheduler->getSchedulerTermId() > 30000)
    {
        static bcos::SharedMutex mutex;
        bcos::WriteGuard l(mutex);

        // Get current blockNumber
        std::promise<protocol::BlockNumber> blockNumberFuture;
        m_factory->getLedger()->asyncGetBlockNumber(
            [&blockNumberFuture](Error::Ptr error, protocol::BlockNumber number) {
                if (error)
                {
                    SCHEDULER_LOG(ERROR) << "Scheduler get blockNumber from storage failed";
                    blockNumberFuture.set_value(-1);
                }
                else
                {
                    blockNumberFuture.set_value(number);
                }
            });
        auto blockNumber = blockNumberFuture.get_future().get();

        // trigger switch
        if (utcTime() - m_scheduler->getSchedulerTermId() > 30000 &&
            blockNumberHasSwitch.count(blockNumber) == 0)
        {
            selfSwitchTerm();
            blockNumberHasSwitch.insert(blockNumber);
        }
    }
}


void SchedulerManager::updateScheduler(int64_t schedulerTermId)
{
    if (m_scheduler)
    {
        m_scheduler->stop();
        m_oldScheduler = m_scheduler;
        SCHEDULER_LOG(DEBUG) << LOG_BADGE("Switch") << "SchedulerSwitch: scheduler term switch "
                             << m_scheduler->getSchedulerTermId() << "->" << schedulerTermId;
    }

    m_scheduler = m_factory->build(schedulerTermId);
    m_scheduler->setOnNeedSwitchEventHandler(
        [this](int64_t oldSchedulerTermID) { handleNeedSwitchEvent(oldSchedulerTermID); });
}

void SchedulerManager::switchTerm(int64_t schedulerSeq)
{
    m_status.store(SWITCHING);
    m_schedulerTerm = SchedulerTerm(schedulerSeq);
    updateScheduler(m_schedulerTerm.getSchedulerTermID());

    m_status.store(RUNNING);
    onSwitchTermNotify();
}

void SchedulerManager::selfSwitchTerm()
{
    if (m_status == SWITCHING)
    {
        // is self-switching, just return
        return;
    }

    m_status.store(SWITCHING);
    m_schedulerTerm = m_schedulerTerm.next();
    updateScheduler(m_schedulerTerm.getSchedulerTermID());

    m_status.store(RUNNING);
    onSwitchTermNotify();
}

void SchedulerManager::asyncSelfSwitchTerm()
{
    m_pool.enqueue([this]() { selfSwitchTerm(); });
}

void SchedulerManager::onSwitchTermNotify()
{
    m_factory->getLedger()->asyncGetBlockNumber(
        [this](Error::Ptr error, protocol::BlockNumber blockNumber) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Could not get blockNumber from ledger on scheduler switch term";
                return;
            }

            for (auto& onSwitchTerm : m_onSwitchTermHandlers)
            {
                onSwitchTerm(blockNumber + 1);
            }
        });
}
