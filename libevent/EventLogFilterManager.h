#pragma once
#include "EventLogFilter.h"
#include <libdevcore/Worker.h>
#include <atomic>
namespace dev
{
namespace ledger
{
class LedgerManager;
class LedgerParamInterface;
}  // namespace ledger
namespace event
{
using IDs = std::vector<uint64_t>;
using EventLogFilterVector = std::vector<EventLogFilter::Ptr>;

class EventLogFilterManager : public Worker
{
public:
    using Ptr = std::shared_ptr<EventLogFilterManager>;

public:
    // constructor function
    EventLogFilterManager(
        std::shared_ptr<dev::ledger::LedgerManager> _ledgerManager, int64_t _maxBlockRange = -1)
      : m_ledgerManager(_ledgerManager), m_maxBlockRange(_maxBlockRange)
    {}
    // destructor function
    virtual ~EventLogFilterManager() { stopWorking(); }

public:
    // m_ledgerManager
    std::shared_ptr<dev::ledger::LedgerManager> getLedgerManager() const { return m_ledgerManager; }
    int64_t getMaxBlockRange() const { return m_maxBlockRange; }

public:
    // start EventLogFilterManager
    void start();
    // stop EventLogFilterManager
    void stop();
    // add EventLogFilter to m_filters by client json request
    void addEventLogFilter(const std::string& _json);

private:
    // main loop thread
    void doWork() override;
    void doAdd();
    void doMatch();

private:
    // the vector for all EventLogFilter
    EventLogFilterVector m_filters;
    // the EventLogFilter to be add to m_filters
    EventLogFilterVector m_waitAddFilter;
    // metux for m_waitAddFilter
    mutable std::mutex m_addMetux;
    // the count of EventLogFilter to be add to m_filters, reduce the range of lock
    std::atomic<uint64_t> m_waitAddCount{0};
    // the LedgerManager of blockchain
    std::shared_ptr<dev::ledger::LedgerManager> m_ledgerManager;
    // max block range should be permited
    // | startBlock  < ------ range -------- >  blockNumber |
    int64_t m_maxBlockRange{1000};
};
}  // namespace event
}  // namespace dev
