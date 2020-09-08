
#include "EventLogFilterManager.h"
#include <json/json.h>
#include <libblockchain/BlockChainInterface.h>
#include <libdevcore/TopicInfo.h>
#include <libethcore/CommonJS.h>
#include <libeventfilter/EventLogFilterParams.h>
#include <libledger/LedgerParam.h>


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

    EVENT_LOG(INFO) << LOG_BADGE("EventLogFilterManager") << LOG_DESC("start")
                    << LOG_KV("m_maxBlockRange", m_maxBlockRange)
                    << LOG_KV("m_maxBlockPerLoopFilter", m_maxBlockPerLoopFilter);
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
    // add or delete new EventLogFilter to m_filters
    addFilter();
    cancelFilter();
    executeFilters();
}

filter_status EventLogFilterManager::executeFilter(EventLogFilter::Ptr _filter)
{
    filter_status status;
    std::string strDesc;

    EventLogFilter::Ptr filter = _filter;
    do
    {
        auto blockchain = getBlockChain();

        // the blockNumber of this blockchain
        BlockNumber blockNumber = blockchain->number();
        BlockNumber nextBlockToProcess = filter->getNextBlockToProcess();

        auto result = (filter->getSessionCheckerCallback())(filter->getParams()->getGroupID());
        // check if session is actived
        if (result == filter_status::CALLBACK_FAILED)
        {
            // maybe sesseion disconnect
            strDesc = "session not active";
            status = filter_status::CALLBACK_FAILED;
            break;
        }
        // check sdk exists in the allow list or not
        if (result == filter_status::REMOTE_PEERS_ACCESS_DENIED)
        {
            strDesc = "The SDK is not allowed to access this group";
            status = filter_status::REMOTE_PEERS_ACCESS_DENIED;
            break;
        }
        if (blockNumber < nextBlockToProcess)
        {  // wait for more block to be sealed
            status = filter_status::WAIT_FOR_MORE_BLOCK;
            break;
        }

        int64_t leftBlockCanProcess = blockNumber - nextBlockToProcess + 1;
        // Process up to m_maxBlockPerFilter blocks at a time, default MAX_BLOCK_PER_PROCESS
        int64_t thisLoopBlockCanProcess = MAX_BLOCK_PER_PROCESS;
        int64_t maxBlockPerLoopFilter = getMaxBlockPerFilter();
        if (maxBlockPerLoopFilter > 0 && maxBlockPerLoopFilter > leftBlockCanProcess)
        {
            thisLoopBlockCanProcess = leftBlockCanProcess;
        }

        Json::Value response(Json::arrayValue);
        for (int64_t i = 0; i < thisLoopBlockCanProcess; i++)
        {
            EVENT_LOG(TRACE) << LOG_BADGE("executeFilter")
                             << LOG_KV("blockNumber", nextBlockToProcess + i);
            auto block = blockchain->getBlockByNumber(nextBlockToProcess + i);
            filter->matches(*block.get(), response);
        }

        filter->updateNextBlockToProcess(nextBlockToProcess + thisLoopBlockCanProcess);

        // call back
        if (!response.empty() && !filter->getResponseCallback()(filter->getParams()->getFilterID(),
                                     0, response, filter->getParams()->getGroupID()))
        {  // call back failed, maybe sesseion disconnect
            strDesc = "response callback failed, session not active";
            status = filter_status::CALLBACK_FAILED;
            break;
        }

        // this filter push completed, remove this filter
        if (filter->pushCompleted())
        {
            filter->getResponseCallback()(filter->getParams()->getFilterID(), PUSH_COMPLETED,
                Json::Value(), filter->getParams()->getGroupID());
            status = filter_status::STATUS_PUSH_COMPLETED;
            break;
        }

        // There are remaining blocks to continue processing in the next loop
        status =
            (leftBlockCanProcess > thisLoopBlockCanProcess ? filter_status::WAIT_FOR_NEXT_LOOP :
                                                             filter_status::WAIT_FOR_MORE_BLOCK);
    } while (0);

    return status;
}

// add EventLogFilter to m_filters by client json request
int32_t EventLogFilterManager::addEventLogFilterByRequest(const EventLogFilterParams::Ptr _params,
    uint32_t _version,
    std::function<bool(const std::string& _filterID, int32_t _result, const Json::Value& _logs,
        GROUP_ID const& _groupId)>
        _respCallback,
    std::function<int(GROUP_ID _groupId)> _sessionCheckerCallback)
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
            nextBlockToProcess = params->getFromBlock();
        }

        // toBlock is smaller than nextBlockToProcess
        if (params->getToBlock() < nextBlockToProcess)
        {
            responseCode = ResponseCode::INVALID_REQUEST_RANGE;
            break;
        }

        // check block range valid be| startBlock  < ------ range -------- >  blockNumber |
        if (getMaxBlockRange() > 0 && (blockNumber >= nextBlockToProcess) &&
            (blockNumber - nextBlockToProcess + 1 > getMaxBlockRange()))
        {  // request invalid block range
            responseCode = ResponseCode::INVALID_REQUEST_RANGE;
            break;
        }

        auto filter = std::make_shared<EventLogFilter>(params, nextBlockToProcess, _version);
        filter->setResponseCallBack(_respCallback);
        filter->setSessionCheckerCallBack(_sessionCheckerCallback);

        // add filter to vector wait for loop thread to process
        addEventLogFilter(filter);
    } while (0);

    EVENT_LOG(INFO) << LOG_BADGE("addEventLogFilterByRequest") << LOG_KV("result", responseCode)
                    << LOG_KV("channel protocol version", _version);
    return responseCode;
}

// add _filter to m_filters waiting for loop thread to process
void EventLogFilterManager::addEventLogFilter(EventLogFilter::Ptr _filter)
{
    {
        std::lock_guard<std::mutex> l(m_addMetux);
        m_waitAddFilter.push_back(_filter);
        m_waitAddCount += 1;
    }
}

// delete EventLogFilter in m_filters by client json request
int32_t EventLogFilterManager::cancelEventLogFilterByRequest(const EventLogFilterParams::Ptr _params, uint32_t _version)
{
    ResponseCode responseCode = ResponseCode::SUCCESS;
    
    EventLogFilter::Ptr filter = NULL;
    for (auto it = m_filters.begin(); it != m_filters.end(); it++)
    {
        if ((*it)->getParams()->getFilterID() == _params->getFilterID()) 
        {
            filter = *it;
        }
    }
    if (filter != NULL)
    {
        cancelEventLogFilter(filter);
    } else {
        responseCode = NONEXISTENT_EVENT;
    }

    EVENT_LOG(INFO) << LOG_BADGE("cancelEventLogFilterByRequest") << LOG_KV("result", responseCode)
                    << LOG_KV("channel protocol version", _version);
    return responseCode;
}

// delete _filter in m_filters waiting for loop thread to process
void EventLogFilterManager::cancelEventLogFilter(EventLogFilter::Ptr _filter)
{
    {
        std::lock_guard<std::mutex> l(m_cancelMetux);
        m_waitCancelFilter.push_back(_filter);
        m_waitCancelCount += 1;
    }
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

void EventLogFilterManager::cancelFilter()
{
    uint64_t cancelCount = m_waitCancelCount.load(std::memory_order_relaxed);
    if (cancelCount > 0)
    {
        std::lock_guard<std::mutex> l(m_cancelMetux);
        {
            // delelte all waiting filters in m_filters to be processed
            for (EventLogFilter::Ptr filter : m_waitCancelFilter) {
                string cannelFilterID = filter->getParams()->getFilterID();
                for (auto it = m_filters.begin(); it != m_filters.end();)
                {
                    string filterID = (*it)->getParams()->getFilterID();
                    if (filterID == cannelFilterID)
                    {
                        EVENT_LOG(TRACE) << LOG_BADGE("cancelFilter") << LOG_KV("filterID", filterID);
                        m_filters.erase(it);
                    } 
                    else 
                    {
                        it++;
                    }
                }
            }

            m_waitCancelFilter.clear();
            m_waitCancelCount = 0;
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
        EVENT_LOG(TRACE) << LOG_BADGE("executeFilters") << LOG_KV("filterId", filter->getParams()->getFilterID())
                         << LOG_KV("groupID", filter->getParams()->getGroupID())
                         << LOG_KV("nextBlockToProcess", filter->getNextBlockToProcess())
                         << LOG_KV("startBlockNumber", filter->getParams()->getFromBlock())
                         << LOG_KV("endBlockNumber", filter->getParams()->getToBlock());

        auto status = executeFilter(filter);
        if ((isErrorStatus(status)) || (status == filter_status::STATUS_PUSH_COMPLETED))
        {
            EVENT_LOG(INFO) << LOG_BADGE("executeFilters") << LOG_DESC("remove filter")
                            << LOG_KV("status", static_cast<int>(status))
                            << LOG_KV("filterID", filter->getParams()->getFilterID())
                            << LOG_KV("fromBlock", filter->getParams()->getFromBlock())
                            << LOG_KV("toBlock", filter->getParams()->getToBlock())
                            << LOG_KV("currentBlock", filter->getNextBlockToProcess());
            it = m_filters.erase(it);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}