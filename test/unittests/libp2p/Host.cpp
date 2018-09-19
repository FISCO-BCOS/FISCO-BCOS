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
 * @brief: unit test for Host
 *
 * @file Host.cpp
 * @author: yujiechen
 * @date 2018-09-11
 */
#include "FakeHost.h"
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::p2p;
namespace dev
{
namespace test
{
class HostFixture : public TestOutputHelperFixture
{
public:
    HostFixture()
    {
        m_host = createFakeHost(m_clientVersion, m_listenIp, m_listenPort);
        FakeRemoteEndpoint();
    }

    FakeHost* getHost() { return m_host; }
    std::string const& listenIp() const { return m_listenIp; }
    std::string const& clientVersion() const { return m_clientVersion; }
    uint16_t const& listenPort() const { return m_listenPort; }
    /// fake remote endpoint
    void FakeRemoteEndpoint()
    {
        bi::address addr = bi::address::from_string(m_remote_address);
        remote_endpoint.address(addr);
        remote_endpoint.port(m_remote_connect_port);
    }
    std::shared_ptr<SessionFace> FakeSession(std::string ip, int i)
    {
        unsigned const protocolVersion = 0;
        NodeIPEndpoint peer_endpoint(
            bi::address::from_string(ip), m_listenPort + i, m_listenPort + i);
        ;
        KeyPair key_pair = KeyPair::create();
        std::shared_ptr<Peer> peer = std::make_shared<Peer>(key_pair.pub(), peer_endpoint);
        PeerSessionInfo peer_info({key_pair.pub(), peer_endpoint.address.to_string(),
            chrono::steady_clock::duration(), 0});
        std::shared_ptr<SessionFace> session =
            std::make_shared<FakeSessionForHost>(getHost(), peer, peer_info);
        session->start();
        return session;
    }

protected:
    FakeHost* m_host;
    std::string m_listenIp = "127.0.0.1";
    std::string m_remote_address = "10.18.19.1";
    std::string m_clientVersion = "2.0";
    uint16_t m_listenPort = 30304;
    uint16_t m_remote_listen_port = 30305;
    uint16_t m_remote_connect_port = 10234;
    bi::tcp::endpoint remote_endpoint;
};

BOOST_FIXTURE_TEST_SUITE(HostTest, HostFixture)
/// test constructor
BOOST_AUTO_TEST_CASE(testHostConstruct)
{
    BOOST_CHECK(getHost()->clientVersion() == clientVersion());
    BOOST_CHECK(getHost()->networkConfig().listenIPAddress == listenIp());
    BOOST_CHECK(getHost()->networkConfig().listenPort == listenPort());
    BOOST_CHECK(getHost()->networkConfig().publicIPAddress.empty());
    BOOST_CHECK(getHost()->ioService() != nullptr);
    BOOST_CHECK(getHost()->keyPair().pub() == getHost()->id());
    BOOST_CHECK(toPublic(getHost()->sec()) == getHost()->id());
    BOOST_CHECK(getHost()->strand() != nullptr);
}

/// test runAcceptor
BOOST_AUTO_TEST_CASE(testRunAcceptor)
{
    getHost()->setAccepting(false);
    getHost()->setRun(true);
    /// callback runAcceptor
    boost::system::error_code boost_error = boost::asio::error::broken_pipe;
    /// std::cout<<"### test boost_error"<<std::endl;
    getHost()->m_loop = 0;
    getHost()->runAcceptor(boost_error);
    getHost()->setRun(false);
    /// std::cout<<"### test Run false"<<std::endl;
    getHost()->m_loop = 0;
    getHost()->runAcceptor();
    /// tcp client hasn't been set
    BOOST_CHECK(getHost()->tcpClient().port() == 0);
    /// set async_accept return true
    getHost()->setRun(true);
    ///--- test too many peers
    /// set sessions
    std::shared_ptr<SessionFace> session1 = FakeSession("127.0.0.1", 1);
    std::shared_ptr<SessionFace> session2 = FakeSession("127.0.0.1", 2);
    getHost()->setSessions(session1);
    getHost()->setSessions(session2);
    BOOST_CHECK(getHost()->peerCount() == 2);
    getHost()->setMaxPeerCount(1);
    /// std::cout<<"#### set limit max peer count"<<std::endl;
    boost_error = boost::asio::error::timed_out;
    getHost()->m_loop = 0;
    getHost()->runAcceptor(boost_error);  // return directly
    BOOST_CHECK(getHost()->tcpClient().port() == 0);

    /// --- test too many peers && boost error => return directly
    getHost()->m_loop = 0;
    getHost()->runAcceptor(boost_error);
    BOOST_CHECK(getHost()->tcpClient().port() == 0);
    /// --- test too many peers && no error => call runAcceptor
    getHost()->m_loop = 0;
    getHost()->runAcceptor(boost_error);
    BOOST_CHECK(getHost()->tcpClient().port() == 0);

    /// get m_tcpClient
    getHost()->setMaxPeerCount(100);
    getHost()->setNodeId(session1->id());
    getHost()->setVerifyResult(true);
    getHost()->setRun(true);
    /// std::cout<<"### test valid runAcceptor"<<std::endl;
    getHost()->m_loop = 0;
    getHost()->runAcceptor();
    BOOST_CHECK(getHost()->tcpClient().address().to_string() == "127.0.0.1");
    BOOST_CHECK(getHost()->tcpClient().port() == 30314);
}

// test startPeerSessions and doneWorking
BOOST_AUTO_TEST_CASE(testStartPeerSessionsAndDoneWorking)
{
    ba::io_service m_ioservice(2);
    NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30303);
    std::shared_ptr<FakeSocket> fake_socket = std::make_shared<FakeSocket>(m_ioservice, m_endpoint);
    /// test start to empty public
    getHost()->startPeerSession(NodeID(), fake_socket);
    BOOST_CHECK(fake_socket->m_close == true);
    fake_socket->m_close = false;
    /// test connect to node-self
    getHost()->startPeerSession(getHost()->id(), fake_socket);
    BOOST_CHECK(fake_socket->m_close == true);
    /// set sessions
    std::shared_ptr<SessionFace> session1 = FakeSession("127.0.0.1", 1);
    std::shared_ptr<SessionFace> session2 = FakeSession("127.0.0.1", 2);
    getHost()->startPeerSession(session1->id(), fake_socket);
    getHost()->startPeerSession(session2->id(), fake_socket);
    BOOST_CHECK(getHost()->sessions().size() == 2);
    BOOST_CHECK(getHost()->peerCount() == 2);
    /// set m_staticNodes
    std::map<NodeIPEndpoint, NodeID> m_staticNodes;
    m_staticNodes[NodeIPEndpoint(bi::address::from_string("127.0.0.1"), 30304, 30304)] =
        getHost()->id();
    m_staticNodes[NodeIPEndpoint(bi::address::from_string("127.0.0.1"), 30305, 30305)] =
        session1->id();
    m_staticNodes[NodeIPEndpoint(bi::address::from_string("127.0.0.1"), 30306, 30306)] =
        session2->id();
    NodeID session1_node_id = session1->id();
    getHost()->setStaticNodes(m_staticNodes);
    BOOST_CHECK(getHost()->staticNodes()->size() == 3);
    /// disconnect session1
    getHost()->disconnectByNodeId(session1->id());
    BOOST_CHECK(getHost()->peerCount() == 1);
    getHost()->keepAlivePeers();
    BOOST_CHECK(getHost()->sessions().size() == 1);
    /// test connect to non-connected peers
    getHost()->startPeerSession(session1_node_id, fake_socket);
    std::cout << "#### node id of session1:" << session1_node_id << std::endl;
    BOOST_CHECK(getHost()->sessions().size() == 2);
    BOOST_CHECK(getHost()->getPeers().size() == 2);
    BOOST_CHECK(getHost()->peerCount() == 2);
    /// test connect to already-existed-peers
    getHost()->startPeerSession(session2->id(), fake_socket);
    BOOST_CHECK(getHost()->sessions().size() == 2);
    BOOST_CHECK(getHost()->peerCount() == 2);

    /// test doneWorking
    getHost()->doWork();
    getHost()->doneWorking();
    BOOST_CHECK(getHost()->ioService()->stopped() == false);
    BOOST_CHECK(getHost()->getAcceptor().is_open() == false);
    BOOST_CHECK(getHost()->sessions().size() == 0);
    BOOST_CHECK(getHost()->peerCount() == 0);
    BOOST_CHECK(getHost()->getPeers().size() == 0);
}

/// test reconnectAllNodes && keepAlivePeers
BOOST_AUTO_TEST_CASE(testkeepAlivePeers)
{
    ba::io_service m_ioservice(2);
    NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30303);
    std::shared_ptr<FakeSocket> fake_socket = std::make_shared<FakeSocket>(m_ioservice, m_endpoint);
    /// reconnectnow
    getHost()->reconnectNow();
    /// set sessions
    std::shared_ptr<SessionFace> session1 = FakeSession("127.0.0.1", 1);
    std::shared_ptr<SessionFace> session2 = FakeSession("127.0.0.1", 2);
    getHost()->startPeerSession(session1->id(), fake_socket);
    getHost()->startPeerSession(session2->id(), fake_socket);
    NodeID session1_node_id = session1->id();
    /// set staticNodes
    std::map<NodeIPEndpoint, NodeID> m_staticNodes;
    m_staticNodes[NodeIPEndpoint(
        bi::address::from_string(listenIp()), listenPort(), listenPort())] = getHost()->id();
    m_staticNodes[NodeIPEndpoint(bi::address::from_string("127.0.0.1"), 30305, 30305)] =
        session1->id();
    m_staticNodes[NodeIPEndpoint(bi::address::from_string("127.0.0.1"), 30306, 30306)] =
        session2->id();
    getHost()->setStaticNodes(m_staticNodes);
    /// check set result
    BOOST_CHECK(getHost()->staticNodes()->size() == 3);
    BOOST_CHECK(getHost()->sessions().size() == 2);
    BOOST_CHECK(getHost()->getPeers().size() == 2);
    BOOST_CHECK(getHost()->havePeerSession(session1->id()));
    BOOST_CHECK(getHost()->havePeerSession(session2->id()));
    /// test only one session disconnected
    getHost()->disconnectByNodeId(session1_node_id);
    BOOST_CHECK(getHost()->sessions().size() == 1);
    getHost()->setRun(true);
    getHost()->setNodeId(session1->id());
    getHost()->setVerifyResult(true);
    /// test reconnectAllNodes, callback connect to start peer session(session1)
    getHost()->reconnectAllNodes();

    BOOST_CHECK(getHost()->sessions().size() == 2);
    /// test keepAlivePeers
    getHost()->keepAlivePeers();
    BOOST_CHECK(getHost()->sessions().size() == 2);
    BOOST_CHECK(getHost()->peerCount() == 2);
    BOOST_CHECK(getHost()->havePeerSession(session1_node_id));
    BOOST_CHECK(getHost()->havePeerSession(session2->id()));
    getHost()->disconnectByNodeId(session1->id());

    getHost()->keepAlivePeers();

    /// update m_sessions after disconnect when callback keepAlivePeers
    BOOST_CHECK(getHost()->sessions().size() == 1);
    BOOST_CHECK(getHost()->getPeers().size() == 1);
    BOOST_CHECK(getHost()->havePeerSession(session1_node_id) == false);
    getHost()->disconnectByNodeId(session2->id());

    getHost()->keepAlivePeers();

    BOOST_CHECK(getHost()->havePeerSession(session2->id()) == false);
    BOOST_CHECK(getHost()->sessions().size() == 0);

    /// to test disconnect
    getHost()->startPeerSession(session1->id(), fake_socket);
    getHost()->startPeerSession(session2->id(), fake_socket);
    BOOST_CHECK(getHost()->sessions().size() == 2);
    BOOST_CHECK(getHost()->peerCount() == 2);
    for (auto session : getHost()->sessions())
        session.second->disconnect(DisconnectReason::UselessPeer);
    getHost()->disableReconnect();
    getHost()->keepAlivePeers();

    getHost()->reconnectNow();
    getHost()->keepAlivePeers();
    BOOST_CHECK(getHost()->sessions().size() == 0);
    BOOST_CHECK(getHost()->getPeers().size() == 0);
    BOOST_CHECK(getHost()->peerCount() == 0);
}

/// test exception cases of handshake client and handshake server
BOOST_AUTO_TEST_CASE(testHandshakeClientAndServerException)
{
    ba::io_service m_ioservice(2);
    NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30303);
    std::shared_ptr<FakeSocket> fake_socket = std::make_shared<FakeSocket>(m_ioservice, m_endpoint);
    std::string node_id = toHex(KeyPair::create().pub());
    std::shared_ptr<std::string> p_node_id = std::make_shared<std::string>(node_id.c_str());
    boost::system::error_code err_code = boost::asio::error::timed_out;
    /// test handshake client exception(connect error)
    getHost()->handshakeClient(err_code, fake_socket, p_node_id, m_endpoint);
    BOOST_CHECK(fake_socket->isConnected() == false);
    /// test connect to node-self(handshake client)
    std::map<NodeIPEndpoint, NodeID> m_staticNodes;
    m_staticNodes[m_endpoint] = NodeID();
    getHost()->setStaticNodes(m_staticNodes);
    /// test modify node_id of m_staticNodes
    p_node_id = std::make_shared<std::string>(toHex(getHost()->id()).c_str());
    err_code = boost::system::error_code();
    fake_socket->m_close = false;
    getHost()->handshakeClient(err_code, fake_socket, p_node_id, m_endpoint);
    BOOST_CHECK(fake_socket->isConnected() == false);
    BOOST_CHECK((*(getHost()->staticNodes()))[m_endpoint] == getHost()->id());
    BOOST_CHECK(getHost()->hasPendingConn(m_endpoint.name()) == false);

    /// test handshakeserver exception: connect error
    fake_socket->m_close = false;
    err_code = boost::asio::error::timed_out;
    /// test error_code
    getHost()->handshakeServer(err_code, p_node_id, fake_socket);
    BOOST_CHECK(fake_socket->isConnected() == false);
    /// test connect to node-self
    fake_socket->m_close = false;
    err_code = boost::system::error_code();
    getHost()->handshakeServer(err_code, p_node_id, fake_socket);
    BOOST_CHECK(fake_socket->isConnected() == false);
}

/// test startedWorking
BOOST_AUTO_TEST_CASE(testRunAndStartedWorking)
{
    /// test run exception
    boost::system::error_code err = boost::asio::error::timed_out;
    getHost()->setRun(false);
    getHost()->run(err);
    BOOST_CHECK(getHost()->ioService()->stopped());
    /// test connect to node-self(handshake client)
    std::map<NodeIPEndpoint, NodeID> m_staticNodes;
    NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30303);
    m_staticNodes[m_endpoint] = KeyPair::create().pub();
    getHost()->setStaticNodes(m_staticNodes);
    getHost()->setNodeId(m_staticNodes[m_endpoint]);

    getHost()->m_loop = 0;
    getHost()->startedWorking();
    BOOST_CHECK(getHost()->haveNetwork() == true);
    /// test determinePublic
    BOOST_CHECK(getHost()->tcpPublic() == Network::determinePublic(getHost()->networkConfig()));
    BOOST_CHECK(getHost()->tcpPublic().port() == listenPort());
    BOOST_CHECK(getHost()->sessions().size() == 1);
    BOOST_CHECK(getHost()->getPeers().size() == 1);
}

/// test doWork
BOOST_AUTO_TEST_CASE(testDoWork)
{
    /// run ioservice
    getHost()->setRun(true);
    getHost()->ioService()->stop();
    BOOST_CHECK(getHost()->ioService()->stopped() == true);
    getHost()->doWork();
    BOOST_CHECK(getHost()->ioService()->stopped() == false);
    /// return directly
    boost::system::error_code ec;
    getHost()->setRun(false);
    getHost()->ioService()->run();
    getHost()->doWork();
    BOOST_CHECK(getHost()->ioService()->stopped() == false);
    /// reset io_service
    getHost()->ioService()->stop();
    BOOST_CHECK(getHost()->ioService()->stopped() == true);
    getHost()->doWork();
    BOOST_CHECK(getHost()->ioService()->stopped() == false);
}

/// test stop

BOOST_AUTO_TEST_CASE(testStop)
{
    getHost()->setRun(false);
    getHost()->stop();
    BOOST_CHECK(getHost()->haveNetwork() == false);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
