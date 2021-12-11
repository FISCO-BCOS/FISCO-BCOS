
/** @file Common.h
 * Miscellanea required for the Host/Session/NodeTable classes.
 *
 * @author yujiechen
 * @date: 2018-09-19
 */

#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <chrono>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace ba = boost::asio;
namespace bi = boost::asio::ip;

#define HOST_LOG(LEVEL) BCOS_LOG(LEVEL) << "[BOOSTSSL][Host]"
#define SESSION_LOG(LEVEL) BCOS_LOG(LEVEL) << "[BOOSTSSL][Session]"
#define ASIO_LOG(LEVEL) BCOS_LOG(LEVEL) << "[BOOSTSSL][ASIO]"

namespace boostssl
{
namespace net
{
using byte = uint8_t;
using bytes = std::vector<byte>;
using bytesPointer = std::shared_ptr<std::vector<byte>>;

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
    IdleWaitTimeout = 0x11,
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

//
using P2pID = std::string;
using P2pIDs = std::set<std::string>;
struct Options
{
    Options() {}
    Options(uint32_t _timeout) : timeout(_timeout) {}
    uint32_t timeout = 0;  ///< The timeout value of async function, in milliseconds.
};

/// node info obtained from the certificate
struct P2PInfo
{
    P2pID p2pID;
    std::string agencyName;
    std::string nodeName;
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
    std::string m_msg;
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
    case IdleWaitTimeout:
        return "(Idle connection for no network io happens during 5s time "
               "intervals.)";
    default:
        return "Unknown reason.";
    }
}

// using NodeIPEndpoint = boost::asio::ip::tcp::endpoint;
/**
 * @brief client end endpoint. Node will connect to NodeIPEndpoint.
 */
struct NodeIPEndpoint
{
    NodeIPEndpoint() = default;
    NodeIPEndpoint(const NodeIPEndpoint& _nodeIPEndpoint) = default;
    NodeIPEndpoint(bi::address _addr, uint16_t _port)
      : m_host(_addr.to_string()), m_port(_port), m_ipv6(_addr.is_v6())
    {}

    virtual ~NodeIPEndpoint() = default;
    NodeIPEndpoint(const boost::asio::ip::tcp::endpoint& _endpoint)
    {
        m_host = _endpoint.address().to_string();
        m_port = _endpoint.port();
        m_ipv6 = _endpoint.address().is_v6();
    }
    bool operator<(const NodeIPEndpoint& rhs) const
    {
        if (m_host + std::to_string(m_port) < rhs.m_host + std::to_string(rhs.m_port))
        {
            return true;
        }
        return false;
    }
    bool operator==(const NodeIPEndpoint& rhs) const
    {
        return (m_host + std::to_string(m_port) == rhs.m_host + std::to_string(rhs.m_port));
    }
    operator boost::asio::ip::tcp::endpoint() const
    {
        return boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(m_host), m_port);
    }

    // Get the port associated with the endpoint.
    uint16_t port() const { return m_port; };

    // Get the IP address associated with the endpoint.
    std::string address() const { return m_host; };
    bool isIPv6() const { return m_ipv6; }

    std::string m_host;
    uint16_t m_port;
    bool m_ipv6 = false;
};

inline std::ostream& operator<<(std::ostream& _out, NodeIPEndpoint const& _endpoint)
{
    _out << _endpoint.address() << ":" << _endpoint.port();
    return _out;
}
}  // namespace net
}  // namespace boostssl
