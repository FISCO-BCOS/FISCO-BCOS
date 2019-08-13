#pragma once
#include <libethcore/Block.h>
#include <libethcore/LogEntry.h>
#include <libevent/EventLogFilterParams.h>
namespace dev
{
namespace event
{
class EventLogFilter
{
public:
    using Ptr = std::shared_ptr<EventLogFilter>;

public:
    // constructor function
    EventLogFilter(EventLogFilterParams::Ptr _params, eth::BlockNumber _initalNumber)
      : m_params(_params), m_nextBlockToProcess(_initalNumber)
    {}

public:
    // m_params
    EventLogFilterParams::Ptr getParams() const { return m_params; }
    // m_nextBlockToProcess
    eth::BlockNumber getNextBlockToProcess() const { return m_nextBlockToProcess; }
    // m_responseCallback
    std::function<bool(int32_t, std::string)> getResponseCallBack() { return m_responseCallback; }

    // update m_nextBlockToProcess
    void updateNextBlockToProcess(eth::BlockNumber _nextBlockToProcess)
    {
        m_nextBlockToProcess = _nextBlockToProcess;
    }
    // set response call back
    void setResponseCallBack(std::function<bool(int32_t, std::string)> _responseCallback)
    {
        m_responseCallback = _responseCallback;
    }

    void matches(eth::Block const& _block, Json::Value& result);
    // filter individual log to see if the requirements are met
    bool matches(eth::LogEntry const& _log);
    Json::Value toJson(
        eth::Block const& _block, std::size_t _transactionIndex, std::size_t _logIndex);

private:
    // event filter params generate from client request.
    EventLogFilterParams::Ptr m_params;
    // next block number to be processed.
    eth::BlockNumber m_nextBlockToProcess;
    // response callback function
    std::function<bool(int32_t, std::string)> m_responseCallback;
};

}  // namespace event
}  // namespace dev
