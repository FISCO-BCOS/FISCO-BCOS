/** @file PeerBlackWhitelist.cpp
 * Peer blacklist/whitelist of peer connection
 * @author jimmyshi
 * @date: 2019-08-06
 */
#include "PeerBlackWhitelist.h"
#include "Common.h"
#include <mutex>
#include <sstream>

namespace bcos::gateway
{

PeerBlackWhitelist::PeerBlackWhitelist(
    Type _type, std::set<std::string> const& _strList, bool _enable)
  : m_type(_type), m_enable(_enable)
{
    for (auto const& str : _strList)
    {
        m_peerList.insert(P2PNodeID(str));
    }
}

PeerBlackWhitelist::PeerBlackWhitelist(
    Type _type, std::set<P2PNodeID> const& _nodeList, bool _enable)
  : m_type(_type), m_enable(_enable)
{
    for (auto const& node : _nodeList)
    {
        m_peerList.emplace(node);
    }
}

PeerBlackWhitelist::PeerBlackWhitelist(PeerBlackWhitelist&& _other) noexcept
{
    bcos::Guard guard(_other.x_peerList);
    m_type = _other.m_type;
    m_enable.store(_other.m_enable.load(std::memory_order_relaxed), std::memory_order_relaxed);
    m_peerList = std::move(_other.m_peerList);
}

PeerBlackWhitelist& PeerBlackWhitelist::operator=(PeerBlackWhitelist&& _other) noexcept
{
    if (this != &_other)
    {
        std::scoped_lock lock(x_peerList, _other.x_peerList);
        m_type = _other.m_type;
        m_enable.store(_other.m_enable.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_peerList = std::move(_other.m_peerList);
    }
    return *this;
}

bool PeerBlackWhitelist::has(P2PNodeID _peer) const
{
    if (!m_enable.load(std::memory_order_relaxed))
    {
        // disabled blacklist blocks no one, disabled whitelist passes everyone
        return m_type == Type::Whitelist;
    }

    bcos::Guard guard(x_peerList);

    auto itr = m_peerList.find(_peer);
    return itr != m_peerList.end();
}

bool PeerBlackWhitelist::has(const std::string& _peer) const
{
    return has(P2PNodeID(_peer));
}

void PeerBlackWhitelist::setEnable(bool _enable)
{
    m_enable.store(_enable, std::memory_order_relaxed);
}

bool PeerBlackWhitelist::enable() const
{
    return m_enable.load(std::memory_order_relaxed);
}

size_t PeerBlackWhitelist::size() const
{
    bcos::Guard guard(x_peerList);
    return m_peerList.size();
}

std::string PeerBlackWhitelist::dump(bool _isAbridged)
{
    bcos::Guard guard(x_peerList);

    std::stringstream ret;
    ret << LOG_KV("enable", m_enable.load()) << LOG_KV("size", m_peerList.size()) << ",list[";
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

void PeerBlackWhitelist::update(std::set<std::string> const& _strList, bool _enable)
{
    bcos::Guard guard(x_peerList);

    m_peerList.clear();
    for (auto& str : _strList)
    {
        m_peerList.emplace(str);
    }
    m_enable = _enable;
}

}  // namespace bcos::gateway
