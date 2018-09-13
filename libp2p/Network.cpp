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
/** @file Network.cpp
 * @author Alex Leverington <nessence@gmail.com>
 * @author Gav Wood <i@gavwood.com>
 * @author Eric Lombrozo <elombrozo@gmail.com> (Windows version of getInterfaceAddresses())
 * @date 2014
 */

#include "Network.h"
#include "Common.h"
#include <ifaddrs.h>
#include <libdevcore/Assertions.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Exceptions.h>
#include <sys/types.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;
using namespace dev;
using namespace dev::p2p;
/// @returns public and private interface addresses
std::set<bi::address> Network::getInterfaceAddresses()
{
    std::set<bi::address> addresses;
    ifaddrs* ifaddr;
    /// creates a linked list of structures describing the network interfaces of the local system
    if (getifaddrs(&ifaddr) == -1)
        BOOST_THROW_EXCEPTION(NoNetworking());

    for (auto ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        /// filter local loop interface and interfaces down
        if (!ifa->ifa_addr || string(ifa->ifa_name) == "lo0" || !(ifa->ifa_flags & IFF_UP))
            continue;
        /// ipv4 addresses
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            in_addr addr = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
            boost::asio::ip::address_v4 address(
                boost::asio::detail::socket_ops::network_to_host_long(addr.s_addr));
            if (!isLocalHostAddress(address))
                addresses.insert(address);
        }
        /// get ipv6 addresses
        else if (ifa->ifa_addr->sa_family == AF_INET6)
        {
            sockaddr_in6* sockaddr = ((struct sockaddr_in6*)ifa->ifa_addr);
            in6_addr addr = sockaddr->sin6_addr;
            boost::asio::ip::address_v6::bytes_type bytes;
            memcpy(&bytes[0], addr.s6_addr, 16);
            boost::asio::ip::address_v6 address(bytes, sockaddr->sin6_scope_id);
            if (!isLocalHostAddress(address))
                addresses.insert(address);
        }
    }

    if (ifaddr != NULL)
        freeifaddrs(ifaddr);

    return addresses;
}

/// Try to bind and listen on _listenPort, else attempt net-allocated port.
int Network::tcp4Listen(bi::tcp::acceptor& _acceptor, NetworkConfig const& _netPrefs)
{
    bi::address listenIP;
    try
    {
        listenIP = bi::address::from_string(_netPrefs.listenIPAddress);
    }
    catch (...)
    {
        LOG(WARNING) << "Couldn't start accepting connections on host. Failed to accept socket on "
                     << listenIP << ":" << _netPrefs.listenPort << ".\n"
                     << boost::current_exception_diagnostic_information();
        return -1;
    }
    bi::tcp::endpoint endpoint(listenIP, _netPrefs.listenPort);
    try
    {
        bool reuse = true;
        _acceptor.open(endpoint.protocol());
        _acceptor.set_option(ba::socket_base::reuse_address(reuse));
        _acceptor.bind(endpoint);
        _acceptor.listen();
        return _acceptor.local_endpoint().port();
    }
    catch (...)
    {
        // bind failed
        LOG(WARNING) << "Couldn't start accepting connections on host. Failed to accept socket on "
                     << listenIP << ":" << _netPrefs.listenPort << ".\n"
                     << boost::current_exception_diagnostic_information();
        _acceptor.close();
        return -1;
        _acceptor.close();
        return -1;
    }
}

/// resolve host string with {ip:port} format to tcp endpoint
bi::tcp::endpoint Network::resolveHost(string const& _addr)
{
    /// singleton
    static boost::asio::io_service s_resolverIoService;
    vector<string> split;
    boost::split(split, _addr, boost::is_any_of(":"));
    unsigned port = dev::p2p::c_defaultIPPort;
    try
    {
        /// get port
        if (split.size() > 1)
            port = static_cast<unsigned>(stoi(split.at(1)));
    }
    catch (...)
    {
        LOG(WARNING) << "Obtain Port from " << _addr << " Failed!";
    }
    boost::system::error_code ec;
    /// get ip address
    bi::address address = bi::address::from_string(split[0], ec);
    bi::tcp::endpoint ep(bi::address(), port);
    if (!ec)
        ep.address(address);
    else
    {
        boost::system::error_code ec;
        /// resolve returns an iterator (host can resolve to multiple addresses)
        /// TODO(Problem): always stucks here
        bi::tcp::resolver r(s_resolverIoService);
        auto it = r.resolve({bi::tcp::v4(), split[0], toString(port)}, ec);
        if (ec)
        {
            LOG(WARNING) << "Error resolving host address..." << _addr << ":" << ec.message();
            return bi::tcp::endpoint();
        }
        else
            ep = *it;
    }
    return ep;
}

/// obtain public address from specified network config
/// case1: the listen ip is public address, then public address equals to listen address
/// case2: the listen ip is a private address && public address has been setted,
///        then the public address is the setted address
/// case3: the listen ip is a private address && the public address has not been setted,
///        obtain the interface address as the public address
bi::tcp::endpoint Network::determinePublic(NetworkConfig const& network_config)
{
    auto ifAddresses = Network::getInterfaceAddresses();
    /// listen address is obtained from networkConfig(listen ip address must be set)
    bi::address laddr;
    try
    {
        laddr = bi::address::from_string(network_config.listenIPAddress);
    }
    catch (...)
    {
        LOG(ERROR) << "MUST SET LISTEN IP Address!";
        return bi::tcp::endpoint();
    }
    auto lset = !laddr.is_unspecified();
    bi::address paddr;
    /// obtain public address
    try
    {
        paddr = bi::address::from_string(network_config.publicIPAddress);
    }
    catch (...)
    {
        LOG(WARNING) << "public address has not been setted, obtain from interfaces now";
    }
    auto pset = !paddr.is_unspecified();
    /// add paddr obtain function
    if (!pset)
    {
        for (auto address : ifAddresses)
        {
            if (address.is_v4())
            {
                paddr = address;
                break;
            }
        }
    }
    pset = !paddr.is_unspecified();

    bool listenIsPublic = lset && isPublicAddress(laddr);
    bool publicIsHost = !lset && pset && ifAddresses.count(paddr);

    bi::tcp::endpoint ep(bi::address(), network_config.listenPort);
    /// set listen address as public address
    if (listenIsPublic)
    {
        LOG(INFO) << "Listen address set to Public address:" << laddr;
        ep.address(laddr);
    }
    /// set address obtained from interfaces as public address
    else if (publicIsHost)
    {
        LOG(INFO) << "Public address set to Host configured address:" << paddr;
        ep.address(paddr);
    }
    else if (pset)
        ep.address(paddr);
    return ep;
}
