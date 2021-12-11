
/** @file Session.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 */

#include <bcos-boostssl/network/ASIOInterface.h>  // for ASIOIn...
#include <bcos-boostssl/network/Common.h>         // for SESSIO...
#include <bcos-boostssl/network/Host.h>           // for Host
#include <bcos-boostssl/network/Session.h>        // for session
#include <bcos-boostssl/network/SessionFace.h>    // for Respon...
#include <bcos-boostssl/network/SocketFace.h>     // for Socket...
#include <chrono>

using namespace boostssl;
using namespace boostssl::net;
using namespace boostssl::utility;

Session::Session(size_t _bufferSize) : bufferSize(_bufferSize)
{
    SESSION_LOG(INFO) << "[Session::Session] this=" << this;
    m_recvBuffer.resize(bufferSize);
    m_seq2Callback = std::make_shared<std::unordered_map<uint32_t, ResponseCallback::Ptr>>();
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
    if (callback)
    {
        auto handler = std::make_shared<ResponseCallback>();
        handler->callback = callback;
        if (options.timeout > 0)
        {
            std::shared_ptr<boost::asio::deadline_timer> timeoutHandler =
                server->asioInterface()->newTimer(options.timeout);

            auto session = std::weak_ptr<Session>(shared_from_this());
            timeoutHandler->async_wait(boost::bind(&Session::onTimeout, shared_from_this(),
                boost::asio::placeholders::error, message->seq()));

            handler->timeoutHandler = timeoutHandler;
        }
        addSeqCallback(message->seq(), handler);
    }
    SESSION_LOG(TRACE) << LOG_DESC("Session asyncSendMessage")
                       << LOG_KV("seq2Callback.size", m_seq2Callback->size())
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
        uint64_t msgSeq = ++m_msgSeq;
        std::lock_guard<std::mutex> l(x_writeQueue);
        m_writeQueue.push(make_pair(_msg, msgSeq));
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
        updateIdleTimer(m_writeIdleTimer);
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
    catch (const std::exception& e)
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
        std::lock_guard<std::mutex> lock(x_writeQueue);

        if (m_writing)
        {
            return;
        }

        m_writing = true;

        std::pair<std::shared_ptr<bytes>, uint64_t> task;
        uint64_t enter_time = 0;

        if (m_writeQueue.empty())
        {
            m_writing = false;
            return;
        }

        task = m_writeQueue.top();
        m_writeQueue.pop();

        enter_time = task.second;
        auto session = shared_from_this();
        auto buffer = task.first;

        auto server = m_server.lock();
        if (server && server->haveNetwork())
        {
            if (m_socket->isConnected())
            {
                // asio::buffer referecne buffer, so buffer need alive before
                // asio::buffer be used
                server->asioInterface()->asyncWrite(m_socket, boost::asio::buffer(*buffer),
                    boost::bind(&Session::onWrite, session, boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred, buffer));
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
    catch (const std::exception& e)
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

    if (m_readIdleTimer)
    {
        m_readIdleTimer->cancel();
    }
    if (m_writeIdleTimer)
    {
        m_writeIdleTimer->cancel();
    }
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
    std::lock_guard<std::mutex> l(x_seq2Callback);
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
        auto self = shared_from_this();
        server->threadPool()->enqueue([handler, self, errorCode, errorMsg]() {
            handler(NetworkException(errorCode, errorMsg), self, Message::Ptr());
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
            auto shutdown_timer =
                std::make_shared<boost::asio::deadline_timer>(*server->asioInterface()->ioService(),
                    boost::posix_time::milliseconds(m_shutDownTimeThres));
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
        {
        }
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
            updateIdleTimer(m_writeIdleTimer);
            updateIdleTimer(m_readIdleTimer);
            server->asioInterface()->strandPost(
                boost::bind(&Session::doRead, shared_from_this()));  // doRead();
        }
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
                s->updateIdleTimer(s->m_readIdleTimer);
                s->m_data.insert(s->m_data.end(), s->m_recvBuffer.begin(),
                    s->m_recvBuffer.begin() + bytesTransferred);

                while (true)
                {
                    Message::Ptr message = s->m_messageFactory->buildMessage();
                    ssize_t result = message->decode(s->m_data.data(), s->m_data.size());
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
    if (m_actived && server && server->haveNetwork())
    {
        ResponseCallback::Ptr callbackPtr = getCallbackBySeq(message->seq());
        if (callbackPtr && message->isRespPacket())
        {
            /// SESSION_LOG(TRACE) << "Found callbackPtr: " << message->seq();

            if (callbackPtr->timeoutHandler)
            {
                callbackPtr->timeoutHandler->cancel();
            }

            if (callbackPtr->callback)
            {
                auto callback = callbackPtr->callback;
                if (callback)
                {
                    auto self = std::weak_ptr<Session>(shared_from_this());
                    server->threadPool()->enqueue([e, callback, self, message]() {
                        callback(e, message);

                        auto s = self.lock();
                        if (s)
                        {
                            s->removeSeqCallback(message->seq());
                        }
                    });
                }
            }
        }
        else
        {
            if (m_messageHandler)
            {
                SESSION_LOG(TRACE) << "onMessage can't find callback, call default messageHandler"
                                   << LOG_KV("message.seq", message->seq());
                auto session = shared_from_this();
                auto handler = m_messageHandler;

                server->threadPool()->enqueue(
                    [session, handler, e, message]() { handler(e, session, message); });
            }
            else
            {
                SESSION_LOG(WARNING) << "onMessage can't find callback and default messageHandler";
            }
        }
    }
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
    server->threadPool()->enqueue([=]() {
        NetworkException e(P2PExceptionType::NetworkTimeout, "NetworkTimeout");
        callbackPtr->callback(e, Message::Ptr());
        removeSeqCallback(seq);
    });
}

void Session::updateIdleTimer(std::shared_ptr<boost::asio::deadline_timer> _idleTimer)
{
    if (!m_actived)
    {
        SESSION_LOG(ERROR) << LOG_DESC("Session inactived");

        return;
    }
    if (_idleTimer)
    {
        _idleTimer->expires_from_now(boost::posix_time::seconds(m_idleTimeInterval));

        auto session = std::weak_ptr<Session>(shared_from_this());
        _idleTimer->async_wait([session](const boost::system::error_code& error) {
            auto s = session.lock();
            if (s)
            {
                s->onIdle(error);
            }
        });
    }
}

void Session::onIdle(const boost::system::error_code& error)
{
    try
    {
        if (!m_actived)
        {
            SESSION_LOG(ERROR) << LOG_DESC("Session inactived");

            return;
        }
        if (error != boost::asio::error::operation_aborted)
        {
            SESSION_LOG(ERROR) << LOG_DESC("Idle connection, disconnect ")
                               << LOG_KV("endpoint", m_socket->nodeIPEndpoint());
            drop(IdleWaitTimeout);
        }
    }
    catch (const std::exception& e)
    {
        SESSION_LOG(ERROR) << LOG_DESC("onIdle error")
                           << LOG_KV("errorMessage", boost::diagnostic_information(e));
    }
}

void Session::setHost(std::weak_ptr<Host> host)
{
    m_server = host;
    auto server = m_server.lock();
    if (server && server->haveNetwork())
    {
        m_readIdleTimer =
            std::make_shared<boost::asio::deadline_timer>(*server->asioInterface()->ioService());
        m_writeIdleTimer =
            std::make_shared<boost::asio::deadline_timer>(*server->asioInterface()->ioService());
    }
    else
    {
        SESSION_LOG(ERROR) << LOG_DESC("create idleTimer failed for the host has no network");
    }
}
