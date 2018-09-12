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
/** @file Network.h
 * @author Alex Leverington <nessence@gmail.com>
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 *
 * @author: yujiechen
 * @date: 2018-09-12
 * @modify: 1. rename NetworkPreferences to NetworkConfig
 *          2. remove traverNAT and confusing constructor of NetworkConfig
 *          3. modify the implementation of determinePublic to add exception catch
 */

#pragma once

#include "Common.h"
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <array>
#include <deque>
#include <memory>
#include <vector>
namespace ba = boost::asio;
namespace bi = ba::ip;

namespace dev
{
namespace p2p
{
struct NetworkConfig
{
    // Network Preferences with specific Listen IP
    NetworkConfig(std::string const& l, uint16_t lp, bool u = true)
      : publicIPAddress(), listenIPAddress(l), listenPort(lp)
    {}
    // Network Preferences with intended Public IP
    NetworkConfig(std::string const& _publicIP, std::string const& _listenAddr,
        uint16_t _listenPort, bool u = true)
      : publicIPAddress(_publicIP), listenIPAddress(_listenAddr), listenPort(_listenPort)
    {
        if (!publicIPAddress.empty() && !isPublicAddress(publicIPAddress))
            BOOST_THROW_EXCEPTION(InvalidPublicIPAddress());
    }
    /// Addressing
    std::string publicIPAddress;
    std::string listenIPAddress;
    uint16_t listenPort;
};

/**
 * @brief Network Class
 * Static network operations and interface(s).
 */
class Network
{
public:
    /// @returns public and private interface addresses
    static std::set<bi::address> getInterfaceAddresses();

    /// bind and listen on _listenPort
    static int tcp4Listen(bi::tcp::acceptor& _acceptor, NetworkConfig const& _netPrefs);

    /// Resolve "host:port" string as TCP endpoint. Returns unspecified endpoint on failure.
    static bi::tcp::endpoint resolveHost(std::string const& _host);
    static bi::tcp::endpoint determinePublic(NetworkConfig const& network_config);
};

}  // namespace p2p
}  // namespace dev
