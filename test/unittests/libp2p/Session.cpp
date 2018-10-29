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
 * @brief: unit test for Session
 *
 * @file Session.cpp
 * @author: caryliao
 * @date 2018-10-22
 */
#include "../../../libnetwork/Session.h"

#include "FakeHost.h"
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include "../../../libnetwork/Host.h"
#include "../../../libnetwork/P2pFactory.h"

using namespace dev;
using namespace dev::p2p;
namespace dev
{
namespace test
{
class P2PMessageFactory : public P2PMessageFactory
{
public:
    virtual ~P2PMessageFactory() {}
    virtual P2PMessage::Ptr buildMessage() override { return std::make_shared<P2PMessage>(); }
};

class SessionFixure : public TestOutputHelperFixture
{
public:
    SessionFixure() { m_session = createSession("127.0.0.1", 1); }
    std::shared_ptr<Session> getSession() { return m_session; }
    std::shared_ptr<FakeSocket> getSocket() { return m_socket; }
    std::shared_ptr<Session> createSession(std::string ip, int i)
    {
        NodeIPEndpoint m_endpoint(bi::address::from_string(ip), m_listenPort + i, m_listenPort + i);
        KeyPair key_pair = KeyPair::create();
        m_peer = std::make_shared<Peer>(key_pair.pub(), m_endpoint);

        PeerSessionInfo m_info(
            {key_pair.pub(), m_endpoint.address.to_string(), chrono::steady_clock::duration(), 0});
        m_host = createFakeHost("2.0", "127.0.0.1", 30304);
        m_socket = std::make_shared<FakeSocket>(m_ioservice, m_endpoint);
        m_session = std::make_shared<Session>(m_host, m_socket, m_peer, m_info);
        m_host->setSessions(m_session);

        return m_session;
    }
    FakeHost* getHost() { return m_host; }
    std::shared_ptr<Peer> peer() const { return m_peer; }

protected:
    std::shared_ptr<Session> m_session;
    FakeHost* m_host;
    std::string m_clientVersion = "2.0";
    std::string m_listenIp = "127.0.0.1";
    uint16_t m_listenPort = 30304;
    std::shared_ptr<Peer> m_peer;
    std::shared_ptr<FakeSocket> m_socket;
    ba::io_service m_ioservice;
};

BOOST_FIXTURE_TEST_SUITE(SessionTest, SessionFixure)

BOOST_AUTO_TEST_CASE(testSessionConstruct)
{
    auto session = getSession();
    BOOST_CHECK(session->host() == getHost());
    BOOST_CHECK(session->peer() == peer());
    BOOST_CHECK(session->id() == session->peer()->id());
    BOOST_CHECK(session->nodeIPEndpoint() == getSocket()->nodeIPEndpoint());
    session->info();
    std::shared_ptr<P2PMessageFactory> p2PMessageFactory = std::make_shared<P2PMessageFactory>();
    session->setMessageFactory(p2PMessageFactory);
    BOOST_CHECK(session->messageFactory() == p2PMessageFactory);
    uint32_t topicSeq = uint32_t(0);
    session->setTopicSeq(topicSeq);
    BOOST_CHECK(session->topicSeq() == topicSeq);
    std::shared_ptr<std::vector<std::string>> topics = std::make_shared<std::vector<std::string>>();
    std::string topic = "Topic";
    topics->push_back(topic);
    session->setTopics(topics);
    BOOST_CHECK(session->topics()->size() == topics->size());
    session->setTopicsAndTopicSeq(session->id(), topics, topicSeq);
    session->setTopicsAndTopicSeq(session->id(), topics, topicSeq);
    BOOST_CHECK(session->peerTopicSeq(session->id()) == topicSeq);
    session->connectionTime();
    session->lastReceived();
}

BOOST_AUTO_TEST_CASE(testSessionConnect)
{
    auto session = getSession();
    BOOST_CHECK(session->isConnected() == true);
    if (session->isConnected())
    {
        session->disconnect(DisconnectReason::DisconnectRequested);
    }
}

BOOST_AUTO_TEST_CASE(testSessionCallback)
{
    auto session = getSession();
    ResponseCallback::Ptr responseCallback = std::make_shared<ResponseCallback>();
    uint32_t seq = uint32_t(0);
    session->addSeq2Callback(seq, responseCallback);
    BOOST_CHECK(session->getCallbackBySeq(seq) == responseCallback);
    session->eraseCallbackBySeq(seq);
    BOOST_CHECK(session->getCallbackBySeq(seq) == NULL);
}

BOOST_AUTO_TEST_CASE(testSessionSend)
{
    auto session = getSession();
    m_ioservice.run_one();
    std::shared_ptr<bytes> msgBuf = std::make_shared<bytes>();
    session->send(msgBuf);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
