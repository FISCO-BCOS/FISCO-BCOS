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
 * @brief Event log filter object.
 * @author: octopuswang
 * @date: 2019-08-13
 */

#pragma once
#include <libethcore/Block.h>
#include <libethcore/LogEntry.h>
#include <libeventfilter/EventLogFilterParams.h>
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
    EventLogFilter(
        EventLogFilterParams::Ptr _params, eth::BlockNumber _nextBlockToProcess, uint32_t _version)
      : m_params(_params),
        m_nextBlockToProcess(_nextBlockToProcess),
        m_channelProtocolVersion(_version)
    {}

public:
    // m_params
    EventLogFilterParams::Ptr getParams() const { return m_params; }
    // m_nextBlockToProcess
    eth::BlockNumber getNextBlockToProcess() const { return m_nextBlockToProcess; }
    // m_responseCallback
    std::function<bool(const std::string& _filterID, int32_t _result, const Json::Value& _logs,
        GROUP_ID const& _groupId)>
    getResponseCallback()
    {
        return m_responseCallback;
    }
    // m_sessionActive
    std::function<int(GROUP_ID _groupId)> getSessionCheckerCallback() { return m_sessionChecker; }

    // this filter pushed end
    bool pushCompleted() const { return m_nextBlockToProcess > m_params->getToBlock(); }

    // update m_nextBlockToProcess
    void updateNextBlockToProcess(eth::BlockNumber _nextBlockToProcess)
    {
        m_nextBlockToProcess = _nextBlockToProcess;
    }
    // set response call back
    void setResponseCallBack(std::function<bool(const std::string& _filterID, int32_t _result,
            const Json::Value& _logs, GROUP_ID const& _groupId)>
            _callback)
    {
        m_responseCallback = _callback;
    }

    void setSessionCheckerCallBack(std::function<int(GROUP_ID _groupId)> _callback)
    {
        m_sessionChecker = _callback;
    }

    uint32_t getChannelProtocolVersion() const { return m_channelProtocolVersion; }

    void matches(eth::Block const& _block, Json::Value& _result);
    // filter individual log to see if the requirements are met
    bool matches(eth::LogEntry const& _log);

private:
    // event filter params generate from client request.
    EventLogFilterParams::Ptr m_params;
    // next block number to be processed.
    eth::BlockNumber m_nextBlockToProcess;
    // channel protocol version
    uint32_t m_channelProtocolVersion;
    // response callback function
    std::function<bool(const std::string& _filterID, int32_t _result, const Json::Value& _logs,
        GROUP_ID const& _groupId)>
        m_responseCallback;
    // connect active check function
    std::function<int(GROUP_ID _groupId)> m_sessionChecker;
};

}  // namespace event
}  // namespace dev
