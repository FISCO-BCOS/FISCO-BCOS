/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2019-2021 fisco-dev contributors.
 */

/**
 * @brief event log filter manager.
 * @author: octopuswang
 * @date: 2019-08-13
 */

#pragma once
#include "Common.h"
#include "EventLogFilter.h"
#include <libdevcore/Worker.h>
#include <atomic>

#define MAX_BLOCK_PER_PROCESS (3)

namespace Json
{
class Value;
}

namespace dev
{
namespace blockchain
{
class BlockChainInterface;
}  // namespace blockchain
namespace event
{
using EventLogFilterVector = std::vector<EventLogFilter::Ptr>;

class EventLogFilterManager : public Worker
{
public:
    using Ptr = std::shared_ptr<EventLogFilterManager>;

public:
    // constructor function
    EventLogFilterManager(std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        int64_t _maxBlockRange = -1, int64_t _maxBlockPerLoopFilter = 10)
      : Worker("eventLog", 0),
        m_blockChain(_blockChain),
        m_maxBlockRange(_maxBlockRange),
        m_maxBlockPerLoopFilter(_maxBlockPerLoopFilter)
    {}

    // destructor function
    virtual ~EventLogFilterManager() { stop(); }

public:
    // m_blockChain
    std::shared_ptr<dev::blockchain::BlockChainInterface> getBlockChain() const
    {
        return m_blockChain;
    }
    // m_maxBlockRange
    int64_t getMaxBlockRange() const { return m_maxBlockRange; }
    // m_maxBlockPerLoopFilter
    int64_t getMaxBlockPerFilter() const { return m_maxBlockPerLoopFilter; }
    // m_waitAddCount
    uint64_t getWaitAddCount() const { return m_waitAddCount.load(std::memory_order_relaxed); }
    // m_filters
    const EventLogFilterVector& filters() const { return m_filters; }

public:
    // start EventLogFilterManager
    void start();
    // stop EventLogFilterManager
    void stop();

public:
    // add EventLogFilter to m_filters by client json request
    int32_t addEventLogFilterByRequest(const EventLogFilterParams::Ptr _params, uint32_t _version,
        std::function<bool(const std::string& _filterID, int32_t _result, const Json::Value& _logs,
            GROUP_ID const& _groupId)>
            _respCallback,
        std::function<int(GROUP_ID _groupId)> _sessionCheckerCallback);

    // delete EventLogFilter in m_filters by client json request
    int32_t cancelEventLogFilterByRequest(
        const EventLogFilterParams::Ptr _params, uint32_t _version);

public:
    bool isErrorStatus(filter_status status)
    {
        return ((status == filter_status::GROUP_ID_NOT_EXIST) ||
                (status == filter_status::CALLBACK_FAILED) ||
                (status == filter_status::ERROR_STATUS));
    }

    // main loop thread
    void doWork() override;
    void addFilter();
    void cancelFilter();
    void executeFilters();
    filter_status executeFilter(EventLogFilter::Ptr _filter);
    // add _filter to m_filters waiting for loop thread to process
    void addEventLogFilter(EventLogFilter::Ptr _filter);
    // delete _filter in m_filters waiting for loop thread to process
    void cancelEventLogFilter(EventLogFilter::Ptr _filter);

private:
    // the vector for all EventLogFilter
    EventLogFilterVector m_filters;
    // the EventLogFilter to be add to m_filters
    EventLogFilterVector m_waitAddFilter;
    // metux for m_waitAddFilter
    mutable std::mutex m_addMutex;
    // the count of EventLogFilter to be add to m_filters, reduce the range of lock
    std::atomic<uint64_t> m_waitAddCount{0};
    // the EventLogFilter to be removed in m_filters
    EventLogFilterVector m_waitCancelFilter;
    // metux for m_waitCancelFilter
    mutable std::mutex m_cancelMutex;
    // the count of EventLogFilter to be removed in m_filters, reduce the range of lock
    std::atomic<uint64_t> m_waitCancelCount{0};
    // the blockchain of this EventLogFilterManager own to
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    //  max block range should be permited
    // | startBlock  < ------ range -------- >  blockNumber |
    int64_t m_maxBlockRange;
    // for one EventLogFilter every loop filter max block
    int64_t m_maxBlockPerLoopFilter;
    // if EventLogFilterManager start or not
    bool m_isInit{false};
};
}  // namespace event
}  // namespace dev
