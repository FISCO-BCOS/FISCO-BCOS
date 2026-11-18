
/** @file Common.h
 * Miscellanea required for the Host/Session/NodeTable classes.
 *
 * @author yujiechen
 * @date: 2018-09-19
 */

#pragma once

#include "bcos-utilities/Error.h"
#include "bcos-utilities/ZstdCompress.h"
#include <bcos-framework/Common.h>
#include <bcos-framework/gateway/GatewayTypeDef.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/LogStream.h>
#include <boost/asio/ip/tcp.hpp>
#include <range/v3/view/any_view.hpp>
#include <set>
#include <string>
#include <string_view>

namespace ba = boost::asio;
namespace bi = boost::asio::ip;
#define HOST_LOG(LEVEL) BCOS_MODULE_LOG(GATEWAY, LEVEL) << "[NETWORK][Host]"
#define SESSION_LOG(LEVEL) BCOS_MODULE_LOG(GATEWAY, LEVEL) << "[SESSION][Session]"
#define ASIO_LOG(LEVEL) BCOS_MODULE_LOG(GATEWAY, LEVEL) << "[ASIO][ASIO]"

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

/// A payload compressed once by a fan-out caller and shared by every peer.
///
/// A broadcast hands the same bytes to N sessions. Compressing inside each session repeats the
/// whole zstd pass N times (measured: ~4.3 us per 64KB, ~97 us per 1MB). When the caller knows the
/// payload and the negotiated versions up front it can compress once and pass the result down; the
/// zstd frame is self-delimiting and independent of the P2P header, so every session can prepend
/// its own header and put this buffer on the wire unchanged.
struct PreparedPayload
{
    /// Compressed bytes when `compressed`; the original bytes otherwise (plaintext fallback).
    std::shared_ptr<const bytes> data;
    /// Whether `data` is a zstd frame, i.e. the COMPRESS ext bit must be set on the wire header.
    bool compressed = false;
};

struct Options
{
    uint32_t timeout = 0;   ///< The timeout value of async function, in milliseconds.
    bool response = false;  ///< Whether to wait for a response.
    /// Optional payload already compressed by the fan-out caller. When set, the session must send
    /// it verbatim instead of compressing again. Only valid when the shared payload is byte-identical
    /// to what this session would send.
    std::shared_ptr<const PreparedPayload> prepared;

    // Explicit constructors instead of aggregate init: adding `prepared` would otherwise trigger
    // -Wmissing-field-initializers on every existing `Options{timeout, response}` call site.
    Options() = default;
    explicit Options(uint32_t _timeout) : timeout(_timeout) {}
    Options(uint32_t _timeout, bool _response) : timeout(_timeout), response(_response) {}
};

/// Compress a fan-out payload ONCE for all peers.
///
/// Every peer receives the same bytes, so compressing inside each session repeats the entire zstd
/// pass per peer (measured: ~4.3 us per 64KB, ~97 us per 1MB). Returns nullptr when compression is
/// not applicable or yields no gain, in which case each session sends the original views and
/// compresses on its own as before.
inline std::shared_ptr<PreparedPayload> prepareFanoutPayload(
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads, bool enableCompress)
{
    if (!enableCompress)
    {
        return nullptr;
    }
    std::size_t total = 0;
    for (auto const& ref : payloads)
    {
        total += ref.size();
    }
    if (total <= c_compressThreshold)
    {
        return nullptr;
    }

    auto joined = std::make_shared<bytes>();
    joined->reserve(total);
    for (auto const& ref : payloads)
    {
        joined->insert(joined->end(), ref.begin(), ref.end());
    }

    auto compressed = std::make_shared<bytes>();
    if (!ZstdCompress::compress(bcos::ref(*joined), *compressed, (int)c_zstdCompressLevel) ||
        compressed->size() >= joined->size())
    {
        // no gain (high-entropy input can expand): let each session send the original views
        return nullptr;
    }
    auto prepared = std::make_shared<PreparedPayload>();
    prepared->data = std::move(compressed);
    prepared->compressed = true;
    return prepared;
}

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
/// @returns the PeerDisconnected reason tag (see docs/ops/log-events.md):
/// remote_close|local_close|timeout|handshake_failed|duplicate|blacklist|error
std::string_view disconnectReasonTag(DisconnectReason _reason);
/// the single emission point of the HandshakeFailed event. Inbound failures are INFO: anyone who
/// can reach the P2P port triggers them at their own rate; outbound (static peers) and protocol
/// mismatches after a successful TLS handshake are WARNING.
void logHandshakeFailed(bcos::LogLevel _level, NodeIPEndpoint const& _endpoint,
    std::string_view _reason, std::string_view _detail);
}  // namespace gateway
}  // namespace bcos
