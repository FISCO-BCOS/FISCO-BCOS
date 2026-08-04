#include "bcos-framework/multigroup/GroupInfo.h"

#include <bcos-utilities/BoostLog.h>
#include <sstream>

namespace bcos::group
{
ChainNodeInfo::Ptr GroupInfo::nodeInfo(std::string_view _nodeName) const
{
    ReadGuard l(x_nodeInfos);
    auto it = m_nodeInfos.find(_nodeName);
    if (it == m_nodeInfos.end())
    {
        return nullptr;
    }
    return it->second;
}

bool GroupInfo::appendNodeInfo(ChainNodeInfo::Ptr _nodeInfo)
{
    UpgradableGuard l(x_nodeInfos);
    auto const& nodeName = _nodeInfo->nodeName();
    auto it = m_nodeInfos.find(nodeName);
    if (it != m_nodeInfos.end())
    {
        return false;
    }
    UpgradeGuard ul(l);
    m_nodeInfos[nodeName] = std::move(_nodeInfo);
    return true;
}

void GroupInfo::updateNodeInfo(ChainNodeInfo::Ptr _nodeInfo)
{
    WriteGuard l(x_nodeInfos);
    auto const& nodeName = _nodeInfo->nodeName();
    auto it = m_nodeInfos.find(nodeName);
    if (it != m_nodeInfos.end())
    {
        *(it->second) = *_nodeInfo;
        return;
    }
    m_nodeInfos[nodeName] = std::move(_nodeInfo);
}

bool GroupInfo::removeNodeInfo(std::string const& _nodeName)
{
    UpgradableGuard l(x_nodeInfos);
    auto it = m_nodeInfos.find(_nodeName);
    if (it == m_nodeInfos.end())
    {
        return false;
    }
    UpgradeGuard ul(l);
    m_nodeInfos.erase(it);
    return true;
}

int64_t GroupInfo::nodesNum() const
{
    ReadGuard l(x_nodeInfos);
    return m_nodeInfos.size();
}

std::string printGroupInfo(const GroupInfo::Ptr& _groupInfo)
{
    if (!_groupInfo)
    {
        return "";
    }

    std::stringstream oss;
    oss << LOG_KV("group", _groupInfo->groupID()) << LOG_KV("chain", _groupInfo->chainID())
        << LOG_KV("nodeSize", _groupInfo->nodesNum());
    return oss.str();
}
}  // namespace bcos::group