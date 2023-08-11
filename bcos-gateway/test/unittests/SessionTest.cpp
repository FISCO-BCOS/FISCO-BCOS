/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @brief test for gateway
 * @file SessionTest.cpp
 * @author: octopus
 * @date 2023-02-23
 */

#include <bcos-gateway/libnetwork/ASIOInterface.h>
#include <bcos-gateway/libnetwork/Host.h>
#include <bcos-gateway/libnetwork/Session.h>
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-utilities/ThreadPool.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(SessionTest, TestPromptFixture)


class FakeASIO : public bcos::gateway::ASIOInterface
{
public:
    using Packet = std::shared_ptr<std::vector<uint8_t>>;
    FakeASIO() : m_threadPool(std::make_shared<bcos::ThreadPool>("FakeASIO", 1)){};
    virtual ~FakeASIO() noexcept override{};

    void readSome(std::shared_ptr<SocketFace> socket, boost::asio::mutable_buffers_1 buffers,
        ReadWriteHandler handler)
    {
        std::size_t bytesTransferred = 0;
        auto limit = buffers.size();

        while (!m_recvPackets.empty())
        {
            auto packet = m_recvPackets.front();
            if (bytesTransferred + packet->size() > limit)
            {
                limit = limit - bytesTransferred;
                boost::asio::buffer_copy(buffers, boost::asio::buffer(*packet), limit);
                bytesTransferred += limit;
                packet->erase(packet->begin(), packet->begin() + limit);
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

    void asyncReadSome(std::shared_ptr<SocketFace> socket, boost::asio::mutable_buffers_1 buffers,
        ReadWriteHandler handler) override
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

public:  // for testing
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

class FakeP2PMessage : public P2PMessage
{
public:
    using Ptr = std::shared_ptr<FakeP2PMessage>;
    int32_t decode(const bytesConstRef& _buffer) override
    {
        if (_buffer.size() == 0)
        {
            return MessageDecodeStatus::MESSAGE_INCOMPLETE;
        }

        uint8_t length = _buffer[0];
        m_length = length;
        if (_buffer.size() < length)
        {
            return MessageDecodeStatus::MESSAGE_INCOMPLETE;
        }

        // check packet is right
        for (uint8_t i = 1; i < length; ++i)
        {
            if (_buffer[i] != uint8_t(0xff))
            {
                BOOST_CHECK(false);
                return MessageDecodeStatus::MESSAGE_ERROR;
            }
        }

        m_payload->assign(_buffer.begin(), _buffer.begin() + length);
        return length;
    }
};

class FakeMessageFactory : public P2PMessageFactory
{
public:
    Message::Ptr buildMessage() override
    {
        auto message = std::make_shared<FakeP2PMessage>();
        return message;
    }
};

class FakeMessagesBuilder
{
public:
    using Packet = FakeASIO::Packet;
    FakeMessagesBuilder(std::size_t packetNum)
    {
        for (std::size_t i = 0; i < packetNum; ++i)
        {
            uint8_t randPacketSize = rand() % 0xfe + 1;  // 1 ~ 254
            Packet packet = buildPacket(randPacketSize);
            m_sendBuffer.insert(m_sendBuffer.end(), packet->begin(), packet->end());
        }

        // cut sendBuffer randomly and generate sendPacket
        std::size_t offset = 0;
        while (offset < m_sendBuffer.size())
        {
            uint8_t randPacketSize = rand() % 0x7f + 1;  // 1 ~ 127
            std::size_t packetSize =
                std::min(std::size_t(randPacketSize), m_sendBuffer.size() - offset);
            Packet packet = std::make_shared<std::vector<uint8_t>>(
                m_sendBuffer.begin() + offset, m_sendBuffer.begin() + offset + packetSize);
            m_sendPackets.push(packet);
            offset += packetSize;
        }

        std::cout << "Fake message build with : " << m_sendBuffer.size() << " bytes and "
                  << packetNum << " packets" << std::endl;
    }

    Packet nextPacket()
    {
        if (m_sendPackets.empty())
        {
            return nullptr;
        }
        auto packet = m_sendPackets.front();
        m_sendPackets.pop();
        return packet;
    }

    bool isSameSendBuffer(const bytesConstRef& _buffer)
    {
        if (_buffer.size() != m_sendBuffer.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < _buffer.size(); ++i)
        {
            if (_buffer[i] != m_sendBuffer[i])
            {
                return false;
            }
        }
        return true;
    }

    size_t sendBufferSize() { return m_sendBuffer.size(); }

private:
    Packet buildPacket(uint8_t size)
    {
        // [size][0xff][0xff]...[0xff]
        // 1B    size-1B
        auto packet = std::make_shared<std::vector<uint8_t>>(size);
        (*packet)[0] = size;
        for (uint8_t i = 1; i < size; ++i)
        {
            (*packet)[i] = uint8_t(0xff);
        }
        return packet;
    }

private:
    std::vector<uint8_t> m_sendBuffer;
    std::queue<Packet> m_sendPackets;
};

class FakeHost : public bcos::gateway::Host
{
public:
    FakeHost(std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<SessionFactory> _sessionFactory, MessageFactory::Ptr _messageFactory)
      : Host(_asioInterface, _sessionFactory, _messageFactory)
    {
        m_run = true;
    }
};

class FakeSocket : public SocketFace
{
public:
    FakeSocket() : SocketFace(){};
    ~FakeSocket() override = default;
    bool isConnected() const override { return true; }
    void close() override {}
    boost::asio::ip::tcp::endpoint remoteEndpoint(boost::system::error_code ec) override
    {
        return boost::asio::ip::tcp::endpoint();
    }
    boost::asio::ip::tcp::endpoint localEndpoint(boost::system::error_code ec) override
    {
        return boost::asio::ip::tcp::endpoint();
    }
    bi::tcp::socket& ref() override { return m_sslSocket->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() override { return *m_sslSocket; }
    const NodeIPEndpoint& nodeIPEndpoint() const override { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) override {}
    std::shared_ptr<ba::io_context> ioService() override
    {
        return std::shared_ptr<ba::io_context>();
    }

private:
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
    NodeIPEndpoint m_nodeIPEndpoint;
};

BOOST_AUTO_TEST_CASE(fakeClassTest)
{
    auto totalPacketNum = 50;

    FakeMessagesBuilder messageBuilder(totalPacketNum);
    std::vector<uint8_t> recvBuffer;
    auto asio = std::make_shared<FakeASIO>();

    while (auto packet = messageBuilder.nextPacket())
    {
        std::vector<uint8_t> readBuffer(10240);
        asio->appendRecvPacket(packet);
        asio->readSome(nullptr, boost::asio::buffer(readBuffer),
            [&recvBuffer, &readBuffer](
                const boost::system::error_code& ec, std::size_t bytesTransferred) {
                recvBuffer.insert(
                    recvBuffer.end(), readBuffer.begin(), readBuffer.begin() + bytesTransferred);
                std::cout << "receive " << bytesTransferred << " bytes" << std::endl;
            });
    }

    BOOST_CHECK(messageBuilder.isSameSendBuffer(ref(recvBuffer)));
}

BOOST_AUTO_TEST_CASE(doReadTest)
{
    auto totalPacketNum = 500;
    FakeMessagesBuilder messageBuilder(totalPacketNum);
    auto fakeAsio = std::make_shared<FakeASIO>();
    auto fakeMessageFactory = std::make_shared<FakeMessageFactory>();
    auto fakeHost = std::make_shared<FakeHost>(fakeAsio, nullptr, fakeMessageFactory);
    auto fakeSocket = std::make_shared<FakeSocket>();

    std::atomic<size_t> recvPacketCnt = 0;
    std::atomic<size_t> recvBufferSize = 0;
    std::atomic<uint64_t> lastReadTime = utcSteadyTime();

    auto session = std::make_shared<Session>(2, true);
    session->setMessageFactory(fakeHost->messageFactory());
    session->setHost(fakeHost);
    session->setSocket(fakeSocket);

    session->setMessageHandler([&recvPacketCnt, &recvBufferSize, &lastReadTime](NetworkException e,
                                   SessionFace::Ptr sessionFace, Message::Ptr message) {
        // doRead() call this function after reading a message
        lastReadTime = utcSteadyTime();
        if (e.errorCode() != P2PExceptionType::Success)
        {
            std::cout << "error: " << e.errorCode() << " " << e.what() << std::endl;
        }
        {
            static bcos::SharedMutex x_mutex;
            bcos::WriteGuard guard(x_mutex);
            BOOST_CHECK_EQUAL(e.errorCode(), P2PExceptionType::Success);
            BOOST_CHECK(message);
            BOOST_CHECK(message->lengthDirect() > 0);
        }

        recvBufferSize += message->lengthDirect();
        recvPacketCnt++;
    });

    session->start();
    std::dynamic_pointer_cast<FakeASIO>(fakeHost->asioInterface())->triggerRead();

    // send packets
    while (auto packet = messageBuilder.nextPacket())
    {
        std::dynamic_pointer_cast<FakeASIO>(fakeHost->asioInterface())
            ->asyncAppendRecvPacket(packet);
    }

    size_t retryTimes = 0;
    while (auto restPacket = totalPacketNum - recvPacketCnt)
    {
        std::cout << "waiting " << restPacket << " packets" << std::endl;
        retryTimes++;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        BOOST_CHECK(retryTimes < 100);
    }

    BOOST_CHECK_EQUAL(recvPacketCnt, totalPacketNum);
    BOOST_CHECK_EQUAL(recvBufferSize, messageBuilder.sendBufferSize());
    session->setSocket(nullptr);
}

BOOST_AUTO_TEST_CASE(SessionRecvBufferTest)
{
    {
        // 0/r/w                               1024
        // |___________|__________|____________|
        //
        std::size_t recvBufferSize = 1024;
        SessionRecvBuffer recvBuffer(recvBufferSize);

        BOOST_CHECK_EQUAL(recvBuffer.recvBufferSize(), recvBufferSize);

        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize);

        // move header operation
        recvBuffer.moveToHeader();
        BOOST_CHECK_EQUAL(recvBuffer.recvBufferSize(), recvBufferSize);

        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize);

        // 0/r                   w            1024
        // |_____________________|____________|
        //
        // write 1000Byte to buffer
        int writeDataSize1 = 1000;
        auto result = recvBuffer.onWrite(writeDataSize1);
        // success
        BOOST_CHECK_EQUAL(result, true);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1);

        // write 1000B to buffer again
        int writeDataSize2 = 1000;
        result = recvBuffer.onWrite(writeDataSize2);
        // failure
        BOOST_CHECK_EQUAL(result, false);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1);

        // 0/r                               w/1024
        // |_________________________________|
        //
        // write 24B to buffer again
        int writeDataSize3 = 24;
        result = recvBuffer.onWrite(writeDataSize3);
        // ok
        BOOST_CHECK_EQUAL(result, true);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(
            recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1 - writeDataSize3);


        // 0/r    w/1024                      3072
        // |______|___________________________|
        //
        recvBufferSize *= 3;
        //  resize the buffer
        recvBuffer.resizeBuffer(recvBufferSize);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(
            recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1 - writeDataSize3);


        // 0                    r w            3072
        // |____________________|_|____________|
        //
        // read 999B from buffer
        int readDataSize1 = 999;
        result = recvBuffer.onRead(readDataSize1);
        // success
        BOOST_CHECK_EQUAL(result, true);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1 - writeDataSize3);

        recvBuffer.moveToHeader();
        // move data to header
        // r w                             3072
        // |_|______________________________|

        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(),
            recvBufferSize - (writeDataSize1 + writeDataSize3 - readDataSize1));

        // read 3072B from buffer
        int readDataSize2 = 3072;
        result = recvBuffer.onRead(readDataSize2);
        // failure
        BOOST_CHECK_EQUAL(result, false);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(),
            recvBufferSize - (writeDataSize1 + writeDataSize3 - readDataSize1));
    }
}

BOOST_AUTO_TEST_SUITE_END()
