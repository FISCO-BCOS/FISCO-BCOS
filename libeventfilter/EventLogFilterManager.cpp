
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
    filter_status status;
    std::string strDesc;

    EventLogFilter::Ptr filter = _filter;
    do
    {
        auto blockchain = getBlockChain();

        // the blockNumber of this blockchain
        BlockNumber blockNumber = blockchain->number();
        BlockNumber nextBlockToProcess = filter->getNextBlockToProcess();

        // check if session active
        if (!filter->getResponseCallBack()(static_cast<int32_t>(ResponseCode::SUCCESS),
                std::string(""), 0x1002, std::string(""), false))
        {  // call back failed, maybe sesseion disconnect
            strDesc = "Session Not Active";
            status = filter_status::CALLBACK_FAILED;
            break;
        }

        if (blockNumber <= nextBlockToProcess)
        {  // wait for more block to be sealed
            status = filter_status::WAIT_FOR_MORE_BLOCK;
            break;
        }

        int64_t leftBlockCanProcess = blockNumber - nextBlockToProcess + 1;
        // Process up to m_maxBlockPerFilter blocks at a time, default 10
        int64_t thisLoopBlockCanProcess =
            (getMaxBlockPerFilter() > 0 ?
                    (leftBlockCanProcess > getMaxBlockPerFilter() ? getMaxBlockPerFilter() :
                                                                    leftBlockCanProcess) :
                    10);
        Json::Value response(Json::arrayValue);
        for (int64_t i = 0; i < thisLoopBlockCanProcess; i++)
        {
            auto block = blockchain->getBlockByNumber(nextBlockToProcess + i);
            filter->matches(*block.get(), response);
        }

        filter->updateNextBlockToProcess(nextBlockToProcess + thisLoopBlockCanProcess);

        Json::FastWriter writer;
        auto resp = writer.write(response);
        // call back
        if (!filter->getResponseCallBack()(static_cast<int32_t>(ResponseCode::SUCCESS),
                filter->getParams()->getFilterID(), 0x1002, resp, true))
        {  // call back failed, maybe sesseion disconnect
            strDesc = "respCallBackFailed, Session Not Active";
            status = filter_status::CALLBACK_FAILED;
            break;
        }

        // this filter push completed, remove this filter
        if (filter->pushCompleted())
        {
            filter->getResponseCallBack()(static_cast<int32_t>(ResponseCode::PUSH_COMPLETED),
                filter->getParams()->getFilterID(), 0x1002, std::string(""), true);
            status = filter_status::PUSH_COMPLETED;
            break;
        }

        // There are remaining blocks to continue processing in the next loop
        status =
            (leftBlockCanProcess > thisLoopBlockCanProcess ? filter_status::WAIT_FOR_NEXT_LOOP :
                                                             filter_status::WAIT_FOR_MORE_BLOCK);
    } while (0);

    if (isErrorStatus(status))
    {
        EVENT_LOG(WARNING) << LOG_BADGE("doWork")
                           << LOG_KV("groupID", filter->getParams()->getGroupID())
                           << LOG_KV("nextBlockToProcess", filter->getNextBlockToProcess())
                           << LOG_KV("startBlockNumber", filter->getParams()->getFromBlock())
                           << LOG_KV("endBlockNumber", filter->getParams()->getToBlock())
                           << LOG_KV("decs", strDesc);
    }

    return status;
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

        // 1. check if groupID exists
        auto blockchain = getBlockChain();

        // the blockNumber of this blockchain.
        BlockNumber blockNumber = blockchain->number();
        BlockNumber nextBlockToProcess = 0;

        // 2. check startBlock valid and update nextBlockToProcess.
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

        // 3. check block range valid
        // | startBlock  < ------ range -------- >  blockNumber |
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
    EVENT_LOG(INFO) << LOG_BADGE("addEventLogFilter0")
                    << LOG_KV("channel protocol version", _filter->getChannelProtocolVersion());
    {
        std::lock_guard<std::mutex> l(m_addMetux);
        m_waitAddFilter.push_back(_filter);
        m_waitAddCount += 1;
    }

    EVENT_LOG(INFO) << LOG_BADGE("addEventLogFilter1")
                    << LOG_KV("channel protocol version", _filter->getChannelProtocolVersion());
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
                         << LOG_KV("nextBlockToProcess", filter->getNextBlockToProcess())
                         << LOG_KV("startBlockNumber", filter->getParams()->getFromBlock())
                         << LOG_KV("endBlockNumber", filter->getParams()->getToBlock());

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
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}