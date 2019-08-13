#pragma once
#include "Common.h"
#include <libdevcore/Address.h>
#include <libdevcore/CommonData.h>
#include <libethcore/Common.h>
#pragma clang diagnostic ignored "-Wunused-private-field"
namespace dev
{
namespace event
{
class EventLogFilterParams
{
public:
    using Ptr = std::shared_ptr<EventLogFilterParams>;

public:
    // from json received from client create EventLogFilterParams Object
    static std::pair<bool, EventLogFilterParams::Ptr> buildEventLogFilterParamsObject(
        const std::string& _json);

public:
    // constructor
    EventLogFilterParams(int _groupID, eth::BlockNumber _startBlock, eth::BlockNumber _endBlock)
      : m_groupID(_groupID), m_startBlock(_startBlock), m_endBlock(_endBlock)
    {}

private:
    int m_groupID;
    eth::BlockNumber m_startBlock;
    eth::BlockNumber m_endBlock;
    AddressHash m_addresses;
    std::array<h256Hash, 4> m_topics;
};
}  // namespace event
}  // namespace dev
