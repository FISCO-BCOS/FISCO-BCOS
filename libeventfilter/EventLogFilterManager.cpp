
#include "EventLogFilterManager.h"
#include "Common.h"
#include <libblockchain/BlockChainInterface.h>
#include <libethcore/CommonJS.h>
#include <libeventfilter/EventLogFilterParams.h>

using namespace std;
using namespace dev;
using namespace dev::event;
using namespace dev::eth;

// start EventLogFilterManager
void EventLogFilterManager::start()
{
    if (m_isInit)
    {  // already start
        return;
    }
    startWorking();
    m_isInit = true;
    EVENT_LOG(INFO) << LOG_BADGE("EventLogFilterManager") << LOG_DESC("start");
}

// stop EventLogFilterManager
void EventLogFilterManager::stop()
{
    if (m_isInit)
    {
        stopWorking();
        // will not restart worker, so terminate it
        terminate();
        m_isInit = false;
        EVENT_LOG(INFO) << LOG_BADGE("EventLogFilterManager") << LOG_DESC("stop");
    }
}

// main loop thread
void EventLogFilterManager::doWork()
{
    // add new EventLogFilter to m_filters
    addFilter();
    executeFilters();
}

EventLogFilterManager::filter_status EventLogFilterManager::executeFilter(
    EventLogFilter::Ptr _filter)
{
    EVENT_LOG(DEBUG) << LOG_KV("groupID", _filter->getParams()->getGroupID());
    // ADD TO DO
    return filter_status::WAIT_FOR_MORE_BLOCK;
}

// add EventLogFilter to m_filters by client json request
int32_t EventLogFilterManager::addEventLogFilterByRequest(const EventLogFilterParams::Ptr _params,
    uint32_t _version,
    std::function<bool(int32_t, const std::string&, uint32_t, const std::string&, bool)> _callback)
{
    ResponseCode responseCode = ResponseCode::SUCCESS;
    do
    {
        auto params = _params;
        auto blockchain = getBlockChain();

        // the blockNumber of this blockchain.
        BlockNumber blockNumber = blockchain->number();
        BlockNumber nextBlockToProcess = 0;
        // check startBlock valid and update nextBlockToProcess.
        if (params->getFromBlock() == MAX_BLOCK_NUMBER)
        {  // begin from current block.
            nextBlockToProcess = blockNumber;
        }
        else
        {
            // request startBlock is bigger than current block number, permited or not ???
            if (params->getFromBlock() > blockNumber)
            {
                responseCode = ResponseCode::INVALID_REQUEST_RANGE;
                break;
            }

            nextBlockToProcess = params->getFromBlock();
        }

        // check block range valid be| startBlock  < ------ range -------- >  blockNumber |
        if (getMaxBlockRange() > 0 && (blockNumber - nextBlockToProcess + 1 > getMaxBlockRange()))
        {  // request invalid block range
            responseCode = ResponseCode::INVALID_REQUEST_RANGE;
            break;
        }

        auto filter = std::make_shared<EventLogFilter>(params, _version, nextBlockToProcess);
        filter->setResponseCallBack(_callback);

        // add filter to vector wait for loop thread to process
        addEventLogFilter(filter);
    } while (0);

    EVENT_LOG(INFO) << LOG_BADGE("addEventLogFilterByRequest") << LOG_KV("version", _version)
                    << LOG_KV("retCode", static_cast<int32_t>(responseCode));
    return static_cast<int32_t>(responseCode);
}

// add _filter to m_filters waiting for loop thread to process
void EventLogFilterManager::addEventLogFilter(EventLogFilter::Ptr _filter)
{
    {
        std::lock_guard<std::mutex> l(m_addMetux);
        m_waitAddFilter.push_back(_filter);
        m_waitAddCount += 1;
    }
    EVENT_LOG(INFO) << LOG_KV("filterID", _filter->getParams()->getFilterID());
}

void EventLogFilterManager::addFilter()
{
    uint64_t addCount = m_waitAddCount.load(std::memory_order_relaxed);
    if (addCount > 0)
    {
        std::lock_guard<std::mutex> l(m_addMetux);
        {
            // insert all waiting filters to m_filters to be processed
            m_filters.insert(m_filters.begin(), m_waitAddFilter.begin(), m_waitAddFilter.end());
            m_waitAddFilter.clear();
            m_waitAddCount = 0;
        }
    }
}

void EventLogFilterManager::executeFilters()
{
    // event log filter and send response to client
    bool shouldSleep = true;
    for (auto it = m_filters.begin(); it != m_filters.end();)
    {
        EventLogFilter::Ptr filter = *it;
        EVENT_LOG(TRACE) << LOG_BADGE("doWork")
                         << LOG_KV("groupID", filter->getParams()->getGroupID())
                         << LOG_KV("nextBlockToProcess", filter->getNextBlockToProcess());

        auto status = executeFilter(filter);
        if ((isErrorStatus(status)) || (status == filter_status::PUSH_COMPLETED))
        {
            it = m_filters.erase(it);
            EVENT_LOG(INFO) << LOG_BADGE("doWork") << LOG_DESC("remove filter");
            continue;
        }
        else if (status == filter_status::WAIT_FOR_NEXT_LOOP)
        {
            shouldSleep = false;
        }

        ++it;
    }

    if (shouldSleep)
    {
        EVENT_LOG(TRACE) << LOG_BADGE("doWork") << LOG_KV("filter count", filters().size());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
