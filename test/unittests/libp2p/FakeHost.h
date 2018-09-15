/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief : main Fakes of p2p module
 *
 * @file FakeP2p.h
 * @author: yujiechen
 * @date 2018-09-10
 */
#pragma once
#include <libp2p/Host.h>
using namespace dev;
using namespace dev::p2p;
namespace dev
{
namespace test
{
class AsioTest : public AsioInterface
{
public:
    virtual void async_accept(
        bi::tcp::acceptor& tcp_acceptor, bi::tcp::socket& socket_ref, Handler_Type handler)
    {
        std::cout << "#########test module" << std::endl;
    }
};
/// Fakes of Host
class FakeHost : public Host
{
public:
    FakeHost(std::string const& _clientVersion, KeyPair const& _alias, NetworkConfig const& _n,
        std::shared_ptr<AsioInterface>& m_asioInterface)
      : Host(_clientVersion, _alias, _n, m_asioInterface)
    {}
    bi::tcp::endpoint tcpClient() { return m_tcpClient; }
    bi::tcp::endpoint tcpPublic() { return m_tcpPublic; }
    virtual void startedWorking()
    {
        m_ioService.run();
        Host::startedWorking();
    }
    virtual void runAcceptor() { Host::runAcceptor(); }
    void PostIoService() { m_ioService.run(); }
    bool accepting() { return m_accepting; }
    ~FakeHost() {}
};

/// class for construct Node
class NodeFixture
{
public:
    static Node& getNode(std::string const& ip_addr, uint16_t const& _udp, uint16_t const& _tcp)
    {
        KeyPair key_pair = KeyPair::create();
        NodeIPEndpoint m_endpoint(bi::address::from_string(ip_addr), _udp, _tcp);
        static Node m_node(key_pair.pub(), m_endpoint);
        return m_node;
    }
};
/// create Host
static std::shared_ptr<FakeHost> createFakeHost(
    std::string const& client_version, std::string const& listenIp, uint16_t const& listenPort)
{
    KeyPair key_pair = KeyPair::create();
    NetworkConfig network_config(listenIp, listenPort);
    std::shared_ptr<AsioInterface> m_asioInterface = std::make_shared<AsioTest>();
    shared_ptr<FakeHost> m_host =
        std::make_shared<FakeHost>(client_version, key_pair, network_config, m_asioInterface);
    return m_host;
}
}  // namespace test
}  // namespace dev