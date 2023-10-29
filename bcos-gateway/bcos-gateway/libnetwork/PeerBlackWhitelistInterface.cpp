/** @file PeerBlackWhitelistInterface.cpp
 * PeerBlackWhitelistInterface of peer connection
 * @author jimmyshi
 * @date: 2019-08-06
 */
#include "PeerBlackWhitelistInterface.h"
#include <sstream>

using namespace bcos;
using namespace gateway;

PeerBlackWhitelistInterface::PeerBlackWhitelistInterface(
    std::set<std::string> const& _strList, bool _enable)
  : m_enable(_enable)
{
    for (auto const& str : _strList)
    {
        m_peerList.insert(NodeID(str));
    }
}

PeerBlackWhitelistInterface::PeerBlackWhitelistInterface(
    std::set<NodeID> const& _nodeList, bool _enable)
  : m_enable(_enable)
{
    for (auto const& node : _nodeList)
    {
        m_peerList.emplace(node);
    }
}

bool PeerBlackWhitelistInterface::has(NodeID _peer) const
{
    if (!m_enable)
    {
        return hasValueWhenDisable();
    }

    bcos::Guard guard(x_peerList);

    auto itr = m_peerList.find(_peer);
    return itr != m_peerList.end();
}

bool PeerBlackWhitelistInterface::has(const std::string& _peer) const
{
    return has(NodeID(_peer));
}

std::string PeerBlackWhitelistInterface::dump(bool _isAbridged)
{
    bcos::Guard guard(x_peerList);

    std::stringstream ret;
    ret << LOG_KV("enable", m_enable) << LOG_KV("size", m_peerList.size()) << ",list[";
    for (auto nodeID : m_peerList)
    {
        if (_isAbridged)
        {
            ret << nodeID.abridged();
        }
        else
        {
            ret << nodeID;
        }
        ret << ",";  // It's ok to tail with ",]"
    }
    ret << "]";

    return ret.str();
}

void PeerBlackWhitelistInterface::update(std::set<std::string> const& _strList, bool _enable)
{
    bcos::Guard guard(x_peerList);

    m_peerList.clear();
    for(auto& str : _strList)
    {
        m_peerList.emplace(str);
    }
    m_enable = _enable;
}