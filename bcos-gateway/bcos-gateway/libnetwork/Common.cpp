/** @file Common.cpp
 * Miscellanea required for the Host/Session/NodeTable classes.
 */
#include "Common.h"

namespace bcos::gateway
{

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

std::string reasonOf(DisconnectReason _reason)
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

std::string_view disconnectReasonTag(DisconnectReason _reason)
{
    switch (_reason)
    {
    case ClientQuit:
        return "remote_close";
    case DisconnectRequested:
    case UserReason:
        return "local_close";
    case PingTimeout:
    case IdleWaitTimeout:
        return "timeout";
    case BadProtocol:
    case IncompatibleProtocol:
    case NullIdentity:
    case UnexpectedIdentity:
    case NegotiateFailed:
        return "handshake_failed";
    case InBlacklistReason:
    case NotInWhitelistReason:
        return "blacklist";
    case TCPError:
    case UselessPeer:
    case TooManyPeers:
    case NoDisconnect:
    default:
        return "error";
    }
}

void logHandshakeFailed(bcos::LogLevel _level, NodeIPEndpoint const& _endpoint,
    std::string_view _reason, std::string_view _detail)
{
    if (bcos::moduleLogEnabled(bcos::LogModule::GATEWAY, _level))
    {
        BOOST_LOG_SEV(bcos::FileLoggerHandler, (boost::log::trivial::severity_level)(_level))
            << "[NETWORK][Host]" << LOG_DESC("HandshakeFailed") << LOG_KV("endpoint", _endpoint)
            << LOG_KV("reason", _reason) << LOG_KV("detail", _detail);
    }
}

}  // namespace bcos::gateway
