/** @file GatewayConfig.h
 *  @author octopus
 *  @date 2021-05-19
 */

#pragma once

#include <bcos-gateway/libnetwork/Message.h>

namespace bcos
{
namespace gateway
{

class GatewayMessageExtAttributes : public MessageExtAttributes
{
public:
    using Ptr = std::shared_ptr<GatewayMessageExtAttributes>;
    using ConstPtr = std::shared_ptr<GatewayMessageExtAttributes>;

public:
    uint16_t moduleID() { return m_moduleID; }
    void setModuleID(uint16_t _moduleID) { m_moduleID = _moduleID; }

    std::string groupID() { return m_groupID; }
    void setGroupID(const std::string& _groupID) { m_groupID = _groupID; }

private:
    uint16_t m_moduleID = 0;
    std::string m_groupID;
};

}  // namespace gateway
}  // namespace bcos