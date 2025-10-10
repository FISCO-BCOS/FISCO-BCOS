#include "SchedulerManager.h"

using namespace bcos::scheduler;

// by pbft & sync
void SchedulerManager::executeBlock(bcos::protocol::Block::Ptr block, bool verify,
    std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool _sysBlock)>
        callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        SCHEDULER_LOG(DEBUG) << LOG_DESC("executeBlock: checkAndInit not ok")
                             << LOG_KV("message", message);
        callback(
            BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {}, false);
        return;
    }

    auto _holdSchedulerCallback =
        [schedulerHolder = m_scheduler, callback = std::move(callback)](bcos::Error::Ptr&& error,
            bcos::protocol::BlockHeader::Ptr&& blockHeader, bool _sysBlock) {
            SCHEDULER_LOG(TRACE) << "Release scheduler holder"
                                 << LOG_KV("ptr count", schedulerHolder.use_count());
            callback(std::move(error), std::move(blockHeader), _sysBlock);
        };

    m_scheduler->executeBlock(block, verify, std::move(_holdSchedulerCallback));
}

// by pbft & sync
void SchedulerManager::commitBlock(bcos::protocol::BlockHeader::Ptr header,
    std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        SCHEDULER_LOG(DEBUG) << LOG_DESC("commitBlock: checkAndInit not ok")
                             << LOG_KV("message", message);
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {});
        return;
    }

    auto _holdSchedulerCallback = [schedulerHolder = m_scheduler, callback = std::move(callback)](
                                      bcos::Error::Ptr&& error,
                                      bcos::ledger::LedgerConfig::Ptr&& ledger) {
        SCHEDULER_LOG(TRACE) << "Release scheduler holder"
                             << LOG_KV("ptr count", schedulerHolder.use_count());
        callback(std::move(error), std::move(ledger));
    };

    m_scheduler->commitBlock(header, std::move(_holdSchedulerCallback));
}

// by console, query committed and committing executing
void SchedulerManager::status(
    std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        SCHEDULER_LOG(DEBUG) << LOG_DESC("status: checkAndInit not ok")
                             << LOG_KV("message", message);
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {});
        return;
    }

    auto _holdSchedulerCallback = [schedulerHolder = m_scheduler, callback = std::move(callback)](
                                      bcos::Error::Ptr&& error,
                                      bcos::protocol::Session::ConstPtr&& session) {
        SCHEDULER_LOG(TRACE) << "Release scheduler holder"
                             << LOG_KV("ptr count", schedulerHolder.use_count());
        callback(std::move(error), std::move(session));
    };
    m_scheduler->status(std::move(_holdSchedulerCallback));
}

// by rpc
void SchedulerManager::call(protocol::Transaction::Ptr tx,
    std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        SCHEDULER_LOG(DEBUG) << LOG_DESC("call: checkAndInit not ok") << LOG_KV("message", message);
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {});
        return;
    }

    auto _holdSchedulerCallback = [schedulerHolder = m_scheduler, callback = std::move(callback)](
                                      bcos::Error::Ptr&& error,
                                      protocol::TransactionReceipt::Ptr&& receipt) {
        SCHEDULER_LOG(TRACE) << "Release scheduler holder"
                             << LOG_KV("ptr count", schedulerHolder.use_count());
        callback(std::move(error), std::move(receipt));
    };

    m_scheduler->call(tx, std::move(_holdSchedulerCallback));
}

// clear all status
void SchedulerManager::reset(std::function<void(Error::Ptr)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        SCHEDULER_LOG(DEBUG) << LOG_DESC("reset: checkAndInit not ok")
                             << LOG_KV("message", message);
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message));
        return;
    }

    m_scheduler->reset(std::move(callback));
}

void SchedulerManager::getCode(
    std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback)
{
    try
    {
        auto [ok, message] = checkAndInit();

        if (!ok)
        {
            callback(
                BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {});
            return;
        }

        auto _holdSchedulerCallback = [schedulerHolder = m_scheduler, callback =
                                                                          std::move(callback)](
                                          bcos::Error::Ptr&& error, bcos::bytes bytes) {
            SCHEDULER_LOG(TRACE) << "Release scheduler holder"
                                 << LOG_KV("ptr count", schedulerHolder.use_count());
            callback(std::move(error), std::move(bytes));
        };


        m_scheduler->getCode(contract, std::move(_holdSchedulerCallback));
    }
    catch (std::exception const& e)
    {
        SCHEDULER_LOG(WARNING) << LOG_DESC("SchedulerManager getCode failed")
                               << LOG_KV("codeAddr", contract)
                               << LOG_KV("error", boost::diagnostic_information(e));
        if (!callback)
        {
            return;
        }
        callback(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e)), bcos::bytes());
    }
}

void SchedulerManager::getABI(
    std::string_view contract, std::function<void(Error::Ptr, std::string)> callback)
{
    try
    {
        auto [ok, message] = checkAndInit();

        if (!ok)
        {
            callback(
                BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message), {});
            return;
        }

        auto _holdSchedulerCallback = [schedulerHolder = m_scheduler, callback =
                                                                          std::move(callback)](
                                          bcos::Error::Ptr&& error, std::string str) {
            SCHEDULER_LOG(TRACE) << "Release scheduler holder"
                                 << LOG_KV("ptr count", schedulerHolder.use_count());
            callback(std::move(error), std::move(str));
        };

        m_scheduler->getABI(contract, std::move(_holdSchedulerCallback));
    }
    catch (std::exception const& e)
    {
        SCHEDULER_LOG(WARNING) << LOG_DESC("getABI exception") << LOG_KV("contract", contract)
                               << LOG_KV("error", boost::diagnostic_information(e));
        callback(BCOS_ERROR_UNIQUE_PTR(-1, boost::diagnostic_information(e)), {});
    }
}

void SchedulerManager::preExecuteBlock(
    bcos::protocol::Block::Ptr block, bool verify, std::function<void(Error::Ptr)> callback)
{
    auto [ok, message] = checkAndInit();

    if (!ok)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::ExecutorNotEstablishedError, message));
        return;
    }

    auto _holdSchedulerCallback = [schedulerHolder = m_scheduler, callback = std::move(callback)](
                                      bcos::Error::Ptr&& error) {
        SCHEDULER_LOG(TRACE) << "Release scheduler holder"
                             << LOG_KV("ptr count", schedulerHolder.use_count());
        callback(std::move(error));
    };

    m_scheduler->preExecuteBlock(block, verify, std::move(_holdSchedulerCallback));
}

std::pair<bool, std::string> SchedulerManager::checkAndInit()
{
    if (m_status == STOPPED)
    {
        return {false, "Scheduler has stopped"};
    }

    initSchedulerIfNotExist();

    if (m_executorManager->size() == 0)
    {
        return {false, "Waiting to connect some executors..."};
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
    if (m_status == STOPPED)
    {
        return;
    }

    // Will update scheduler session, clear all scheduler & executor block pipeline cache and
    // re-dispatch executor
    m_pool.enqueue([this, callback = std::move(callback), schedulerSeq]() {
        try
        {
            switchTerm(schedulerSeq);
            callback(nullptr);
        }
        catch (std::exception const& _e)
        {
            SCHEDULER_LOG(ERROR) << "asyncSwitchTerm failed" << boost::diagnostic_information(_e);
        }
        catch (...)
        {
            SCHEDULER_LOG(ERROR) << "asyncSwitchTerm failed";
        }
    });
}


void SchedulerManager::initSchedulerIfNotExist()
{
    try
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
    }
    catch (Exception const& _e)
    {
        SCHEDULER_LOG(FATAL) << "initSchedulerIfNotExist failed" << diagnostic_information(_e);
        exit(-1);
    }
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
        if (m_scheduler->getSchedulerTermId() == schedulerTermId)
        {
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("Switch")
                                 << "SchedulerSwitch: scheduler has just switched, ignore trigger."
                                 << m_scheduler->getSchedulerTermId() << " == " << schedulerTermId;
            return;
        }
    }
    auto newScheduler = m_factory->build(schedulerTermId);

    if (m_scheduler)
    {
        m_scheduler->stop();
        SCHEDULER_LOG(DEBUG) << LOG_BADGE("Switch") << "SchedulerSwitch: scheduler term switch "
                             << m_scheduler->getSchedulerTermId() << "->" << schedulerTermId;
    }


    m_scheduler = newScheduler;
    m_scheduler->setOnNeedSwitchEventHandler(
        [this](int64_t oldSchedulerTermID) { handleNeedSwitchEvent(oldSchedulerTermID); });
}

void SchedulerManager::switchTerm(int64_t schedulerSeq)
{
    if (m_status == STOPPED)
    {
        return;
    }

    m_status.store(SWITCHING);
    try
    {
        auto newTerm = SchedulerTerm(schedulerSeq);
        updateScheduler(newTerm.getSchedulerTermID());  // may throw exception
        m_schedulerTerm = newTerm;

        m_status.store(RUNNING);
        onSwitchTermNotify();
    }
    catch (Exception const& _e)
    {
        m_status.store(RUNNING);
        SCHEDULER_LOG(ERROR) << "switchTerm failed. Re-push to task pool"
                             << diagnostic_information(_e);
        asyncSwitchTerm(schedulerSeq, {});
    }
}

void SchedulerManager::selfSwitchTerm(bool needCheckSwitching)
{
    if (m_status == STOPPED)
    {
        return;
    }

    auto status = m_status.exchange(SWITCHING);

    if (needCheckSwitching && status == SWITCHING)
    {
        // is self-switching, just return
        return;
    }

    try
    {
        auto newTerm = m_schedulerTerm.next();
        updateScheduler(newTerm.getSchedulerTermID());  // may throw exception
        m_schedulerTerm = newTerm;


        m_status.store(RUNNING);
        onSwitchTermNotify();
    }
    catch (Exception const& _e)
    {
        m_status.store(RUNNING);
        SCHEDULER_LOG(ERROR) << "selfSwitchTerm failed. Re-push to task pool"
                             << diagnostic_information(_e);
        asyncSelfSwitchTerm();
    }
}

void SchedulerManager::asyncSelfSwitchTerm()
{
    auto status = m_status.exchange(SWITCHING);
    if (status == SWITCHING)
    {
        // already just return
        return;
    }

    m_pool.enqueue([this]() { selfSwitchTerm(false); });
}

void SchedulerManager::onSwitchTermNotify()
{
    if (m_status == STOPPED)
    {
        return;
    }

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
