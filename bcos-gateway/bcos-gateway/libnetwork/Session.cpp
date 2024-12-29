
/** @file Session.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 */

#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Overloaded.h"
#include <bcos-gateway/libnetwork/ASIOInterface.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libnetwork/Host.h>
#include <bcos-gateway/libnetwork/Session.h>
#include <bcos-gateway/libnetwork/SessionFace.h>
#include <bcos-gateway/libnetwork/SocketFace.h>
#include <boost/asio/buffer.hpp>
#include <boost/container/container_fwd.hpp>
#include <boost/throw_exception.hpp>
#include <cstddef>
#include <functional>
#include <iterator>
#include <range/v3/numeric/accumulate.hpp>
#include <range/v3/view/transform.hpp>
#include <utility>
#include <variant>

using namespace bcos;
using namespace bcos::gateway;


Session::Session(
    std::shared_ptr<SocketFace> socket, Host& server, size_t _recvBufferSize, bool _forceSize)
  : m_maxRecvBufferSize(_recvBufferSize < MIN_SESSION_RECV_BUFFER_SIZE ?
                            MIN_SESSION_RECV_BUFFER_SIZE :
                            _recvBufferSize),
    m_recvBuffer(_forceSize ? _recvBufferSize : MIN_SESSION_RECV_BUFFER_SIZE),
    m_server(server),
    m_socket(std::move(socket)),
    m_idleCheckTimer(
        std::make_shared<Timer>(m_socket->ioService(), m_idleTimeInterval, "idleChecker"))
{
    SESSION_LOG(INFO) << "[Session::Session] this=" << this
                      << LOG_KV("recvBufferSize", m_maxRecvBufferSize);
}

Session::~Session() noexcept
{
    SESSION_LOG(INFO) << "[Session::~Session] this=" << this;
    try
    {
        m_idleCheckTimer->stop();
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

bool Session::active() const
{
    return active(m_server);
}

bool Session::active(Host& server) const
{
    return m_active && server.haveNetwork() && m_socket && m_socket->isConnected();
}

void Session::asyncSendMessage(Message::Ptr message, Options options, SessionCallbackFunc callback)
{
    if (!active(m_server))
    {
        SESSION_LOG(WARNING) << "Session inactive";
        if (callback)
        {
            m_server.get().asyncTo([callback = std::move(callback)] {
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
            m_server.get().asyncTo([callback = std::move(callback)] {
                callback(NetworkException(-1, "Msg size overflow"), Message::Ptr());
            });
        }
        return;
    }

    auto session = shared_from_this();

    if (auto result =
            (m_beforeMessageHandler ? m_beforeMessageHandler(*session, *message) : std::nullopt))
    {
        if (callback && result.has_value())
        {
            auto error = result.value();
            auto errorCode = error.errorCode();
            auto errorMessage = error.errorMessage();
            m_server.get().asyncTo([callback = std::move(callback), errorCode,
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
            handler->timeoutHandler.emplace(
                m_server.get().asioInterface()->newTimer(options.timeout));
            auto session = std::weak_ptr<Session>(shared_from_this());
            auto seq = message->seq();
            handler->timeoutHandler->async_wait(
                [session, seq](const boost::system::error_code& _error) {
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
            handler->startTime = utcSteadyTime();
        }

        m_sessionCallbackManager->addCallback(message->seq(), handler);
    }

    EncodedMessage encodedMessage;
    encodedMessage.compress = m_enableCompress;
    message->encode(encodedMessage);

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
    return static_cast<std::size_t>(!m_writeQueue.empty());
}

void Session::send(EncodedMessage encodedMsg)
{
    if (!active() || !m_socket->isConnected())
    {
        return;
    }

    m_writeQueue.push({.m_data = std::move(encodedMsg), .m_callback = {}});
    write();
}

void send(Session& session, ::ranges::input_range auto&& payloads,
    std::function<void(boost::system::error_code)> callback)
{
    Payload payload{.m_data{Payload::MessageList{}}, .m_callback = std::move(callback)};
    auto& vec = std::get<Payload::MessageList>(payload.m_data);
    if constexpr (::ranges::sized_range<decltype(payloads)>)
    {
        vec.reserve(::ranges::size(payloads));
    }
    for (const auto& data : payloads)
    {
        vec.emplace_back(data.data(), data.size());
    }

    session.m_writeQueue.push(std::move(payload));
    session.write();
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

bool Session::tryPopSomeEncodedMsgs(
    std::vector<Payload>& encodedMsgs, size_t _maxSendDataSize, size_t _maxSendMsgCount)  // NOLINT
{
    // Desc: Try to send multi packets one time to improve the efficiency of sending
    // data
    size_t totalDataSize = 0;
    while (totalDataSize < _maxSendDataSize && encodedMsgs.size() < _maxSendMsgCount &&
           m_writeQueue.try_pop(encodedMsgs.emplace_back()))
    {
        totalDataSize += encodedMsgs.back().size();
    }

    return totalDataSize > 0;
}

void Session::write()
{
    if (!m_server.get().haveNetwork())
    {
        SESSION_LOG(WARNING) << "Host has gone";
        drop(TCPError);
        return;
    }
    if (!m_socket->isConnected())
    {
        SESSION_LOG(WARNING) << "Error sending ssl socket is close!"
                             << LOG_KV("endpoint", nodeIPEndpoint());
        drop(TCPError);
        return;
    }

    try
    {
        bool writing = false;
        if (!m_writing.compare_exchange_strong(writing, true))
        {
            return;
        }
        std::unique_ptr<std::atomic_bool, decltype([](std::atomic_bool* ptr) { *ptr = false; })>
            defer(std::addressof(m_writing));

        if (!tryPopSomeEncodedMsgs(m_writingPayloads, m_maxSendDataSize, m_maxSendMsgCountS))
        {
            return;
        }

        auto outputIt = std::back_inserter(m_writingBuffers);
        for (auto& payload : m_writingPayloads)
        {
            payload.toConstBuffer(outputIt);
        }
        defer.release();  // NOLINT
        m_server.get().asioInterface()->asyncWrite(m_socket, m_writingBuffers,
            [self = std::weak_ptr<Session>(shared_from_this())](
                const boost::system::error_code _error, std::size_t _size) mutable {
                if (auto session = self.lock())
                {
                    std::vector<Payload> payloads;
                    payloads.swap(session->m_writingPayloads);
                    session->m_writingBuffers.clear();

                    session->m_writing = false;
                    session->onWrite(_error, _size);

                    for (auto& payload : payloads)
                    {
                        if (payload.m_callback)
                        {
                            payload.m_callback(_error);
                        }
                    }
                }
            });
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

    if (m_messageHandler)
    {
        m_server.get().asyncTo(
            [self = weak_from_this(), errorCode, errorMsg = std::move(errorMsg)]() {
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
            if (socket->isConnected())
            {
                socket->close();
            }
            auto shutdown_timer = std::make_shared<boost::asio::deadline_timer>(
                socket->ioService(), boost::posix_time::milliseconds(m_shutDownTimeThres));
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
    if (!m_active && m_server.get().haveNetwork())
    {
        m_active = true;
        m_lastWriteTime.store(utcSteadyTime());
        m_lastReadTime.store(utcSteadyTime());
        m_server.get().asioInterface()->strandPost(
            [session = shared_from_this()] { session->doRead(); });
    }

    auto self = weak_from_this();
    m_idleCheckTimer->registerTimeoutHandler([self]() {
        if (auto session = self.lock())
        {
            session->checkNetworkStatus();
        }
    });
    m_idleCheckTimer->start();

    SESSION_LOG(INFO) << "[start] start session " << LOG_KV("this", this);
}

void Session::doRead()
{
    if (m_active && m_server.get().haveNetwork())
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
            m_server.get().asioInterface()->asyncReadSome(
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
                           << LOG_KV("haveNetwork", m_server.get().haveNetwork());
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
    m_server.get().asyncTo([self = weak_from_this(), e, message]() {
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
            // in-activate session
            if (!session->m_active || !session->m_server.get().haveNetwork())
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

    ResponseCallback::Ptr callback = m_sessionCallbackManager->getCallback(seq, true);
    if (!callback)
    {
        return;
    }
    m_server.get().asyncTo([callback = std::move(callback)]() {
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

bcos::task::Task<Message::Ptr> bcos::gateway::Session::sendMessage(
    const Message& message, ::ranges::any_view<bytesConstRef> payloads, Options options)
{
    if (!active())
    {
        SESSION_LOG(WARNING) << "Session inactive";
        co_return {};
    }

    bytes headerBuffer;
    message.encodeHeader(headerBuffer);

    auto view = ::ranges::views::concat(
        ::ranges::views::single(bcos::ref(std::as_const(headerBuffer))), payloads);
    uint32_t totalLength = 0;
    for (auto ref : view)
    {
        totalLength += ref.size();
    }
    *(uint32_t*)headerBuffer.data() =
        boost::asio::detail::socket_ops::host_to_network_long(totalLength);

    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        SESSION_LOG(TRACE) << LOG_DESC("Session asyncSendMessage")
                           << LOG_KV("endpoint", nodeIPEndpoint()) << LOG_KV("seq", message.seq())
                           << LOG_KV("packetType", message.packetType())
                           << LOG_KV("ext", message.ext());
    }
    if (options.response)
    {
        struct Awaitable
        {
            std::reference_wrapper<Options> m_options;
            std::reference_wrapper<Host> m_host;
            std::reference_wrapper<const Message> m_message;
            std::weak_ptr<Session> m_self;
            std::reference_wrapper<SessionCallbackManagerInterface> m_sessionCallbackManager;
            std::reference_wrapper<decltype(view)> m_view;
            std::variant<NetworkException, Message::Ptr> m_result;

            constexpr static bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<> handle)
            {
                auto handler = std::make_shared<ResponseCallback>();
                handler->callback = [this, handle](
                                        NetworkException exception, Message::Ptr response) {
                    if (exception.errorCode() != 0)
                    {
                        m_result.emplace<NetworkException>(std::move(exception));
                    }
                    else
                    {
                        m_result.emplace<Message::Ptr>(std::move(response));
                    }
                    handle.resume();
                };
                auto seq = m_message.get().seq();
                if (m_options.get().timeout > 0)
                {
                    handler->timeoutHandler.emplace(
                        m_host.get().asioInterface()->newTimer(m_options.get().timeout));
                    handler->timeoutHandler->async_wait(
                        [self = m_self, seq](const boost::system::error_code& _error) {
                            try
                            {
                                if (auto session = self.lock())
                                {
                                    session->onTimeout(_error, seq);
                                }
                            }
                            catch (std::exception const& e)
                            {
                                SESSION_LOG(WARNING)
                                    << LOG_DESC("async_wait exception")
                                    << LOG_KV("message", boost::diagnostic_information(e));
                            }
                        });
                    handler->startTime = utcSteadyTime();
                }
                m_sessionCallbackManager.get().addCallback(seq, std::move(handler));
                ::send(*m_self.lock(), m_view.get(), {});
            }
            Message::Ptr await_resume()
            {
                return std::visit(
                    bcos::overloaded(
                        [](NetworkException& exception) -> Message::Ptr {
                            BOOST_THROW_EXCEPTION(exception);
                            return {};
                        },
                        [](Message::Ptr& response) -> Message::Ptr { return std::move(response); }),
                    m_result);
            }
        };

        Awaitable awaitable{.m_options = options,
            .m_host = m_server,
            .m_message = message,
            .m_self = shared_from_this(),
            .m_sessionCallbackManager = *m_sessionCallbackManager,
            .m_view = view,
            .m_result = {}};

        co_return co_await awaitable;
    }
    else
    {
        struct Awaitable
        {
            std::reference_wrapper<Session> m_self;
            std::reference_wrapper<decltype(view)> m_view;
            NetworkException m_exception;

            constexpr static bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<> handle)
            {
                ::send(m_self, m_view.get(), [this, handle](boost::system::error_code errorCode) {
                    if (errorCode.failed())
                    {
                        m_exception = NetworkException(errorCode.value(), errorCode.message());
                    }
                    handle.resume();
                });
            }
            void await_resume() const
            {
                if (m_exception.errorCode() != 0)
                {
                    BOOST_THROW_EXCEPTION(m_exception);
                }
            }
        };
        Awaitable awaitable{.m_self = *this, .m_view = view, .m_exception = {}};
        co_await awaitable;
        co_return {};
    }
}

size_t bcos::gateway::Payload::size() const
{
    return std::visit(
        bcos::overloaded(
            [](const EncodedMessage& encodedMessage) -> size_t {
                return encodedMessage.header.size() + encodedMessage.payload.size();
            },
            [](const boost::container::small_vector<bytesConstRef, 3>& refs) {
                return ::ranges::accumulate(refs, size_t(0),
                    [](size_t sum, const bytesConstRef& ref) { return sum + ref.size(); });
            }),
        m_data);
}
