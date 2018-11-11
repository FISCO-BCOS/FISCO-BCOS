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
#include <libnetwork/Session.h>

#include "FakeHost.h"
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <libnetwork/Host.h>
#include <libnetwork/P2pFactory.h>
#include <libnetwork/Session.h>

using namespace dev;
using namespace dev::p2p;

namespace dev
{
namespace test
{
class MockMessageFactory : public MessageFactory
{
public:
    virtual ~MockMessageFactory() {}
    virtual P2PMessage::Ptr buildMessage() override { return std::make_shared<P2PMessage>(); }
};

class MockASIO: public ASIOInterface {
    virtual void async_accept(bi::tcp::acceptor& tcp_acceptor, std::shared_ptr<SocketFace>& socket,
        boost::asio::io_service::strand& m_strand, Handler_Type handler,
        boost::system::error_code ec = boost::system::error_code()) override
    {
        tcp_acceptor.async_accept(socket->ref(), m_strand.wrap(handler));
    }

    /// default implementation of async_connect
    virtual void async_connect(std::shared_ptr<SocketFace>& socket,
        boost::asio::io_service::strand& m_strand, const bi::tcp::endpoint& peer_endpoint,
        Handler_Type handler, boost::system::error_code ec = boost::system::error_code()) override
    {
        socket->ref().async_connect(peer_endpoint, m_strand.wrap(handler));
    }

    /// default implementation of async_write
    virtual void async_write(std::shared_ptr<SocketFace> const& socket,
        boost::asio::mutable_buffers_1 buffers, ReadWriteHandler handler,
        std::size_t transferred_bytes = 0,
        boost::system::error_code ec = boost::system::error_code()) override
    {
        ba::async_write(socket->sslref(), buffers, handler);
    }

    /// default implementation of async_read
    virtual void async_read(std::shared_ptr<SocketFace> const& socket,
        boost::asio::io_service::strand& m_strand, boost::asio::mutable_buffers_1 buffers,
        ReadWriteHandler handler, std::size_t transferred_bytes = 0,
        boost::system::error_code ec = boost::system::error_code()) override
    {
        ba::async_read(socket->sslref(), buffers, m_strand.wrap(handler));
    }

    /// default implementation of async_read_some
    virtual void async_read_some(std::shared_ptr<SocketFace> const& socket,
        boost::asio::io_service::strand& m_strand, boost::asio::mutable_buffers_1 buffers,
        ReadWriteHandler handler, std::size_t transferred_bytes = 0,
        boost::system::error_code ec = boost::system::error_code()) override
    {
        socket->sslref().async_read_some(buffers, m_strand.wrap(handler));
    }

    /// default implementation of async_handshake
    virtual void async_handshake(std::shared_ptr<SocketFace> const& socket,
        boost::asio::io_service::strand& m_strand, ba::ssl::stream_base::handshake_type const& type,
        Handler_Type handler, boost::system::error_code ec = boost::system::error_code()) override
    {
        socket->sslref().async_handshake(type, m_strand.wrap(handler));
    }
    /// default implementation of async_wait
    virtual void asyncWait(boost::asio::deadline_timer* m_timer,
        boost::asio::io_service::strand& m_strand, Handler_Type handler,
        boost::system::error_code ec = boost::system::error_code()) override
    {
        if (m_timer)
            m_timer->async_wait(m_strand.wrap(handler));
    }
    /// default implementation of set_verify_callback
    virtual void set_verify_callback(
        std::shared_ptr<SocketFace> const& socket, VerifyCallback callback, bool verify_succ = true) override
    {
        socket->sslref().set_verify_callback(callback);
    }

    /// default implementation of m_strand.post
    virtual void strand_post(boost::asio::io_service::strand& m_strand, Base_Handler handler) override
    {
        m_strand.post(handler);
    }
};

class MockHost: public Host {
public:
    virtual NodeID id() const override { return Public(); }
    virtual uint16_t listenPort() const override { return 8888; }

    virtual void start(boost::system::error_code const& error) override {

    }
    virtual void stop() override {

    }

    virtual void asyncConnect(
        NodeIPEndpoint const& _nodeIPEndpoint,
        std::function<void(boost::system::error_code, std::shared_ptr<SessionFace>)> callback
    ) override {

    }

    virtual bool haveNetwork() const override { return run; }

    virtual std::shared_ptr<ASIOInterface>  asioInterface() override { return asio; }

    bool run = false;
    std::shared_ptr<ASIOInterface> asio;
};

class MockSocket: public Socket {
public:
    virtual bool isConnected() const { return connected; }

    bool connected = false;
};

class SessionFixure
{
public:
    SessionFixure() {
        host = std::make_shared<MockHost>();
        host->asio = std::make_shared<MockASIO>();
        messageFactory = std::make_shared<MockMessageFactory>();
        socket = std::make_shared<MockSocket>();
    }

    Session::Ptr newSession(std::shared_ptr<Host> host, std::shared_ptr<MessageFactory> messageFactory, std::shared_ptr<SocketFace> socket) {
        auto session = std::make_shared<Session>();
        session->setHost(host);
        session->setMessageFactory(messageFactory);
        session->setSocket(socket);

        return session;
    }

    std::shared_ptr<MockHost> host;
    std::shared_ptr<MockMessageFactory> messageFactory;
    std::shared_ptr<MockSocket> socket;
};

BOOST_FIXTURE_TEST_SUITE(SessionTest, SessionFixure)

BOOST_AUTO_TEST_CASE(start) {
    auto session = newSession(std::shared_ptr<Host>(), messageFactory, socket);
    session->start();
    BOOST_CHECK(session->actived() == false );

    session = newSession(host, messageFactory, socket);
    host->run = false;
    session->start();
    BOOST_CHECK(session->actived() == false);

    session = newSession(host, messageFactory, socket);
    host->run = true;
    session->start();
    BOOST_CHECK(session->actived() == true);
}

BOOST_AUTO_TEST_CASE(isConnected) {
    auto session = newSession(std::shared_ptr<Host>(), messageFactory, socket);
    socket->connected = false;
    BOOST_CHECK(session->isConnected() = false);

    socket->connected = true;
    BOOST_CHECK(session->isConnected() = true);
}

BOOST_AUTO_TEST_CASE(asyncSendMessage) {
    auto session = newSession(std::shared_ptr<Host>(), messageFactory, socket);
    P2PMessage::Ptr message = std::make_shared<P2PMessage>();
    message->setSeq(1);

    dev::p2p::Options options;
    options.timeout = 0;
    session->asyncSendMessage(message, options, [] (dev::p2p::NetworkException e, Message::Ptr response) {
        BOOST_CHECK(e.errorCode() == -1);
    });

    host->run = true;
    session->start();
    session->asyncSendMessage(message, options, [] (dev::p2p::NetworkException e, Message::Ptr response) {
        BOOST_CHECK(e.errorCode() == 0);
        BOOST_CHECK(response->seq() == 1);
    });
}

BOOST_AUTO_TEST_CASE(disconnect) {
    auto session = newSession(host, messageFactory, socket);
    host->run = true;
    session->start();

    //session->disconnect();
}

#if 0
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
#endif

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
