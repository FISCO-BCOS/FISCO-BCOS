
/** @file Common.h
 * Miscellanea required for the Host/Session/NodeTable classes.
 *
 * @author yujiechen
 * @date: 2018-09-19
 */

#pragma once

#include "bcos-utilities/Error.h"
#include <bcos-framework/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/asio/ip/tcp.hpp>
#include <set>
#include <string>
#include <bcos-utilities/BoostLog.h>

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

/// default compress threshold: 1KB
const uint64_t c_compressThreshold = 1024;
/// default zstd compress level:
const uint64_t c_zstdCompressLevel = 1;
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

///< P2PExceptionType used as the error code carried by NetworkException
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

// NetworkException follows the DERIVE_BCOS_EXCEPTION convention (see
// bcos-framework/engine/Errors.h): the message is attached via errinfo_comment (read back by
// bcos::Exception::what()) and the error code via errinfo_errorCode.
using errinfo_errorCode = boost::error_info<struct tag_errorCode, int64_t>;

DERIVE_BCOS_EXCEPTION(NetworkException);

inline NetworkException makeNetworkException(int64_t errorCode, std::string msg)
{
    NetworkException e;
    e << errinfo_errorCode(errorCode);
    e << errinfo_comment(std::move(msg));
    return e;
}

[[nodiscard]] inline int64_t errorCodeOf(NetworkException const& e)
{
    if (auto const* errorCode = boost::get_error_info<errinfo_errorCode>(e))
    {
        return *errorCode;
    }
    return 0;
}

inline bool operator!(NetworkException const& e) { return errorCodeOf(e) == 0; }

inline Error::Ptr toError(NetworkException const& e)
{
    return BCOS_ERROR_PTR(errorCodeOf(e), e.what());
}

/// @returns the string form of the given disconnection reason.
std::string reasonOf(DisconnectReason _reason);
}  // namespace gateway
}  // namespace bcos
