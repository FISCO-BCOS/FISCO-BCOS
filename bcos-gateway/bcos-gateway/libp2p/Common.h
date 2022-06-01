/*
 * Common.h
 *
 *      Author: ancelmo
 */

#pragma once


#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/Exceptions.h>


namespace bcos
{
namespace gateway
{

#define P2PMSG_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][P2PMessage]"
#define P2PSESSION_LOG(LEVEL) \
    BCOS_LOG(LEVEL) << LOG_BADGE(m_moduleName) << "[P2PService][P2PSession]"
// #define P2PSESSION_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][P2PSession]"
#define SERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][Service]"

enum MessageExtFieldFlag
{
    Response = 0x0001,
};

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

class NetworkException : public std::exception
{
public:
    NetworkException(){};
    NetworkException(bcos::Error::Ptr _error)
      : m_errorCode(_error ? _error->errorCode() : 0),
        m_msg(_error ? _error->errorMessage() : "success"){};
    NetworkException(int _errorCode, const std::string& _msg)
      : m_errorCode(_errorCode), m_msg(_msg){};

    virtual int errorCode() const { return m_errorCode; };
    const char* what() const noexcept override { return m_msg.c_str(); };
    bool operator!() const { return m_errorCode == 0; }

    virtual Error::Ptr toError() { return std::make_shared<Error>(errorCode(), m_msg); }

private:
    int m_errorCode = 0;
    std::string m_msg;
};

}  // namespace gateway
}  // namespace bcos
