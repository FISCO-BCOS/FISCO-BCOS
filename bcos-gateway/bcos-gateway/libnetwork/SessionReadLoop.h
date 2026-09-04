/**
 * @brief Template definitions of Session::readLoop / startWithPolicy — the read path
 *        is a compile-time policy (see ASIOInterface::awaitableReadSome), so production compiles
 *        against the default policy (a direct, inlined async_read_some) while read-loop test
 *        fakes instantiate the same code with their own parking policy. The bodies live in this
 *        separate header (included by Session.cpp and by the mock-using test TUs) to keep
 *        Session.h a pure class declaration.
 * @file SessionReadLoop.h
 */
#pragma once
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Session.h"
#include <bcos-task/Wait.h>
#include <boost/exception/diagnostic_information.hpp>

namespace bcos::gateway
{
// Read-policy seam (compile-time): identical lifecycle to start(), but the read loop is compiled
// against an explicit ReadPolicy so read-loop test fakes can inject a policy that parks/controls
// read completions (see ASIOInterface::awaitableReadSome). Production call sites use the virtual
// start() (the default policy), which delegates here with ASIOInterface::DefaultReadPolicy — this
// template adds no runtime cost in production. Tests instantiate it with a fake policy, e.g.
// session->startWithPolicy<FakeASIO::ReadPolicy>().
template <typename ReadPolicy>
void Session::startWithPolicy()
{
    SESSION_LOG(INFO) << "[Session::start] this=" << this;
    if (!m_active && m_server.get().haveNetwork())
    {
        m_active = true;
        m_lastWriteTime.store(utcSteadyTime());
        m_lastReadTime.store(utcSteadyTime());
        // fire-and-forget: the detached task owns the coroutine chain; readLoop() holds the
        // session alive in its frame (FIB-184) and exits on error, drop or shutdown.
        task::wait(readLoop<ReadPolicy>());
    }

    auto self = weak_from_this();
    m_idleCheckTimer->registerTimeoutHandler([self]() {
        auto session = self.lock();
        if (session)
        {
            session->checkNetworkStatus();
        }
    });
    m_idleCheckTimer->start();
}

template <typename ReadPolicy>
task::Task<void> Session::readLoop()
{
    // FIB-184: the coroutine frame holds a strong reference to the session for the whole read
    // loop. While the loop is suspended at the co_await below, the buffer handed to
    // async_read_some points into this->m_recvBuffer and the stream is this->m_socket — the frame
    // keeps both alive until the read completes, so a concurrent teardown on another thread can
    // free them only after the read finishes. This supersedes the FIB-97/FIB-184 completion-
    // handler captures with a structural guarantee.
    auto self = shared_from_this();
    try
    {
        while (m_active && m_server.get().haveNetwork())
        {
            if (!m_socket->isConnected())
            {
                SESSION_LOG(WARNING) << LOG_DESC("Error Reading ssl socket is close!");
                drop(TCPError);
                co_return;
            }

            auto writeBuffer = m_recvBuffer.asWriteBuffer();
            std::size_t readSize =
                (writeBuffer.size() > m_maxReadDataSize ? m_maxReadDataSize : writeBuffer.size());
            auto [ec, bytesTransferred] =
                co_await m_server.get().asioInterface()
                    ->template awaitableReadSome<ReadPolicy>(
                        m_socket, boost::asio::buffer((void*)writeBuffer.data(), readSize));

            if (ec)
            {
                SESSION_LOG(INFO) << LOG_DESC("readLoop failed")
                                  << LOG_KV("endpoint", nodeIPEndpoint())
                                  << LOG_KV("message", ec.message());
                drop(TCPError);
                co_return;
            }

            m_lastReadTime.store(utcSteadyTime());

            auto& recvBuffer = this->recvBuffer();
            // FIB-184 (review): onWrite advances the write position and returns false if the
            // just-read bytes would overrun the recv buffer. With the lazy-initial / grow-on-
            // demand buffer the read size is bounded by the write-buffer span, so this should
            // not happen; but if it ever did the bytes would be silently dropped and the stream
            // desynchronized. Treat it as a transport error and drop the session instead.
            if (!recvBuffer.onWrite(bytesTransferred))
            {
                SESSION_LOG(ERROR)
                    << LOG_BADGE("readLoop") << LOG_DESC("recv buffer overflow on write, drop")
                    << LOG_KV("bytesTransferred", bytesTransferred)
                    << LOG_KV("recvBufferSize", recvBuffer.recvBufferSize());
                drop(TCPError);
                co_return;
            }

            // decode every complete message already in the buffer, then loop back for more
            while (true)
            {
                Message::Ptr message = m_messageFactory->buildMessage();
                try
                {
                    auto bufferForWrite = recvBuffer.asWriteBuffer();
                    auto readBuffer = recvBuffer.asReadBuffer();
                    // Note: the decode function may throw exception
                    ssize_t result = message->decode(readBuffer);
                    if (result > 0)
                    {
                        NetworkException e(P2PExceptionType::Success, "Success");
                        onMessage(e, message);
                        recvBuffer.onRead(result);
                    }
                    else if (result == 0)
                    {
                        auto length = message->lengthDirect();
                        if (length > allowMaxMsgSize())
                        {
                            SESSION_LOG(ERROR)
                                << LOG_BADGE("readLoop")
                                << LOG_DESC("the message size exceeded the allow maximum value")
                                << LOG_KV("msgSize", message->length())
                                << LOG_KV("allowMaxMsgSize", allowMaxMsgSize());

                            onMessage(NetworkException(P2PExceptionType::ProtocolError,
                                          "ProtocolError(msg overflow)"),
                                message);
                            drop(UserReason);
                            co_return;
                        }

                        if ((length > recvBuffer.recvBufferSize()) ||
                            (length > bufferForWrite.size()) ||
                            maxReadDataSize() > bufferForWrite.size())
                        {
                            recvBuffer.moveToHeader();

                            // the write buffer is not enough, move the left data to recv
                            // buffer header for waiting for the next read
                            if (length >= recvBuffer.recvBufferSize())
                            {
                                auto resizeRecvBufferSize = 2 * length;
                                resizeRecvBufferSize = std::min<std::size_t>(
                                    resizeRecvBufferSize, m_maxRecvBufferSize);
                                recvBuffer.resizeBuffer(resizeRecvBufferSize);

                                SESSION_LOG(INFO)
                                    << LOG_BADGE("readLoop")
                                    << LOG_DESC(
                                           "the current recv buffer size is not enough for "
                                           "the "
                                           "next message, resize the recv buffer")
                                    << LOG_KV("msgSize", length)
                                    << LOG_KV("resizeRecvBufferSize", resizeRecvBufferSize)
                                    << LOG_KV("allowMaxMsgSize", allowMaxMsgSize());
                            }
                        }

                        // need more data: continue the outer loop and arm the next read
                        // (replaces the doRead() recursion of the old callback version)
                        break;
                    }
                    else
                    {
                        SESSION_LOG(ERROR)
                            << LOG_BADGE("readLoop") << LOG_DESC("decode message error")
                            << LOG_KV("result", result);
                        onMessage(NetworkException(P2PExceptionType::ProtocolError,
                                      "ProtocolError(decode msg error)"),
                            message);
                        drop(UserReason);
                        co_return;
                    }
                }
                catch (std::exception const& e)
                {
                    SESSION_LOG(ERROR) << LOG_DESC("Decode message exception")
                                       << LOG_KV("message", boost::diagnostic_information(e));
                    onMessage(NetworkException(P2PExceptionType::ProtocolError,
                                  "ProtocolError(decode msg exception)"),
                        message);
                    drop(UserReason);
                    co_return;
                }
                catch (...)
                {
                    SESSION_LOG(ERROR)
                        << LOG_DESC("Decode message exception")
                        << LOG_KV("message", boost::current_exception_diagnostic_information());
                    onMessage(NetworkException(P2PExceptionType::ProtocolError,
                                  "ProtocolError(decode msg exception)"),
                        message);
                    drop(UserReason);
                    co_return;
                }
            }
        }
    }
    catch (...)
    {
        // never let an exception escape into the resuming asio handler (see AsioAwaitable.h);
        // a session whose read loop died without a drop would zombie until the idle timer
        SESSION_LOG(ERROR) << LOG_DESC("read loop exception")
                           << LOG_KV("endpoint", nodeIPEndpoint())
                           << LOG_KV("what", boost::current_exception_diagnostic_information());
        drop(TCPError);
        co_return;
    }
}
}  // namespace bcos::gateway
