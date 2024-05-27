
/** @file Common.h
 * Miscellanea required for the Host/Session/NodeTable classes.
 *
 * @author yujiechen
 * @date: 2018-09-19
 */

#pragma once

#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/gateway/GatewayTypeDef.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/asio/ip/tcp.hpp>
#include <set>
#include <string>

namespace ba = boost::asio;
namespace bi = boost::asio::ip;
#define HOST_LOG(LEVEL) BCOS_LOG(LEVEL) << "[NETWORK][Host]"
#define SESSION_LOG(LEVEL) BCOS_LOG(LEVEL) << "[SESSION][Session]"
#define ASIO_LOG(LEVEL) BCOS_LOG(LEVEL) << "[ASIO][ASIO]"

namespace bcos
{
namespace gateway
{
enum MessageDecodeStatus
{
    MESSAGE_ERROR = -1,
    MESSAGE_INCOMPLETE = 0,
};
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
    NegotiateFailed = 0x12,
    InBlacklistReason = 0x13,
    NotInWhitelistReason = 0x14,
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
    OutBWOverflow,
    InQPSOverflow,
    ALL
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

class NetworkException : public std::exception
{
public:
    NetworkException() = default;
    NetworkException(int _errorCode, const std::string& _msg)
      : m_errorCode(_errorCode), m_msg(_msg){};

    virtual int errorCode() const { return m_errorCode; };
    const char* what() const noexcept override { return m_msg.c_str(); };
    bool operator!() const { return m_errorCode == 0; }

    virtual Error::Ptr toError() { return BCOS_ERROR_PTR(errorCode(), m_msg); }

private:
    int m_errorCode = 0;
    std::string m_msg;
};

/// @returns the string form of the given disconnection reason.
inline std::string reasonOf(DisconnectReason _reason)
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
        return "(Idle connection for no network io happens during 60s time "
               "intervals.)";
    default:
        return "Unknown reason.";
    }
}
}  // namespace gateway
}  // namespace bcos
