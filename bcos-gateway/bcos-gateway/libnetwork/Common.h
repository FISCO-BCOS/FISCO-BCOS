
/** @file Common.h
 * Miscellanea required for the Host/Session/NodeTable classes.
 *
 * @author yujiechen
 * @date: 2018-09-19
 */

#pragma once

#include "bcos-utilities/Error.h"
#include <bcos-framework/Common.h>
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
    uint32_t timeout = 0;   ///< The timeout value of async function, in milliseconds.
    bool response = false;  ///< Whether to wait for a response.
};

class NetworkException : public std::exception
{
public:
    NetworkException() = default;
    NetworkException(int _errorCode, std::string _msg);

    virtual int errorCode() const;
    const char* what() const noexcept override;
    bool operator!() const;

    virtual Error::Ptr toError();

private:
    int m_errorCode = 0;
    std::string m_msg;
};

/// @returns the string form of the given disconnection reason.
std::string reasonOf(DisconnectReason _reason);
}  // namespace gateway
}  // namespace bcos
