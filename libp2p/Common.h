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
#include <chrono>

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

bool isLocalHostAddress(bi::address const& _addressToCheck);
bool isLocalHostAddress(std::string const& _addressToCheck);
bool isPublicAddress(bi::address const& _addressToCheck);
bool isPublicAddress(std::string const& _addressToCheck);

/// define Exceptions
DEV_SIMPLE_EXCEPTION(NetworkStartRequired);
DEV_SIMPLE_EXCEPTION(InvalidPublicIPAddress);
DEV_SIMPLE_EXCEPTION(InvalidHostIPAddress);

enum PacketType
{
    HelloPacket = 0,
    DisconnectPacket,
    PingPacket,
    PongPacket,
    GetPeersPacket,
    PeersPacket,
    UserPacket = 0x10
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
    NoDisconnect = 0xffff
};

///< The protocolID of synchronous Package, related to Service::sendMessageByNodeID
static int16_t g_synchronousPackageProtocolID = 32767;

///< P2PExceptionType and g_P2PExceptionMsg used in P2PException
enum P2PExceptionType
{
    Success = 0,
    ProtocolError,
    NetworkTimeout,
    P2PExceptionTypeCnt
};

static std::string g_P2PExceptionMsg[P2PExceptionTypeCnt] = {
    "Success", "ProtocolError", "NetworkTimeout"};

enum PacketDecodeStatus
{
    PACKET_ERROR = -1,
    PACKET_INCOMPLETE = 0
};

struct Options
{
    uint32_t timeout;  /// < The timeout value of async function, in milliseconds.
};

class Message : public std::enable_shared_from_this<Message>
{
public:
    typedef std::shared_ptr<Message> Ptr;

    const static size_t HEADER_LENGTH = 12;
    const static size_t MAX_LENGTH = 1024 * 1024;  ///< The maximum length of data is 1M.

    Message() { m_buffer = std::make_shared<bytes>(); }

    virtual ~Message() {}

    uint32_t length() { return m_length; }
    void setLength(uint32_t _length) { m_length = _length; }

    int16_t protocolID() { return m_protocolID; }
    void setProtocolID(int16_t _protocolID) { m_protocolID = _protocolID; }
    uint16_t packetType() { return m_packetType; }
    void setPacketType(uint16_t _packetType) { m_packetType = _packetType; }

    uint32_t seq() { return m_seq; }
    void setSeq(uint32_t _seq) { m_seq = _seq; }

    std::shared_ptr<bytes> buffer() { return m_buffer; }
    void setBuffer(std::shared_ptr<bytes> _buffer) { m_buffer = _buffer; }

    bool isRequestPacket() { return (m_protocolID > 0); }
    int16_t getResponceProtocolID()
    {
        if (isRequestPacket())
            return -m_protocolID;
        else
            return 0;
    }

    ///< encode and decode logic implemented in send() and doRead() of session.cpp
    void encode(bytes& buffer);

    /// < If the decoding is successful, the length of the decoded data is returned; otherwise, 0 is
    /// returned.
    ssize_t decode(const byte* buffer, size_t size);

    void Print(std::string const& str)
    {
        LOG(INFO) << str << ",Message(" << m_length << "," << m_protocolID << "," << m_packetType
                  << "," << m_seq << ","
                  << std::string((const char*)m_buffer->data(), m_buffer->size()) << ")";
    }

private:
    uint32_t m_length = 0;      ///< m_length = HEADER_LENGTH + length(m_buffer)
    int16_t m_protocolID = 0;   ///< message type, the first two bytes of information, when greater
                                ///< than 0 is the ID of the request package.
    uint16_t m_packetType = 0;  ///< message sub type, the second two bytes of information
    uint32_t m_seq = 0;         ///< the message identify
    std::shared_ptr<bytes> m_buffer;  ///< message data
};

class P2PException : public std::exception
{
public:
    P2PException(){};
    P2PException(int _errorCode, const std::string& _msg) : m_errorCode(_errorCode), m_msg(_msg){};

    virtual int errorCode() { return m_errorCode; };
    virtual const char* what() const _GLIBCXX_USE_NOEXCEPT override { return m_msg.c_str(); };
    bool operator!() const { return m_errorCode == 0; }

private:
    int m_errorCode = 0;
    std::string m_msg = "";
};

#define CallbackFunc std::function<void(P2PException, Message::Ptr)>
#define CallbackFuncWithSession \
    std::function<void(P2PException, std::shared_ptr<Session>, Message::Ptr)>

struct ResponseCallback : public std::enable_shared_from_this<ResponseCallback>
{
    typedef std::shared_ptr<ResponseCallback> Ptr;

    CallbackFunc callbackFunc;
    std::shared_ptr<boost::asio::deadline_timer> timeoutHandler;
};

struct SessionCallback : public std::enable_shared_from_this<SessionCallback>
{
public:
    typedef std::shared_ptr<SessionCallback> Ptr;

    SessionCallback() { mutex.lock(); }

    void onResponse(P2PException _error, Message::Ptr _message)
    {
        error = _error;
        response = _message;
        mutex.unlock();
    }

    P2PException error;
    Message::Ptr response;
    std::mutex mutex;
};

/// @returns the string form of the given disconnection reason.
std::string reasonOf(DisconnectReason _r);

class HostResolver
{
public:
    static boost::asio::ip::address query(std::string);
};
enum PeerSlotType
{
    Egress,
    Ingress
};
struct NodeInfo
{
    NodeInfo() = default;
    NodeInfo(
        NodeID const& _id, std::string const& _address, unsigned _port, std::string const& _version)
      : id(_id), address(_address), port(_port), version(_version)
    {}

    std::string enode() const
    {
        return "enode://" + id.hex() + "@" + address + ":" + toString(port);
    }

    NodeID id;
    std::string address;
    unsigned port = 0;
    std::string version;
};

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

class NodeSpec
{
public:
    NodeSpec() = default;
    /// Accepts user-readable strings of the form (enode://pubkey@)host({:port,:tcpport.udpport})
    NodeSpec(std::string const& _user);

    NodeSpec(NodeID const& node_id, std::string const& _addr, uint16_t _port, int _udpPort = -1)
      : m_id(node_id),
        m_address(_addr),
        m_tcpPort(_port),
        m_udpPort(_udpPort == -1 ? _port : (uint16_t)_udpPort)
    {}

    /// get interfaces
    NodeID id() const { return m_id; }
    uint16_t tcpPort() const { return m_tcpPort; }
    uint16_t udpPort() const { return m_udpPort; }
    std::string const& address() const { return m_address; }
    NodeIPEndpoint nodeIPEndpoint() const;
    std::string enode() const;

    /// set interfaces
    void setNodeId(NodeID const& _node) { m_id = _node; }
    void setAddress(std::string const& _address) { m_address = _address; }
    void setTcpPort(uint16_t const _port) { m_tcpPort = _port; }
    void setUdpPort(uint16_t const _port) { m_udpPort = _port; }

private:
    NodeID m_id;
    std::string m_address;
    uint16_t m_tcpPort = 0;
    uint16_t m_udpPort = 0;
};
}  // namespace p2p
/// Simple stream output for a NodeIPEndpoint.
std::ostream& operator<<(std::ostream& _out, dev::p2p::NodeIPEndpoint const& _ep);
}  // namespace dev
