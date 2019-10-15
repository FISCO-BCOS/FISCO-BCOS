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
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Protocol.h>
#include <chrono>
#include <sstream>

namespace ba = boost::asio;
namespace bi = boost::asio::ip;
#define HOST_LOG(LEVEL) LOG(LEVEL) << "[NETWORK][Host]"
#define SESSION_LOG(LEVEL) LOG(LEVEL) << "[NETWORK][Session]"
#define ASIO_LOG(LEVEL) LOG(LEVEL) << "[NETWORK][ASIO]"

namespace dev
{
namespace network
{
struct NodeIPEndpoint;
class Node;
extern const NodeIPEndpoint UnspecifiedNodeIPEndpoint;
extern const Node UnspecifiedNode;

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
    P2PExceptionTypeCnt,
    ConnectError,
    DuplicateSession,
    NotInWhitelist,
    ALL,
};

enum PacketDecodeStatus
{
    PACKET_ERROR = -1,
    PACKET_INCOMPLETE = 0
};

using NodeID = dev::h512;

struct Options
{
    uint32_t subTimeout = 0;  ///< The timeout value of every node, used in send message to topic,
                              ///< in milliseconds.
    uint32_t timeout = 0;     ///< The timeout value of async function, in milliseconds.
};

/// node info obtained from the certificate
struct NodeInfo
{
    NodeID nodeID;
    std::string agencyName;
    std::string nodeName;
};

class Message : public std::enable_shared_from_this<Message>
{
public:
    virtual ~Message(){};

    typedef std::shared_ptr<Message> Ptr;

    virtual uint32_t length() = 0;
    virtual uint32_t seq() = 0;

    virtual bool isRequestPacket() = 0;

    virtual void encode(bytes& buffer) = 0;
    virtual ssize_t decode(const byte* buffer, size_t size) = 0;
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
    NetworkException(int _errorCode, const std::string& _msg)
      : m_errorCode(_errorCode), m_msg(_msg){};

    virtual int errorCode() { return m_errorCode; };
    virtual const char* what() const noexcept override { return m_msg.c_str(); };
    bool operator!() const { return m_errorCode == 0; }

private:
    int m_errorCode = 0;
    std::string m_msg = "";
};

/// @returns the string form of the given disconnection reason.
inline std::string reasonOf(DisconnectReason _r)
{
    switch (_r)
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
    default:
        return "Unknown reason.";
    }
}

/**
 * @brief client end endpoint. Node will connect to NodeIPEndpoint.
 */
struct NodeIPEndpoint
{
    NodeIPEndpoint() = default;
    NodeIPEndpoint(const std::string& _host, const std::string& _port) : host(_host), port(_port) {}
    NodeIPEndpoint(const std::string& _host, uint16_t _port)
      : host(_host), port(std::to_string(_port))
    {}
    NodeIPEndpoint(bi::address _addr, uint16_t _port)
      : host(_addr.to_string()), port(std::to_string(_port))
    {}
    NodeIPEndpoint(const NodeIPEndpoint& _nodeIPEndpoint)
    {
        host = _nodeIPEndpoint.host;
        port = _nodeIPEndpoint.port;
    }

    bool operator<(const NodeIPEndpoint& rhs) const
    {
        if (host + port < rhs.host + rhs.port)
        {
            return true;
        }
        return false;
    }

    bool operator==(const NodeIPEndpoint& rhs) const
    {
        return (host + port == rhs.host + rhs.port);
    }

    std::string name() const
    {
        std::ostringstream os;
        os << host << ":" << port;
        return os.str();
    }
    std::string host;
    // port may be a descriptive name or a numeric string corresponding to a port number
    std::string port;
};

}  // namespace network
}  // namespace dev
