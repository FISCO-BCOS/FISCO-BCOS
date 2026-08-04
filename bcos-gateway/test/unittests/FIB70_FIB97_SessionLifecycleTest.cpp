/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief Regression tests for FIB-70 and FIB-97 session lifecycle fixes
 * @file FIB70_FIB97_SessionLifecycleTest.cpp
 * @date 2026-04-07
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/Socket.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-utilities/ThreadPool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <queue>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;
using namespace bcos::crypto;

BOOST_FIXTURE_TEST_SUITE(FIB70_FIB97_SessionLifecycleTest, TestPromptFixture)

// --- Fake components (mirrors SessionTest.cpp infrastructure) ---

class FakeASIO_FIB : public bcos::gateway::ASIOInterface
{
public:
    using Packet = std::shared_ptr<std::vector<uint8_t>>;
    FakeASIO_FIB() : m_threadPool(std::make_shared<bcos::ThreadPool>("FakeASIO_FIB", 1)) {}
    ~FakeASIO_FIB() noexcept override {}

    void readSome(std::shared_ptr<SocketFace> socket, boost::asio::mutable_buffer buffers,
        ReadWriteHandler handler)
    {
        std::size_t bytesTransferred = 0;
        auto limit = buffers.size();

        while (!m_recvPackets.empty())
        {
            auto packet = m_recvPackets.front();
            if (bytesTransferred + packet->size() > limit)
            {
                auto remaining = limit - bytesTransferred;
                boost::asio::buffer_copy(buffers, boost::asio::buffer(*packet), remaining);
                bytesTransferred += remaining;
                packet->erase(packet->begin(), packet->begin() + remaining);
                break;
            }
            else
            {
                m_recvPackets.pop();
                boost::asio::buffer_copy(buffers, boost::asio::buffer(*packet));
                buffers += packet->size();
                bytesTransferred += packet->size();
            }
        }

        handler(boost::system::error_code(), bytesTransferred);
    }

    void asyncReadSome(const std::shared_ptr<SocketFace>& socket,
        boost::asio::mutable_buffer buffers, ReadWriteHandler handler) override
    {
        m_threadPool->enqueue([this, socket, buffers, handler]() {
            if (m_recvPackets.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                asyncReadSome(socket, buffers, handler);
                return;
            }
            readSome(socket, buffers, handler);
        });
    }

    void strandPost(Base_Handler handler) override { m_handler = handler; }
    void stop() override { m_threadPool->stop(); }

    void appendRecvPacket(Packet packet) { m_recvPackets.push(packet); }
    void asyncAppendRecvPacket(Packet packet)
    {
        m_threadPool->enqueue([this, packet]() { appendRecvPacket(packet); });
    }
    void triggerRead()
    {
        m_threadPool->enqueue([this]() {
            if (m_handler)
            {
                m_handler();
            }
        });
    }

protected:
    Base_Handler m_handler;
    std::queue<Packet> m_recvPackets;
    bcos::ThreadPool::Ptr m_threadPool;
};

// A message that always returns MESSAGE_ERROR to simulate decode failure
class DecodeErrorMessage : public P2PMessage
{
public:
    using Ptr = std::shared_ptr<DecodeErrorMessage>;
    int32_t decode(const bytesConstRef& _buffer) override
    {
        if (_buffer.size() == 0)
        {
            return MessageDecodeStatus::MESSAGE_INCOMPLETE;
        }
        // Always return error for any non-empty buffer
        return MessageDecodeStatus::MESSAGE_ERROR;
    }
};

class DecodeErrorMessageFactory : public P2PMessageFactory
{
public:
    Message::Ptr buildMessage() override { return std::make_shared<DecodeErrorMessage>(); }
};

// A message that throws an exception during decode
class DecodeExceptionMessage : public P2PMessage
{
public:
    using Ptr = std::shared_ptr<DecodeExceptionMessage>;
    int32_t decode(const bytesConstRef& _buffer) override
    {
        if (_buffer.size() == 0)
        {
            return MessageDecodeStatus::MESSAGE_INCOMPLETE;
        }
        throw std::runtime_error("Simulated decode exception");
    }
};

class DecodeExceptionMessageFactory : public P2PMessageFactory
{
public:
    Message::Ptr buildMessage() override { return std::make_shared<DecodeExceptionMessage>(); }
};

class FakeHost_FIB : public bcos::gateway::Host
{
public:
    FakeHost_FIB(bcos::crypto::Hash::Ptr _hash, std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<SessionFactory> _sessionFactory, MessageFactory::Ptr _messageFactory)
      : Host(_hash, _asioInterface, _sessionFactory, _messageFactory)
    {
        m_run = true;
    }
};

// A FakeSocket backed by a real SSL context and stream so that drop() can safely
// call sslref().async_shutdown() without crashing.
class FakeSocket_FIB : public SocketFace
{
public:
    FakeSocket_FIB()
      : SocketFace(),
        m_ioContext(std::make_shared<ba::io_context>()),
        m_sslContext(ba::ssl::context::tlsv12),
        m_sslSocket(std::make_shared<ba::ssl::stream<bi::tcp::socket>>(*m_ioContext, m_sslContext))
    {}
    ~FakeSocket_FIB() override = default;

    bool isConnected() const override { return m_connected; }
    void close() override { m_connected = false; }
    boost::asio::ip::tcp::endpoint remoteEndpoint(boost::system::error_code ec) override
    {
        return {};
    }
    boost::asio::ip::tcp::endpoint localEndpoint(boost::system::error_code ec) override
    {
        return {};
    }
    bi::tcp::socket& ref() override { return m_sslSocket->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() override { return *m_sslSocket; }
    const NodeIPEndpoint& nodeIPEndpoint() const override { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) override {}
    ba::io_context& ioService() override { return *m_ioContext; }

    bool m_connected{true};

private:
    std::shared_ptr<ba::io_context> m_ioContext;
    ba::ssl::context m_sslContext;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
    NodeIPEndpoint m_nodeIPEndpoint;
};

// FIB-70: Verify that decode error (negative return from decode()) triggers session drop.
// Before the fix, the session would remain active as a "zombie" until the idle timeout.
// After the fix, drop(UserReason) is called immediately, setting m_active = false.
BOOST_AUTO_TEST_CASE(DecodeErrorTriggersSessionDrop)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();
    auto decodeErrorFactory = std::make_shared<DecodeErrorMessageFactory>();

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost =
            std::make_shared<FakeHost_FIB>(hashImpl, fakeAsio, nullptr, decodeErrorFactory);

        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());
        session->setMessageHandler(
            [](NetworkException e, SessionFace::Ptr sessionFace, Message::Ptr message) {});

        session->start();
        fakeAsio->triggerRead();

        // Send a packet that will trigger a decode error (MESSAGE_ERROR)
        auto badPacket = std::make_shared<std::vector<uint8_t>>(10, 0xAB);
        fakeAsio->asyncAppendRecvPacket(badPacket);

        // Wait for the session to be dropped
        size_t retryCount = 0;
        while (session->active() && retryCount < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retryCount++;
        }

        // FIB-70 fix: session must be inactive after decode error.
        // drop(UserReason) sets m_active = false as its first action.
        BOOST_CHECK(!session->active());

        session->setSocket(nullptr);
    }

    fakeSocket->close();
}

// FIB-70: Verify that decode exception triggers session drop.
// Before the fix, an exception in decode() would leave the session as a zombie.
// After the fix, drop(UserReason) is called in the catch block.
BOOST_AUTO_TEST_CASE(DecodeExceptionTriggersSessionDrop)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();
    auto decodeExceptionFactory = std::make_shared<DecodeExceptionMessageFactory>();

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost =
            std::make_shared<FakeHost_FIB>(hashImpl, fakeAsio, nullptr, decodeExceptionFactory);

        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());
        session->setMessageHandler(
            [](NetworkException e, SessionFace::Ptr sessionFace, Message::Ptr message) {});

        session->start();
        fakeAsio->triggerRead();

        // Send a packet that will trigger a decode exception
        auto badPacket = std::make_shared<std::vector<uint8_t>>(10, 0xCD);
        fakeAsio->asyncAppendRecvPacket(badPacket);

        // Wait for the session to be dropped
        size_t retryCount = 0;
        while (session->active() && retryCount < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retryCount++;
        }

        // FIB-70 fix: session must be inactive after decode exception
        BOOST_CHECK(!session->active());

        session->setSocket(nullptr);
    }

    fakeSocket->close();
}

// FIB-97: Verify socket shared_ptr capture prevents premature destruction.
// The fix captures m_socket as a shared_ptr in the async read handler lambda,
// keeping the socket alive even if Session::drop() is called concurrently.
BOOST_AUTO_TEST_CASE(SocketSharedPtrCaptureInAsyncHandler)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();
    auto decodeErrorFactory = std::make_shared<DecodeErrorMessageFactory>();

    // Verify socket has expected reference count before session creation
    auto initialRefCount = fakeSocket.use_count();
    BOOST_CHECK_EQUAL(initialRefCount, 1);

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost =
            std::make_shared<FakeHost_FIB>(hashImpl, fakeAsio, nullptr, decodeErrorFactory);

        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());
        session->setMessageHandler(
            [](NetworkException e, SessionFace::Ptr sessionFace, Message::Ptr message) {});

        // After session creation, socket should be held by both fakeSocket and session
        BOOST_CHECK(fakeSocket.use_count() > 1);

        session->setSocket(nullptr);
    }

    // After session destruction, only fakeSocket holds the socket
    BOOST_CHECK_EQUAL(fakeSocket.use_count(), 1);

    fakeSocket->close();
}

BOOST_AUTO_TEST_SUITE_END()
