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
    unsigned port;
    std::string version;
};

/**
 * @brief IPv4,UDP/TCP endpoints.
 */
class NodeIPEndpoint
{
public:
    enum RLPAppend
    {
        StreamList,
        StreamInline
    };

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
    NodeIPEndpoint(RLP const& _r) { interpretRLP(_r); }
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
    void streamRLP(RLPStream& _s, RLPAppend _append = StreamList) const;
    void interpretRLP(RLP const& _r);
    std::string name() const
    {
        std::ostringstream os;
        os << address.to_string() << ":" << tcpPort << ":" << udpPort;
        return os.str();
    }
    bool isValid() { return (!address.to_string().empty()) && (tcpPort != 0); }
    // TODO: make private, give accessors and rename m_...
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
    std::map<std::string, std::string> notes;
};

using PeerSessionInfos = std::vector<PeerSessionInfo>;

struct NodeSpec
{
    NodeSpec() {}

    /// Accepts user-readable strings of the form (enode://pubkey@)host({:port,:tcpport.udpport})
    NodeSpec(std::string const& _user);

    NodeSpec(std::string const& _addr, uint16_t _port, int _udpPort = -1)
      : m_address(_addr), m_tcpPort(_port), m_udpPort(_udpPort == -1 ? _port : (uint16_t)_udpPort)
    {}

    NodeID id() const { return m_id; }

    NodeIPEndpoint nodeIPEndpoint() const;

    std::string enode() const;

private:
    std::string m_address;
    uint16_t m_tcpPort = 0;
    uint16_t m_udpPort = 0;
    NodeID m_id;
};
}  // namespace p2p
/// Simple stream output for a NodeIPEndpoint.
std::ostream& operator<<(std::ostream& _out, dev::p2p::NodeIPEndpoint const& _ep);
}  // namespace dev

/// std::hash for asio::adress
namespace std
{
template <>
struct hash<bi::address>
{
    size_t operator()(bi::address const& _a) const
    {
        if (_a.is_v4())
            return std::hash<unsigned long>()(_a.to_v4().to_ulong());
        if (_a.is_v6())
        {
            auto const& range = _a.to_v6().to_bytes();
            return boost::hash_range(range.begin(), range.end());
        }
        if (_a.is_unspecified())
            return static_cast<size_t>(0x3487194039229152ull);  // Chosen by fair dice roll,
                                                                // guaranteed to be random
        return std::hash<std::string>()(_a.to_string());
    }
};

}  // namespace std
