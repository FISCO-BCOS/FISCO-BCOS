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
/** @file Common.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "Common.h"
#include "Network.h"
#include <libdevcore/CommonIO.h>

using namespace std;
using namespace dev;
using namespace dev::p2p;
unsigned dev::p2p::c_defaultIPPort = 16789;
bool dev::p2p::NodeIPEndpoint::test_allowLocal = false;

bool p2p::isPublicAddress(std::string const& _addressToCheck)
{
    return _addressToCheck.empty() ? false :
                                     isPublicAddress(bi::address::from_string(_addressToCheck));
}

bool p2p::isPublicAddress(bi::address const& _addressToCheck)
{
    if (_addressToCheck.to_string() == "0.0.0.0")
        return false;
    return !_addressToCheck.is_unspecified();
}

// Helper function to determine if an address is localhost
bool p2p::isLocalHostAddress(bi::address const& _addressToCheck)
{
    // @todo: ivp6 link-local adresses (macos), ex: fe80::1%lo0
    static const set<bi::address> c_rejectAddresses = {{bi::address_v4::from_string("127.0.0.1")},
        {bi::address_v4::from_string("0.0.0.0")}, {bi::address_v6::from_string("::1")},
        {bi::address_v6::from_string("::")}};

    return find(c_rejectAddresses.begin(), c_rejectAddresses.end(), _addressToCheck) !=
           c_rejectAddresses.end();
}

bool p2p::isLocalHostAddress(std::string const& _addressToCheck)
{
    return _addressToCheck.empty() ? false :
                                     isLocalHostAddress(bi::address::from_string(_addressToCheck));
}

std::string p2p::reasonOf(DisconnectReason _r)
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

void Message::encode(bytes& buffer)
{
    buffer.clear();  ///< It is not allowed to be assembled outside.
    m_length = HEADER_LENGTH + m_buffer->size();

    uint32_t length = htonl(m_length);
    int16_t protocolID = htons(m_protocolID);
    uint16_t packetType = htons(m_packetType);
    uint32_t seq = htonl(m_seq);

    buffer.insert(buffer.end(), (byte*)&length, (byte*)&length + sizeof(length));
    buffer.insert(buffer.end(), (byte*)&protocolID, (byte*)&protocolID + sizeof(protocolID));
    buffer.insert(buffer.end(), (byte*)&packetType, (byte*)&packetType + sizeof(packetType));
    buffer.insert(buffer.end(), (byte*)&seq, (byte*)&seq + sizeof(seq));
    buffer.insert(buffer.end(), m_buffer->begin(), m_buffer->end());
}

ssize_t Message::decode(const byte* buffer, size_t size)
{
    if (size < HEADER_LENGTH)
    {
        return PACKET_INCOMPLETE;
    }

    m_length = ntohl(*((uint32_t*)&buffer[0]));

    if (m_length > MAX_LENGTH)
    {
        return PACKET_ERROR;
    }

    if (size < m_length)
    {
        return PACKET_INCOMPLETE;
    }

    m_protocolID = ntohs(*((int16_t*)&buffer[4]));
    m_packetType = ntohs(*((uint16_t*)&buffer[6]));
    m_seq = ntohl(*((uint32_t*)&buffer[8]));
    ///< TODO: assign to std::move
    m_buffer->assign(&buffer[HEADER_LENGTH], &buffer[HEADER_LENGTH] + m_length - HEADER_LENGTH);

    return m_length;
}

NodeSpec::NodeSpec(string const& _user)
{
    m_address = _user;
    if (m_address.substr(0, 8) == "enode://" && m_address.find('@') == 136)
    {
        m_id = p2p::NodeID(m_address.substr(8, 128));
        m_address = m_address.substr(137);
    }
    size_t colon = m_address.find_first_of(":");
    if (colon != string::npos)
    {
        string ports = m_address.substr(colon + 1);
        m_address = m_address.substr(0, colon);
        size_t p2 = ports.find_first_of(".");
        if (p2 != string::npos)
        {
            m_udpPort = stoi(ports.substr(p2 + 1));
            m_tcpPort = stoi(ports.substr(0, p2));
        }
        else
            m_tcpPort = m_udpPort = boost::lexical_cast<int>(ports);
    }
}

NodeIPEndpoint NodeSpec::nodeIPEndpoint() const
{
    return NodeIPEndpoint(p2p::Network::resolveHost(m_address).address(), m_udpPort, m_tcpPort);
}

std::string NodeSpec::enode() const
{
    string ret = m_address;

    if (m_tcpPort)
        if (m_udpPort && m_tcpPort != m_udpPort)
            ret += ":" + dev::toString(m_tcpPort) + "." + dev::toString(m_udpPort);
        else
            ret += ":" + dev::toString(m_tcpPort);
    else if (m_udpPort)
        ret += ":" + dev::toString(m_udpPort);

    if (m_id)
        return "enode://" + m_id.hex() + "@" + ret;
    return ret;
}
namespace dev
{
std::ostream& operator<<(std::ostream& _out, dev::p2p::NodeIPEndpoint const& _ep)
{
    _out << _ep.address << _ep.udpPort << _ep.tcpPort;
    return _out;
}

}  // namespace dev


boost::asio::ip::address HostResolver::query(std::string host)
{
    ba::ip::address result;
    try
    {
        ba::io_service ioService;
        ba::ip::tcp::resolver resolver(ioService);
        ba::ip::tcp::resolver::query query(host, "");

        for (ba::ip::tcp::resolver::iterator i = resolver.resolve(query);
             i != ba::ip::tcp::resolver::iterator(); ++i)
        {
            ba::ip::tcp::endpoint end = *i;
            if (result.to_string().empty() || result.to_string() == "0.0.0.0")
                result = end.address();
            LOG(INFO) << "HostResolver::query " << host << ":" << end.address().to_string();
        }
    }
    catch (std::exception const& _e)
    {
        LOG(WARNING) << "Exception in HostResolver::query " << _e.what();
    }
    if (result.to_string().empty() || result.to_string() == "0.0.0.0")
        LOG(WARNING) << "HostResolver::query " << host << " Is Emtpy! ";

    return result;
}
