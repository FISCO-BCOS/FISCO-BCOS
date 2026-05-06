#include "bcos-framework/multigroup/ChainNodeInfo.h"

#include <bcos-utilities/BoostLog.h>
#include <sstream>

namespace bcos::group
{
ChainNodeInfo::ChainNodeInfo()
  : m_nodeProtocol(std::make_shared<bcos::protocol::ProtocolInfo>())
{}

ChainNodeInfo::ChainNodeInfo(std::string const& _nodeName, int32_t _type) : ChainNodeInfo()
{
    m_nodeName = _nodeName;
    m_nodeCryptoType = static_cast<NodeCryptoType>(_type);
}

std::string const& ChainNodeInfo::serviceName(bcos::protocol::ServiceType _type) const
{
    auto it = m_servicesInfo.find(_type);
    if (it == m_servicesInfo.end())
    {
        return c_emptyServiceName;
    }
    return it->second;
}

void ChainNodeInfo::appendServiceInfo(
    bcos::protocol::ServiceType _type, std::string const& _serviceName)
{
    m_servicesInfo[_type] = _serviceName;

    BCOS_LOG(TRACE) << LOG_BADGE("ChainNodeInfo") << LOG_DESC("appendServiceInfo")
                    << LOG_KV("type", _type) << LOG_KV("name", getServiceNameByType(_type))
                    << LOG_KV("serviceName", _serviceName);
}

std::string printNodeInfo(ChainNodeInfo::Ptr _nodeInfo)
{
    if (!_nodeInfo)
    {
        return "";
    }

    std::stringstream oss;
    oss << LOG_KV("name", _nodeInfo->nodeName())
        << LOG_KV("cryptoType", std::to_string((int32_t)_nodeInfo->nodeCryptoType()))
        << LOG_KV("nodeType", _nodeInfo->nodeType());
    auto const& serviceInfos = _nodeInfo->serviceInfo();
    oss << ", serviceInfos: ";
    for (auto const& info : serviceInfos)
    {
        oss << info.second << ",";
    }
    return oss.str();
}
}  // namespace bcos::group