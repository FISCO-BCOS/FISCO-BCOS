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
 * @brief Event log filter params object.
 * @author: octopuswang
 * @date: 2019-08-13
 */

#pragma once
#include "Common.h"
#include <libethcore/Common.h>
#include <libethcore/Protocol.h>
#include <libutilities/DataConvertUtility.h>

namespace Json
{
class Value;
}

namespace bcos
{
namespace event
{
class EventLogFilterParams
{
public:
    using Ptr = std::shared_ptr<EventLogFilterParams>;

private:
    static bool getFilterIDField(const Json::Value& _json, std::string& _filterID);
    static bool getGroupIDField(const Json::Value& _json, bcos::GROUP_ID& _groupID);
    static bool getFromBlockField(const Json::Value& _json, eth::BlockNumber& _startBlock);
    static bool getToBlockField(const Json::Value& _json, eth::BlockNumber& _endBlock);
    static bool getAddressField(const Json::Value& _json, EventLogFilterParams::Ptr params);
    static bool getTopicField(const Json::Value& _json, EventLogFilterParams::Ptr params);

public:
    // from json received from client create EventLogFilterParams Object
    static EventLogFilterParams::Ptr buildEventLogFilterParamsObject(const std::string& _json);

public:
    // constructor
    EventLogFilterParams(int _groupID, std::string _filterID, eth::BlockNumber _startBlock,
        eth::BlockNumber _endBlock)
      : m_groupID(_groupID), m_filterID(_filterID), m_startBlock(_startBlock), m_endBlock(_endBlock)
    {}
    // m_groupID
    bcos::GROUP_ID getGroupID() const { return m_groupID; }
    // m_filterID
    std::string getFilterID() const { return m_filterID; }
    // m_startBlock
    eth::BlockNumber getFromBlock() const { return m_startBlock; }
    // m_endBlock
    eth::BlockNumber getToBlock() const { return m_endBlock; }
    // m_addresses
    const AddressHash& getAddresses() const { return m_addresses; }
    // m_topics
    const std::array<h256Hash, eth::MAX_NUM_TOPIC_EVENT_LOG>& getTopics() const { return m_topics; }
    // Add a matching address to m_addresses
    void addAddress(Address const& _a) { m_addresses.insert(_a); }
    // Add a matching topic to m_addresses
    bool addTopic(unsigned _index, h256 const& _t)
    {
        if (_index < eth::MAX_NUM_TOPIC_EVENT_LOG)
        {
            m_topics[_index].insert(_t);
        }

        return (_index < eth::MAX_NUM_TOPIC_EVENT_LOG);
    }

private:
    bcos::GROUP_ID m_groupID;
    std::string m_filterID;
    eth::BlockNumber m_startBlock;
    eth::BlockNumber m_endBlock;
    AddressHash m_addresses;
    std::array<h256Hash, eth::MAX_NUM_TOPIC_EVENT_LOG> m_topics;
};

}  // namespace event
}  // namespace bcos
