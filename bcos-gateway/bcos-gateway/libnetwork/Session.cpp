
/** @file Session.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 */

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

using namespace bcos;
using namespace bcos::gateway;

Session::Session(size_t _bufferSize) : bufferSize(_bufferSize)
{
    SESSION_LOG(INFO) << "[Session::Session] this=" << this;
    m_recvBuffer.resize(bufferSize);
    m_seq2Callback = std::make_shared<std::unordered_map<uint32_t, ResponseCallback::Ptr>>();
    m_idleCheckTimer = std::make_shared<bcos::Timer>(m_idleTimeInterval, "idleChecker");
    m_idleCheckTimer->registerTimeoutHandler([this]() { checkNetworkStatus(); });
}

Session::~Session()
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

bool Session::actived() const
{
    auto server = m_server.lock();
    return m_actived && server && server->haveNetwork() && m_socket && m_socket->isConnected();
}

void Session::asyncSendMessage(Message::Ptr message, Options options, SessionCallbackFunc callback)
{
    auto server = m_server.lock();
    if (!actived())
    {
        SESSION_LOG(WARNING) << "Session inactived";
        if (callback)
        {
            server->threadPool()->enqueue([callback] {
                callback(NetworkException(-1, "Session inactived"), Message::Ptr());
            });
        }
        return;
    }

    auto session = shared_from_this();
    // checking before send the message
    if (m_beforeMessageHandler && !m_beforeMessageHandler(session, message, callback))
    {
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
                                         << LOG_KV("error", boost::diagnostic_information(e));
                }
            });
            handler->timeoutHandler = timeoutHandler;
            handler->m_startTime = utcSteadyTime();
        }
        addSeqCallback(message->seq(), handler);
    }
    SESSION_LOG(TRACE) << LOG_DESC("Session asyncSendMessage")
                       << LOG_KV("endpoint", nodeIPEndpoint());

    std::shared_ptr<bytes> p_buffer = std::make_shared<bytes>();
    message->encode(*p_buffer);

    send(p_buffer);
}

void Session::send(std::shared_ptr<bytes> _msg)
{
    if (!actived())
    {
        return;
    }

    if (!m_socket->isConnected())
        return;

    SESSION_LOG(TRACE) << "send" << LOG_KV("writeQueue size", m_writeQueue.size());
    {
        Guard l(x_writeQueue);

        m_writeQueue.push(make_pair(_msg, u256(utcTime())));
    }

    write();
}

void Session::onWrite(boost::system::error_code ec, std::size_t, std::shared_ptr<bytes>)
{
    if (!actived())
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
        {
            if (m_writing)
            {
                m_writing = false;
            }
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

void Session::write()
{
    if (!actived())
    {
        return;
    }

    try
    {
        Guard l(x_writeQueue);

        if (m_writing)
        {
            return;
        }

        m_writing = true;

        std::pair<std::shared_ptr<bytes>, u256> task;
        u256 enter_time = u256(0);

        if (m_writeQueue.empty())
        {
            m_writing = false;
            return;
        }

        task = m_writeQueue.top();
        m_writeQueue.pop();

        enter_time = task.second;
        auto buffer = task.first;

        auto server = m_server.lock();
        if (server && server->haveNetwork())
        {
            if (m_socket->isConnected())
            {
                // asio::buffer referecne buffer, so buffer need alive before
                // asio::buffer be used
                auto self = std::weak_ptr<Session>(shared_from_this());
                server->asioInterface()->asyncWrite(m_socket, boost::asio::buffer(*buffer),
                    [self, buffer](const boost::system::error_code _error, std::size_t _size) {
                        auto session = self.lock();
                        if (!session)
                        {
                            return;
                        }
                        session->onWrite(_error, _size, buffer);
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
    if (!m_actived)
        return;
    m_actived = false;

    int errorCode = P2PExceptionType::Disconnect;
    std::string errorMsg = "Disconnect";
    if (_reason == DuplicatePeer)
    {
        errorCode = P2PExceptionType::DuplicateSession;
        errorMsg = "DuplicateSession";
    }

    SESSION_LOG(INFO) << "drop, call and erase all callback in this session!"
                      << LOG_KV("endpoint", nodeIPEndpoint());
    RecursiveGuard l(x_seq2Callback);
    for (auto& it : *m_seq2Callback)
    {
        if (it.second->timeoutHandler)
        {
            it.second->timeoutHandler->cancel();
        }
        if (it.second->callback)
        {
            SESSION_LOG(TRACE) << "drop, call callback by seq" << LOG_KV("seq", it.first);
            if (server)
            {
                auto callback = it.second;
                server->threadPool()->enqueue([callback, errorCode, errorMsg]() {
                    callback->callback(NetworkException(errorCode, errorMsg), Message::Ptr());
                });
            }
        }
    }
    clearSeqCallback();

    if (server && m_messageHandler)
    {
        auto handler = m_messageHandler;
        auto self = std::weak_ptr<Session>(shared_from_this());
        server->threadPool()->enqueue([handler, self, errorCode, errorMsg]() {
            auto session = self.lock();
            if (!session)
            {
                return;
            }
            handler(NetworkException(errorCode, errorMsg), session, Message::Ptr());
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
                SESSION_LOG(WARNING) << "[drop] closing remote " << m_socket->remoteEndpoint()
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
                    SESSION_LOG(DEBUG) << "[drop] operation aborted  by async_shutdown"
                                       << LOG_KV("errorValue", error.value())
                                       << LOG_KV("message", error.message());
                    return;
                }
                /// shutdown timer error
                if (error && error != boost::asio::error::operation_aborted)
                {
                    SESSION_LOG(WARNING)
                        << "[drop] shutdown timer error" << LOG_KV("errorValue", error.value())
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
                        SESSION_LOG(WARNING)
                            << "[drop] shutdown failed " << LOG_KV("errorValue", error.value())
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
        {}
    }
}

void Session::disconnect(DisconnectReason _reason)
{
    drop(_reason);
}

void Session::start()
{
    if (!m_actived)
    {
        auto server = m_server.lock();
        if (server && server->haveNetwork())
        {
            m_actived = true;
            m_lastWriteTime.store(utcSteadyTime());
            m_lastReadTime.store(utcSteadyTime());
            server->asioInterface()->strandPost(
                boost::bind(&Session::doRead, shared_from_this()));  // doRead();
        }
    }
    if (m_idleCheckTimer)
    {
        m_idleCheckTimer->start();
    }
}

void Session::doRead()
{
    auto server = m_server.lock();
    if (m_actived && server && server->haveNetwork())
    {
        auto self = std::weak_ptr<Session>(shared_from_this());
        auto asyncRead = [self](boost::system::error_code ec, std::size_t bytesTransferred) {
            auto s = self.lock();
            if (s)
            {
                if (ec)
                {
                    SESSION_LOG(WARNING)
                        << LOG_DESC("doRead error") << LOG_KV("endpoint", s->nodeIPEndpoint())
                        << LOG_KV("message", ec.message());
                    s->drop(TCPError);
                    return;
                }
                s->m_lastReadTime.store(utcSteadyTime());
                s->m_data.insert(s->m_data.end(), s->m_recvBuffer.begin(),
                    s->m_recvBuffer.begin() + bytesTransferred);

                while (true)
                {
                    Message::Ptr message = s->m_messageFactory->buildMessage();
                    try
                    {
                        // Note: the decode function may throw exception
                        ssize_t result =
                            message->decode(bytesConstRef(s->m_data.data(), s->m_data.size()));
                        if (result > 0)
                        {
                            /// SESSION_LOG(TRACE) << "Decode success: " << result;
                            NetworkException e(P2PExceptionType::Success, "Success");
                            s->onMessage(e, message);
                            s->m_data.erase(s->m_data.begin(), s->m_data.begin() + result);
                        }
                        else if (result == 0)
                        {
                            s->doRead();
                            break;
                        }
                        else
                        {
                            SESSION_LOG(ERROR)
                                << LOG_DESC("Decode message error") << LOG_KV("result", result);
                            s->onMessage(
                                NetworkException(P2PExceptionType::ProtocolError, "ProtocolError"),
                                message);
                            break;
                        }
                    }
                    catch (std::exception const& e)
                    {
                        SESSION_LOG(ERROR) << LOG_DESC("Decode message exception")
                                           << LOG_KV("error", boost::diagnostic_information(e));
                        s->onMessage(
                            NetworkException(P2PExceptionType::ProtocolError, "ProtocolError"),
                            message);
                        break;
                    }
                }
            }
        };

        if (m_socket->isConnected())
        {
            server->asioInterface()->asyncReadSome(
                m_socket, boost::asio::buffer(m_recvBuffer, m_recvBuffer.size()), asyncRead);
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
        SESSION_LOG(ERROR) << LOG_DESC("callback doRead failed for session inactived")
                           << LOG_KV("active", m_actived)
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
    auto self = std::weak_ptr<Session>(shared_from_this());
    server->threadPool()->enqueue([e, message, self]() {
        auto session = self.lock();
        if (!session)
        {
            return;
        }
        try
        {
            // the forwarding message
            if (message->dstP2PNodeID().size() > 0 &&
                message->dstP2PNodeID() != session->m_hostNodeID)
            {
                session->m_messageHandler(e, session, message);
                return;
            }
            auto server = session->m_server.lock();
            // in-activate session
            if (!session->m_actived || !server || !server->haveNetwork())
            {
                return;
            }
            auto callbackPtr = session->getCallbackBySeq(message->seq());
            // without callback, call default handler
            if (!callbackPtr || !message->isRespPacket())
            {
                session->m_messageHandler(e, session, message);
                return;
            }
            // with callback
            if (callbackPtr->timeoutHandler)
            {
                callbackPtr->timeoutHandler->cancel();
            }
            auto callback = callbackPtr->callback;
            session->removeSeqCallback(message->seq());
            if (!callback)
            {
                return;
            }
            callback(e, message);
        }
        catch (std::exception const& e)
        {
            SESSION_LOG(WARNING) << LOG_DESC("onMessage exception")
                                 << LOG_KV("msg", boost::diagnostic_information(e));
        }
    });
}

void Session::onTimeout(const boost::system::error_code& error, uint32_t seq)
{
    if (error)
    {
        SESSION_LOG(TRACE) << "timer cancel" << error;
        return;
    }

    auto server = m_server.lock();
    if (!server)
        return;
    ResponseCallback::Ptr callbackPtr = getCallbackBySeq(seq);
    if (!callbackPtr)
        return;
    server->threadPool()->enqueue([=, this]() {
        NetworkException e(P2PExceptionType::NetworkTimeout, "NetworkTimeout");
        callbackPtr->callback(e, Message::Ptr());
        removeSeqCallback(seq);
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