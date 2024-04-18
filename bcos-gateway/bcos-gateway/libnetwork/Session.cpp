
/** @file Session.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 */

#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/CompositeBuffer.h"
#include <bcos-gateway/libnetwork/ASIOInterface.h>  // for ASIOIn...
#include <bcos-gateway/libnetwork/Common.h>         // for SESSIO...
#include <bcos-gateway/libnetwork/Host.h>           // for Host
#include <bcos-gateway/libnetwork/Session.h>
#include <bcos-gateway/libnetwork/SessionFace.h>  // for Respon...
#include <bcos-gateway/libnetwork/SocketFace.h>   // for Socket...
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <utility>

using namespace bcos;
using namespace bcos::gateway;


Session::Session(size_t _recvBufferSize, bool _forceSize)
  : m_maxRecvBufferSize(_recvBufferSize < MIN_SESSION_RECV_BUFFER_SIZE ?
                            MIN_SESSION_RECV_BUFFER_SIZE :
                            _recvBufferSize),
    m_recvBuffer(_forceSize ? _recvBufferSize : MIN_SESSION_RECV_BUFFER_SIZE)
{
    SESSION_LOG(INFO) << "[Session::Session] this=" << this
                      << LOG_KV("recvBufferSize", m_maxRecvBufferSize);
    m_idleCheckTimer = std::make_shared<bcos::Timer>(m_idleTimeInterval, "idleChecker");
}

Session::~Session() noexcept
{
    SESSION_LOG(INFO) << "[Session::~Session] this=" << this;
    try
    {
        if (m_socket)
        {
            bi::tcp::socket& socket = m_socket->ref();
            if (m_socket->isConnected())
            {
                socket.close();
            }
        }
        if (m_idleCheckTimer)
        {
            m_idleCheckTimer->stop();
        }
    }
    catch (...)
    {
        SESSION_LOG(ERROR) << "Deconstruct Session exception";
    }
}

NodeIPEndpoint Session::nodeIPEndpoint() const
{
    return m_socket->nodeIPEndpoint();
}

bool Session::active() const
{
    auto server = m_server.lock();
    return active(server);
}

bool Session::active(std::shared_ptr<bcos::gateway::Host>& server) const
{
    return m_active && server && server->haveNetwork() && m_socket && m_socket->isConnected();
}

void Session::asyncSendMessage(Message::Ptr message, Options options, SessionCallbackFunc callback)
{
    auto server = m_server.lock();
    if (!server)
    {
        return;
    }
    if (!active(server))
    {
        SESSION_LOG(WARNING) << "Session inactive";
        if (callback)
        {
            server->asyncTo([callback = std::move(callback)] {
                callback(NetworkException(-1, "Session inactive"), Message::Ptr());
            });
        }
        return;
    }

    // Notice: check message size overflow, if msg->length() > allowMaxMsgSize()
    if (message->length() > allowMaxMsgSize())
    {
        SESSION_LOG(WARNING) << LOG_BADGE("asyncSendMessage") << LOG_DESC("msg size overflow")
                             << LOG_KV("msgSize", message->length())
                             << LOG_KV("allowMaxMsgSize", allowMaxMsgSize());
        if (callback)
        {
            server->asyncTo([callback = std::move(callback)] {
                callback(NetworkException(-1, "Msg size overflow"), Message::Ptr());
            });
        }
        return;
    }

    auto session = shared_from_this();

    if (auto result =
            (m_beforeMessageHandler ? m_beforeMessageHandler(session, message) : std::nullopt))
    {
        if (callback && result.has_value())
        {
            auto error = result.value();
            auto errorCode = error.errorCode();
            auto errorMessage = error.errorMessage();
            server->asyncTo([callback = std::move(callback), errorCode,
                                errorMessage = std::move(errorMessage)] {
                callback(NetworkException((int64_t)errorCode, errorMessage), Message::Ptr());
            });
        }
        return;
    }

    if (callback)
    {
        auto handler = std::make_shared<ResponseCallback>();
        handler->callback = callback;
        if (options.timeout > 0)
        {
            std::shared_ptr<boost::asio::deadline_timer> timeoutHandler =
                server->asioInterface()->newTimer(options.timeout);

            auto session = std::weak_ptr<Session>(shared_from_this());
            auto seq = message->seq();
            timeoutHandler->async_wait([session, seq](const boost::system::error_code& _error) {
                try
                {
                    auto s = session.lock();
                    if (!s)
                    {
                        return;
                    }
                    s->onTimeout(_error, seq);
                }
                catch (std::exception const& e)
                {
                    SESSION_LOG(WARNING) << LOG_DESC("async_wait exception")
                                         << LOG_KV("message", boost::diagnostic_information(e));
                }
            });
            handler->timeoutHandler = timeoutHandler;
            handler->startTime = utcSteadyTime();
        }

        m_sessionCallbackManager->addCallback(message->seq(), handler);
    }

    EncodedMessage::Ptr encodedMessage = std::make_shared<EncodedMessage>();
    encodedMessage->compress = m_enableCompress;
    message->encode(*encodedMessage);

    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        SESSION_LOG(TRACE) << LOG_DESC("Session asyncSendMessage")
                           << LOG_KV("endpoint", nodeIPEndpoint()) << LOG_KV("seq", message->seq())
                           << LOG_KV("packetType", message->packetType())
                           << LOG_KV("ext", message->ext());
    }

    send(encodedMessage);
}

std::size_t Session::writeQueueSize()
{
    Guard lockGuard(x_writeQueue);
    return m_writeQueue.size();
}

void Session::send(EncodedMessage::Ptr& _encodedMsg)
{
    if (!active())
    {
        return;
    }

    if (!m_socket->isConnected())
    {
        return;
    }

    {
        Guard lockGuard(x_writeQueue);
        m_writeQueue.push_back(std::move(_encodedMsg));
    }

    write();
}

void Session::onWrite(boost::system::error_code ec, std::size_t /*unused*/)
{
    if (!active())
    {
        return;
    }

    try
    {
        m_lastWriteTime.store(utcSteadyTime());
        if (ec)
        {
            SESSION_LOG(WARNING) << LOG_DESC("onWrite error sending")
                                 << LOG_KV("message", ec.message())
                                 << LOG_KV("endpoint", nodeIPEndpoint());
            drop(TCPError);
            return;
        }
        if (m_writing)
        {
            m_writeConstBuffer.clear();
            m_writing = false;
        }
        else
        {
            SESSION_LOG(ERROR) << LOG_DESC("onWrite wrong state") << LOG_KV("m_writing", m_writing);
        }

        write();
    }
    catch (std::exception& e)
    {
        SESSION_LOG(ERROR) << LOG_DESC("onWrite error") << LOG_KV("endpoint", nodeIPEndpoint())
                           << LOG_KV("what", boost::diagnostic_information(e));
        drop(TCPError);
        return;
    }
}

/**
 * @brief The packets that can be sent are obtained based on the configured policy
 *
 * @param encodedMsgs
 * @param _maxSendDataSize
 * @param _maxSendMsgCount
 * @return std::size_t
 */
std::size_t Session::tryPopSomeEncodedMsgs(std::vector<EncodedMessage::Ptr>& encodedMsgs,
    uint32_t _maxSendDataSize, uint32_t _maxSendMsgCount)  // NOLINT
{
    // Desc: Try to send multi packets one time to improve the efficiency of sending
    // data
    uint64_t totalDataSize = 0;
    encodedMsgs.clear();
    encodedMsgs.reserve(_maxSendMsgCount);

    do
    {
        // Notice: lock m_writeQueue in the caller
        if (m_writeQueue.empty())
        {
            break;
        }

        // msg count will overflow
        if (encodedMsgs.size() >= _maxSendMsgCount)
        {
            break;
        }

        EncodedMessage::Ptr encodedMsg = m_writeQueue.front();
        totalDataSize += encodedMsg->dataSize();

        // data size will overflow
        if (totalDataSize > _maxSendDataSize)
        {
            // At least one msg pkg
            if (encodedMsgs.empty())
            {
                encodedMsgs.push_back(std::move(encodedMsg));
                m_writeQueue.pop_front();
            }
            else
            {
                totalDataSize -= encodedMsg->dataSize();
            }
            break;
        }

        encodedMsgs.push_back(std::move(encodedMsg));
        m_writeQueue.pop_front();
    } while (true);

    return totalDataSize;
}

void Session::write()
{
    // TODO: use reference instead of weak_ptr
    auto server = m_server.lock();
    if (!active(server))
    {
        return;
    }

    try
    {
        std::vector<EncodedMessage::Ptr> encodedMsgs;
        Guard lockGuard(x_writeQueue);
        if (m_writing)
        {
            return;
        }
        m_writing = true;

        if (m_writeQueue.empty())
        {
            m_writing = false;
            return;
        }

        m_writeConstBuffer.clear();
        // Try to send multi packets one time to improve the efficiency of sending
        // data
        tryPopSomeEncodedMsgs(encodedMsgs, m_maxSendDataSize, m_maxSendMsgCountS);

        if (server && server->haveNetwork())
        {
            if (m_socket->isConnected())
            {
                // asio::buffer reference buffer, so buffer need alive before
                // asio::buffer be used
                auto self = std::weak_ptr<Session>(shared_from_this());

                toMultiBuffers(m_writeConstBuffer, encodedMsgs);
                server->asioInterface()->asyncWrite(m_socket, m_writeConstBuffer,
                    [self, encodedMsgs](const boost::system::error_code _error, std::size_t _size) {
                        auto session = self.lock();
                        if (!session)
                        {
                            return;
                        }
                        session->onWrite(_error, _size);
                    });
            }
            else
            {
                SESSION_LOG(WARNING)
                    << "Error sending ssl socket is close!" << LOG_KV("endpoint", nodeIPEndpoint());
                drop(TCPError);
                return;
            }
        }
        else
        {
            SESSION_LOG(WARNING) << "Host has gone";
            drop(TCPError);
            return;
        }
    }
    catch (std::exception& e)
    {
        SESSION_LOG(ERROR) << LOG_DESC("write error") << LOG_KV("endpoint", nodeIPEndpoint())
                           << LOG_KV("what", boost::diagnostic_information(e));
        drop(TCPError);
        return;
    }
}

void Session::drop(DisconnectReason _reason)
{
    auto server = m_server.lock();
    if (!m_active)
    {
        return;
    }
    m_active = false;

    int errorCode = P2PExceptionType::Disconnect;
    std::string errorMsg = "Disconnect";
    if (_reason == DuplicatePeer)
    {
        errorCode = P2PExceptionType::DuplicateSession;
        errorMsg = "DuplicateSession";
    }

    SESSION_LOG(INFO) << "drop, call and erase all callback in this session!"
                      << LOG_KV("this", this) << LOG_KV("endpoint", nodeIPEndpoint());

    if (server && m_messageHandler)
    {
        server->asyncTo([self = weak_from_this(), errorCode, errorMsg = std::move(errorMsg)]() {
            auto session = self.lock();
            if (!session)
            {
                return;
            }
            session->m_messageHandler(
                NetworkException(errorCode, errorMsg), session, Message::Ptr());
        });
    }

    if (m_socket->isConnected())
    {
        try
        {
            if (_reason == DisconnectRequested || _reason == DuplicatePeer ||
                _reason == ClientQuit || _reason == UserReason)
            {
                SESSION_LOG(DEBUG) << "[drop] closing remote " << m_socket->remoteEndpoint()
                                   << LOG_KV("reason", reasonOf(_reason))
                                   << LOG_KV("endpoint", m_socket->nodeIPEndpoint());
            }
            else
            {
                SESSION_LOG(INFO) << "[drop] closing remote " << m_socket->remoteEndpoint()
                                  << LOG_KV("reason", reasonOf(_reason))
                                  << LOG_KV("endpoint", m_socket->nodeIPEndpoint());
            }

            /// if get Host object failed, close the socket directly
            auto socket = m_socket;
            auto server = m_server.lock();
            if (server && socket->isConnected())
            {
                socket->close();
            }
            auto shutdown_timer = std::make_shared<boost::asio::deadline_timer>(
                *(socket->ioService()), boost::posix_time::milliseconds(m_shutDownTimeThres));
            /// async wait for shutdown
            shutdown_timer->async_wait([socket](const boost::system::error_code& error) {
                /// drop operation has been aborted
                if (error == boost::asio::error::operation_aborted)
                {
                    SESSION_LOG(DEBUG)
                        << "[drop] operation aborted  by async_shutdown"
                        << LOG_KV("value", error.value()) << LOG_KV("message", error.message());
                    return;
                }
                /// shutdown timer error
                if (error && error != boost::asio::error::operation_aborted)
                {
                    SESSION_LOG(WARNING)
                        << "[drop] shutdown timer failed" << LOG_KV("failedValue", error.value())
                        << LOG_KV("message", error.message());
                }
                /// force to shutdown when timeout
                if (socket->ref().is_open())
                {
                    SESSION_LOG(WARNING) << "[drop] timeout, force close the socket"
                                         << LOG_KV("remote endpoint", socket->nodeIPEndpoint());
                    socket->close();
                }
            });

            /// async shutdown normally
            socket->sslref().async_shutdown(
                [socket, shutdown_timer](const boost::system::error_code& error) {
                    shutdown_timer->cancel();
                    if (error)
                    {
                        SESSION_LOG(INFO)
                            << "[drop] shutdown failed " << LOG_KV("failedValue", error.value())
                            << LOG_KV("message", error.message());
                    }
                    /// force to close the socket
                    if (socket->ref().is_open())
                    {
                        SESSION_LOG(WARNING) << LOG_DESC("force to shutdown session")
                                             << LOG_KV("endpoint", socket->nodeIPEndpoint());
                        socket->close();
                    }
                });
        }
        catch (...)
        {
            SESSION_LOG(ERROR) << LOG_DESC("drop error") << LOG_KV("endpoint", nodeIPEndpoint());
        }
    }
}

void Session::disconnect(DisconnectReason _reason)
{
    drop(_reason);
}

void Session::start()
{
    if (!m_active)
    {
        auto server = m_server.lock();
        if (server && server->haveNetwork())
        {
            m_active = true;
            m_lastWriteTime.store(utcSteadyTime());
            m_lastReadTime.store(utcSteadyTime());
            server->asioInterface()->strandPost(
                [session = shared_from_this()] { session->doRead(); });
        }
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

    SESSION_LOG(INFO) << "[start] start session " << LOG_KV("this", this);
}

void Session::doRead()
{
    auto server = m_server.lock();
    if (m_active && server && server->haveNetwork())
    {
        auto self = std::weak_ptr<Session>(shared_from_this());
        auto asyncRead = [self](boost::system::error_code ec, std::size_t bytesTransferred) {
            auto session = self.lock();
            if (session)
            {
                if (ec)
                {
                    SESSION_LOG(INFO)
                        << LOG_DESC("doRead error") << LOG_KV("endpoint", session->nodeIPEndpoint())
                        << LOG_KV("message", ec.message());
                    session->drop(TCPError);
                    return;
                }

                session->m_lastReadTime.store(utcSteadyTime());

                auto& recvBuffer = session->recvBuffer();
                recvBuffer.onWrite(bytesTransferred);

                while (true)
                {
                    Message::Ptr message = session->m_messageFactory->buildMessage();
                    try
                    {
                        auto writeBuffer = recvBuffer.asWriteBuffer();
                        auto readBuffer = recvBuffer.asReadBuffer();
                        // Note: the decode function may throw exception
                        ssize_t result = message->decode(readBuffer);
                        if (result > 0)
                        {
                            NetworkException e(P2PExceptionType::Success, "Success");
                            session->onMessage(e, message);
                            recvBuffer.onRead(result);
                        }
                        else if (result == 0)
                        {
                            auto length = message->lengthDirect();
                            assert(length <= session->allowMaxMsgSize());
                            if (length > session->allowMaxMsgSize())
                            {
                                SESSION_LOG(ERROR)
                                    << LOG_BADGE("doRead")
                                    << LOG_DESC("the message size exceeded the allow maximum value")
                                    << LOG_KV("msgSize", message->length())
                                    << LOG_KV("allowMaxMsgSize", session->allowMaxMsgSize());

                                session->onMessage(NetworkException(P2PExceptionType::ProtocolError,
                                                       "ProtocolError(msg overflow)"),
                                    message);
                                break;
                            }

                            if ((length > recvBuffer.recvBufferSize()) ||
                                (length > writeBuffer.size()) ||
                                session->maxReadDataSize() > writeBuffer.size())
                            {
                                recvBuffer.moveToHeader();

                                // the write buffer is not enough, move the left data to recv
                                // buffer header for waiting for the next read
                                if (length >= recvBuffer.recvBufferSize())
                                {
                                    auto resizeRecvBufferSize = 2 * length;
                                    if (resizeRecvBufferSize > session->m_maxRecvBufferSize)
                                    {
                                        resizeRecvBufferSize = session->m_maxRecvBufferSize;
                                    }
                                    recvBuffer.resizeBuffer(resizeRecvBufferSize);

                                    SESSION_LOG(INFO)
                                        << LOG_BADGE("doRead")
                                        << LOG_DESC(
                                               "the current recv buffer size is not enough for "
                                               "the "
                                               "next message, resize the recv buffer")
                                        << LOG_KV("msgSize", length)
                                        << LOG_KV("resizeRecvBufferSize", resizeRecvBufferSize)
                                        << LOG_KV("allowMaxMsgSize", session->allowMaxMsgSize());
                                }
                            }

                            session->doRead();
                            break;
                        }
                        else
                        {
                            SESSION_LOG(ERROR)
                                << LOG_BADGE("doRead") << LOG_DESC("decode message error")
                                << LOG_KV("result", result);
                            session->onMessage(NetworkException(P2PExceptionType::ProtocolError,
                                                   "ProtocolError(decode msg error)"),
                                message);
                            break;
                        }
                    }
                    catch (std::exception const& e)
                    {
                        SESSION_LOG(ERROR) << LOG_DESC("Decode message exception")
                                           << LOG_KV("message", boost::diagnostic_information(e));
                        session->onMessage(NetworkException(P2PExceptionType::ProtocolError,
                                               "ProtocolError(decode msg exception)"),
                            message);
                        break;
                    }
                }
            }
        };

        if (m_socket->isConnected())
        {
            auto writeBuffer = m_recvBuffer.asWriteBuffer();
            std::size_t readSize =
                (writeBuffer.size() > m_maxReadDataSize ? m_maxReadDataSize : writeBuffer.size());
            server->asioInterface()->asyncReadSome(
                m_socket, boost::asio::buffer((void*)writeBuffer.data(), readSize), asyncRead);
        }
        else
        {
            SESSION_LOG(WARNING) << LOG_DESC("Error Reading ssl socket is close!");
            drop(TCPError);
            return;
        }
    }
    else
    {
        SESSION_LOG(ERROR) << LOG_DESC("callback doRead failed for session inactive")
                           << LOG_KV("active", m_active)
                           << LOG_KV("haveNetwork", server->haveNetwork());
    }
}

bool Session::checkRead(boost::system::error_code _ec)
{
    if (_ec && _ec.category() != boost::asio::error::get_misc_category() &&
        _ec.value() != boost::asio::error::eof)
    {
        SESSION_LOG(WARNING) << LOG_DESC("checkRead error") << LOG_KV("message", _ec.message());
        drop(TCPError);

        return false;
    }

    return true;
}


void Session::onMessage(NetworkException const& e, Message::Ptr message)
{
    auto server = m_server.lock();
    if (!server)
    {
        return;
    }
    server->asyncTo([self = weak_from_this(), e, message]() {
        try
        {
            auto session = self.lock();
            if (!session)
            {
                return;
            }
            // TODO: move the logic to Service for deal with the forwarding message
            if (!message->dstP2PNodeID().empty() &&
                message->dstP2PNodeID() != session->m_hostNodeID)
            {
                session->m_messageHandler(e, session, message);
                return;
            }
            auto server = session->m_server.lock();
            // in-activate session
            if (!session->m_active || !server || !server->haveNetwork())
            {
                return;
            }

            if (!message->isRespPacket())
            {
                session->m_messageHandler(e, session, message);
                return;
            }

            auto callbackManager = session->sessionCallbackManager();
            auto callbackPtr = callbackManager->getCallback(message->seq(), true);
            // without callback, call default handler
            if (!callbackPtr)
            {
                SESSION_LOG(WARNING)
                    << LOG_BADGE("onMessage")
                    << LOG_DESC("callback not found, maybe the callback timeout")
                    << LOG_KV("endpoint", session->nodeIPEndpoint())
                    << LOG_KV("seq", message->seq()) << LOG_KV("resp", message->isRespPacket());
                return;
            }

            // with callback
            if (callbackPtr->timeoutHandler)
            {
                callbackPtr->timeoutHandler->cancel();
            }
            auto callback = callbackPtr->callback;
            if (!callback)
            {
                return;
            }
            callback(e, message);
        }
        catch (std::exception const& e)
        {
            SESSION_LOG(WARNING) << LOG_BADGE("onMessage") << LOG_DESC("onMessage exception")
                                 << LOG_KV("msg", boost::diagnostic_information(e));
        }
    });
}

void Session::onTimeout(const boost::system::error_code& error, uint32_t seq)
{
    if (error)
    {
        // SESSION_LOG(TRACE) << "timer cancel" << error;
        return;
    }

    auto server = m_server.lock();
    if (!server)
    {
        return;
    }
    ResponseCallback::Ptr callback = m_sessionCallbackManager->getCallback(seq, true);
    if (!callback)
    {
        return;
    }
    server->asyncTo([callback = std::move(callback)]() {
        NetworkException e(P2PExceptionType::NetworkTimeout, "NetworkTimeout");
        callback->callback(e, Message::Ptr());
    });
}

void Session::checkNetworkStatus()
{
    m_idleCheckTimer->restart();
    try
    {
        auto now = utcSteadyTime();
        // read idle
        if ((m_lastReadTime + m_idleTimeInterval) < now)
        {
            SESSION_LOG(WARNING) << LOG_DESC(
                                        "Long time without read operation, maybe session "
                                        "inactivated, drop the session")
                                 << LOG_KV("endpoint", m_socket->nodeIPEndpoint());
            drop(IdleWaitTimeout);
            return;
        }
        // write idle
        if ((m_lastWriteTime + m_idleTimeInterval) < now)
        {
            SESSION_LOG(WARNING) << LOG_DESC(
                                        "Long time without write operation, maybe session "
                                        "inactivated, drop the session")
                                 << LOG_KV("endpoint", m_socket->nodeIPEndpoint());
            drop(IdleWaitTimeout);
            return;
        }
    }
    catch (std::exception const& e)
    {
        SESSION_LOG(WARNING) << LOG_DESC("checkNetworkStatus error")
                             << LOG_KV("msg", boost::diagnostic_information(e));
    }
}
