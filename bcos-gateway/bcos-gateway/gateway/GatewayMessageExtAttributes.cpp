#include "GatewayMessageExtAttributes.h"

uint16_t bcos::gateway::GatewayMessageExtAttributes::moduleID() const
{
    return m_moduleID;
}

void bcos::gateway::GatewayMessageExtAttributes::setModuleID(uint16_t _moduleID)
{
    m_moduleID = _moduleID;
}

std::string bcos::gateway::GatewayMessageExtAttributes::groupID() const
{
    return m_groupID;
}

void bcos::gateway::GatewayMessageExtAttributes::setGroupID(const std::string& _groupID)
{
    m_groupID = _groupID;
}