/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Common.h
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 *
 * Miscellanea required for the Host/Session/NodeTable classes.
 *
 * @author yujiechen
 * @date: 2018-09-19
 * @modifications:
 * 1. remove DeadlineOps(useless class)
 * 2. remove isPrivateAddress method for logical imperfect
 * 3. remove std::hash<bi::address> class since it has not been used
 */

#pragma once

#include <set>
#include <string>
#include <vector>

// Make sure boost/asio.hpp is included before windows.h.
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/logic/tribool.hpp>

#include <libdevcore/Exceptions.h>
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Protocol.h>
#include <chrono>
#include <sstream>

namespace ba = boost::asio;
namespace bi = boost::asio::ip;

namespace dev
{
namespace p2p
{
extern unsigned c_defaultIPPort;

class NodeIPEndpoint;
class Node;
extern const NodeIPEndpoint UnspecifiedNodeIPEndpoint;
extern const Node UnspecifiedNode;

using NodeID = h512;
/// Nice name for vector of NodeID.
using NodeIDs = std::vector<NodeID>;

bool isLocalHostAddress(bi::address const& _addressToCheck);
bool isLocalHostAddress(std::string const& _addressToCheck);
bool isPublicAddress(bi::address const& _addressToCheck);
bool isPublicAddress(std::string const& _addressToCheck);

/// define Exceptions
DEV_SIMPLE_EXCEPTION(NetworkStartRequired);
DEV_SIMPLE_EXCEPTION(InvalidPublicIPAddress);
DEV_SIMPLE_EXCEPTION(InvalidHostIPAddress);

enum DisconnectReason
{
    DisconnectRequested = 0,
    TCPError,
    BadProtocol,
    UselessPeer,
    TooManyPeers,
    DuplicatePeer,
    IncompatibleProtocol,
    NullIdentity,
    ClientQuit,
    UnexpectedIdentity,
    LocalIdentity,
    PingTimeout,
    UserReason = 0x10,
    NoDisconnect = 0xffff
};

///< P2PExceptionType and g_P2PExceptionMsg used in P2PException
enum P2PExceptionType
{
    Success = 0,
    ProtocolError,
    NetworkTimeout,
    Disconnect,
    P2PExceptionTypeCnt
};

static std::string g_P2PExceptionMsg[P2PExceptionTypeCnt] = {
    "Success", "ProtocolError", "NetworkTimeout", "Disconnect"};

enum PacketDecodeStatus
{
    PACKET_ERROR = -1,
    PACKET_INCOMPLETE = 0
};

struct Options
{
    uint32_t subTimeout = 0;  ///< The timeout value of every node, used in send message to topic, in
                          ///< milliseconds.
    uint32_t timeout = 0;     ///< The timeout value of async function, in milliseconds.
};

class Message: public std::enable_shared_from_this<Message> {
public:
	virtual ~Message() {};

	typedef std::shared_ptr<Message> Ptr;

	virtual uint32_t length() = 0;
	virtual uint32_t seq() = 0;

	virtual void encode(bytes &buffer) = 0;
	virtual ssize_t decode(const byte *buffer, size_t size) = 0;
};

class MessageFactory : public std::enable_shared_from_this<MessageFactory>
{
public:
    typedef std::shared_ptr<MessageFactory> Ptr;

    virtual ~MessageFactory(){};
    virtual Message::Ptr buildMessage() = 0;
};

class NetworkException : public std::exception
{
public:
    NetworkException(){};
    NetworkException(int _errorCode, const std::string& _msg) : m_errorCode(_errorCode), m_msg(_msg){};

    virtual int errorCode() { return m_errorCode; };
    virtual const char* what() const _GLIBCXX_USE_NOEXCEPT override { return m_msg.c_str(); };
    bool operator!() const { return m_errorCode == 0; }

private:
    int m_errorCode = 0;
    std::string m_msg = "";
};

class Session;

/// @returns the string form of the given disconnection reason.
std::string reasonOf(DisconnectReason _r);

/**
 * @brief IPv4,UDP/TCP endpoints.
 */
struct NodeIPEndpoint
{
    /// Setting true causes isAllowed to return true for all addresses. (Used by test fixtures)
    static bool test_allowLocal;

    NodeIPEndpoint() = default;
    NodeIPEndpoint(bi::address _addr, uint16_t _udp, uint16_t _tcp)
      : address(_addr), udpPort(_udp), tcpPort(_tcp)
    {}
    NodeIPEndpoint(std::string _host, uint16_t _udp, uint16_t _tcp)
      : udpPort(_udp), tcpPort(_tcp), host(_host)
    {
        address = HostResolver::query(host);
    }
    NodeIPEndpoint(const NodeIPEndpoint& _nodeIPEndpoint)
    {
        address = _nodeIPEndpoint.address;
        udpPort = _nodeIPEndpoint.udpPort;
        tcpPort = _nodeIPEndpoint.tcpPort;
        host = _nodeIPEndpoint.host;
    }
    operator bi::udp::endpoint() const { return bi::udp::endpoint(address, udpPort); }
    operator bi::tcp::endpoint() const { return bi::tcp::endpoint(address, tcpPort); }

    operator bool() const { return !address.is_unspecified() && udpPort > 0 && tcpPort > 0; }

    bool isAllowed() const
    {
        return NodeIPEndpoint::test_allowLocal ? !address.is_unspecified() :
                                                 isPublicAddress(address);
    }
    bool operator==(NodeIPEndpoint const& _cmp) const
    {
        return address == _cmp.address && udpPort == _cmp.udpPort && tcpPort == _cmp.tcpPort;
    }
    bool operator!=(NodeIPEndpoint const& _cmp) const { return !operator==(_cmp); }
    bool operator<(const dev::p2p::NodeIPEndpoint& rhs) const
    {
        if (address < rhs.address)
        {
            return true;
        }

        return tcpPort < rhs.tcpPort;
    }
    std::string name() const
    {
        std::ostringstream os;
        os << address.to_string() << ":" << tcpPort << ":" << udpPort;
        return os.str();
    }
    bool isValid() { return (!address.to_string().empty()) && (tcpPort != 0); }
    bi::address address;
    uint16_t udpPort = 0;
    uint16_t tcpPort = 0;
    std::string host;
};

/*
 * Used by Host to pass negotiated information about a connection to a
 * new Peer Session; PeerSessionInfo is then maintained by Session and can
 * be queried for point-in-time status information via Host.
 */
struct PeerSessionInfo
{
    NodeID const id;
    std::string const host;
    std::chrono::steady_clock::duration lastPing;
    unsigned socketId;
};

using PeerSessionInfos = std::vector<PeerSessionInfo>;

struct SessionInfo
{
    NodeID nodeID;
    NodeIPEndpoint nodeIPEndpoint;
    std::set<std::string> topics;
    SessionInfo(NodeID _nodeID, NodeIPEndpoint _nodeIPEndpoint,
        std::set<std::string> _topics)
    {
        nodeID = _nodeID;
        nodeIPEndpoint = _nodeIPEndpoint;
        topics = _topics;
    }
};
using SessionInfos = std::vector<SessionInfo>;

}  // namespace p2p
/// Simple stream output for a NodeIPEndpoint.
std::ostream& operator<<(std::ostream& _out, dev::p2p::NodeIPEndpoint const& _ep);
}  // namespace dev
