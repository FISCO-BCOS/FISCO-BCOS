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

#include <libdevcore/CommonIO.h>

using namespace std;
using namespace dev;
using namespace dev::network;

unsigned dev::network::c_defaultIPPort = 16789;
bool dev::network::NodeIPEndpoint::test_allowLocal = false;

bool isPublicAddress(std::string const& _addressToCheck)
{
    return _addressToCheck.empty() ? false :
                                     isPublicAddress(bi::address::from_string(_addressToCheck));
}

bool isPublicAddress(bi::address const& _addressToCheck)
{
    if (_addressToCheck.to_string() == "0.0.0.0")
        return false;
    return !_addressToCheck.is_unspecified();
}

// Helper function to determine if an address is localhost
bool isLocalHostAddress(bi::address const& _addressToCheck)
{
    // @todo: ivp6 link-local adresses (macos), ex: fe80::1%lo0
    static const set<bi::address> c_rejectAddresses = {{bi::address_v4::from_string("127.0.0.1")},
        {bi::address_v4::from_string("0.0.0.0")}, {bi::address_v6::from_string("::1")},
        {bi::address_v6::from_string("::")}};

    return find(c_rejectAddresses.begin(), c_rejectAddresses.end(), _addressToCheck) !=
           c_rejectAddresses.end();
}

bool isLocalHostAddress(std::string const& _addressToCheck)
{
    return _addressToCheck.empty() ? false :
                                     isLocalHostAddress(bi::address::from_string(_addressToCheck));
}

std::string reasonOf(DisconnectReason _r)
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

namespace dev
{
std::ostream& operator<<(std::ostream& _out, NodeIPEndpoint const& _ep)
{
    _out << _ep.address << _ep.udpPort << _ep.tcpPort;
    return _out;
}

}  // namespace dev
