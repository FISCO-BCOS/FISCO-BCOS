/**
 * @brief: inteface for boost::asio(for unittest)
 *
 * @file AsioInterface.h
 * @author: yujiechen
 * @date 2018-09-13
 */
#pragma once
#include "bcos-gateway/libnetwork/SocketFace.h"
#include "bcos-utilities/IOServicePool.h"
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <optional>
#include <string>
#include <utility>

namespace ba = boost::asio;
namespace bi = ba::ip;

namespace bcos::gateway
{
class ASIOInterface
{
public:
    enum ASIO_TYPE
    {
        TCP_ONLY = 0,
        SSL = 1
    };

    /// CompletionHandler
    using Base_Handler = std::function<void()>;
    /// accept handler
    using Handler_Type = std::function<void(const boost::system::error_code)>;
    /// write handler
    using ReadWriteHandler = std::function<void(const boost::system::error_code, std::size_t)>;
    using VerifyCallback = std::function<bool(bool, boost::asio::ssl::verify_context&)>;

    ASIOInterface(IOServicePool::Ptr _ioServicePool, std::string listenHost, uint16_t listenPort);
    virtual ~ASIOInterface();
    virtual void setType(int type);

    virtual void setIOServicePool(IOServicePool::Ptr _ioServicePool);

    virtual ba::ssl::context* srvContext();
    virtual ba::ssl::context* clientContext();

    virtual void setSrvContext(ba::ssl::context _srvContext);
    virtual void setClientContext(ba::ssl::context _clientContext);

    virtual boost::asio::steady_timer newTimer(uint32_t timeout);

    virtual std::shared_ptr<SocketFace> newSocket(
        bool _server, NodeIPEndpoint nodeIPEndpoint = NodeIPEndpoint());

    virtual bi::tcp::acceptor* acceptor();

    virtual void asyncAccept(const std::shared_ptr<SocketFace>& socket, Handler_Type handler,
        boost::system::error_code /*unused*/ = boost::system::error_code());

    virtual void asyncResolveConnect(
        const std::shared_ptr<SocketFace>& socket, Handler_Type handler);

    void asyncWrite(const std::shared_ptr<SocketFace>& socket, auto buffers, auto handler)
    {
        auto type = m_type;
        if (socket->isConnected())
        {
            auto& ioService = socket->ioService();
            boost::asio::post(ioService, [type, socket, buffers = std::move(buffers),
                                             handler = std::move(handler)]() mutable {
                switch (type)
                {
                case TCP_ONLY:
                {
                    ba::async_write(socket->ref(), buffers, std::move(handler));
                    break;
                }
                case SSL:
                {
                    ba::async_write(socket->sslref(), buffers, std::move(handler));
                    break;
                }
                }
            });
        }
    }

    virtual void asyncRead(const std::shared_ptr<SocketFace>& socket,
        boost::asio::mutable_buffer buffers, ReadWriteHandler handler);

    virtual void asyncReadSome(const std::shared_ptr<SocketFace>& socket,
        boost::asio::mutable_buffer buffers, ReadWriteHandler handler);

    virtual void asyncHandshake(const std::shared_ptr<SocketFace>& socket,
        ba::ssl::stream_base::handshake_type type, Handler_Type handler);

    virtual void setVerifyCallback(
        const std::shared_ptr<SocketFace>& socket, VerifyCallback callback, bool /*unused*/ = true);

    template <class Task>
    void dispatch(Task&& task)
    {
        m_ioServicePool->dispatch(std::forward<Task>(task));
    }

    template <class Task>
    void post(Task&& task)
    {
        m_ioServicePool->post(std::forward<Task>(task));
    }

private:
    IOServicePool::Ptr m_ioServicePool;
    bi::tcp::acceptor m_acceptor;
    bi::tcp::resolver m_resolver;

    std::optional<ba::ssl::context> m_srvContext;
    std::optional<ba::ssl::context> m_clientContext;
    int m_type = 0;
};
}  // namespace bcos::gateway
