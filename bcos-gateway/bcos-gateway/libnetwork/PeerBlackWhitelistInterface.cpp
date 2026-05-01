/** @file PeerBlackWhitelistInterface.cpp
 * PeerBlackWhitelistInterface of peer connection
 * @author jimmyshi
 * @date: 2019-08-06
 */
#include "Common.h"
#include "PeerBlacklist.h"
#include "PeerBlackWhitelistInterface.h"
#include "PeerWhitelist.h"
#include <sstream>

using namespace bcos;
using namespace gateway;

NetworkException::NetworkException(int _errorCode, std::string _msg)
  : m_errorCode(_errorCode), m_msg(std::move(_msg))
{}

int NetworkException::errorCode() const
{
    return m_errorCode;
}

const char* NetworkException::what() const noexcept
{
    return m_msg.c_str();
}

bool NetworkException::operator!() const
{
    return m_errorCode == 0;
}

Error::Ptr NetworkException::toError()
{
    return BCOS_ERROR_PTR(errorCode(), m_msg);
}

std::string bcos::gateway::reasonOf(DisconnectReason _reason)
{
    switch (_reason)
    {
    case DisconnectRequested:
        return "Disconnect was requested.";
    case TCPError:
        return "Low-level TCP communication error.";
    case BadProtocol:
        return "Data format error.";
    case UselessPeer:
        return "Peer had no use for this node.";
    case TooManyPeers:
        return "Peer had too many connections.";
    case DuplicatePeer:
        return "Peer was already connected.";
    case IncompatibleProtocol:
        return "Peer protocol versions are incompatible.";
    case NullIdentity:
        return "Null identity given.";
    case ClientQuit:
        return "Peer is exiting.";
    case UnexpectedIdentity:
        return "Unexpected identity given.";
    case LocalIdentity:
        return "Connected to ourselves.";
    case UserReason:
        return "Subprotocol reason.";
    case NoDisconnect:
        return "(No disconnect has happened.)";
    case IdleWaitTimeout:
        return "(Idle connection for no network io happens during 60s time intervals.)";
    default:
        return "Unknown reason.";
    }
}

PeerBlackWhitelistInterface::PeerBlackWhitelistInterface(
    std::set<std::string> const& _strList, bool _enable)
  : m_enable(_enable)
{
    for (auto const& str : _strList)
    {
        m_peerList.insert(P2PNodeID(str));
    }
}

PeerBlackWhitelistInterface::PeerBlackWhitelistInterface(
    std::set<P2PNodeID> const& _nodeList, bool _enable)
  : m_enable(_enable)
{
    for (auto const& node : _nodeList)
    {
        m_peerList.emplace(node);
    }
}

bool PeerBlackWhitelistInterface::has(P2PNodeID _peer) const
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
    return has(P2PNodeID(_peer));
}

void PeerBlackWhitelistInterface::setEnable(bool _enable)
{
    m_enable = _enable;
}

bool PeerBlackWhitelistInterface::enable() const
{
    return m_enable;
}

size_t PeerBlackWhitelistInterface::size()
{
    return m_peerList.size();
}

bool PeerBlacklist::hasValueWhenDisable() const
{
    return false;
}

PeerBlacklist::PeerBlacklist(std::set<std::string> const& _strList, bool _enable)
    : PeerBlackWhitelistInterface(_strList, _enable)
{}

PeerBlacklist::PeerBlacklist(std::set<P2PNodeID> const& _nodeList, bool _enable)
    : PeerBlackWhitelistInterface(_nodeList, _enable)
{}

bool PeerWhitelist::hasValueWhenDisable() const
{
    return true;
}

PeerWhitelist::PeerWhitelist(std::set<std::string> const& _strList, bool _enable)
    : PeerBlackWhitelistInterface(_strList, _enable)
{}

PeerWhitelist::PeerWhitelist(std::set<P2PNodeID> const& _nodeList, bool _enable)
    : PeerBlackWhitelistInterface(_nodeList, _enable)
{}

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
    for (auto& str : _strList)
    {
        m_peerList.emplace(str);
    }
    m_enable = _enable;
}