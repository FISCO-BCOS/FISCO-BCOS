/**
 * @brief: inteface for boost::asio(for unittest)
 *
 * @file AsioInterface.h
 * @author: yujiechen
 * @date 2018-09-13
 */
#pragma once
#include <bcos-gateway/libnetwork/Socket.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
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
    using Base_Handler = boost::function<void()>;
    /// accept handler
    using Handler_Type = boost::function<void(const boost::system::error_code)>;
    /// write handler
    using ReadWriteHandler = boost::function<void(const boost::system::error_code, std::size_t)>;
    using VerifyCallback = boost::function<bool(bool, boost::asio::ssl::verify_context&)>;

    virtual ~ASIOInterface() {}
    virtual void setType(int type) { m_type = type; }

    virtual std::shared_ptr<ba::io_context> ioService() { return m_ioServicePool->getIOService(); }
    virtual void setIOServicePool(IOServicePool::Ptr _ioServicePool)
    {
        m_ioServicePool = std::move(_ioServicePool);
        m_timerIOService = m_ioServicePool->getIOService();
    }

    virtual std::shared_ptr<ba::ssl::context> srvContext() { return m_srvContext; }
    virtual std::shared_ptr<ba::ssl::context> clientContext() { return m_clientContext; }

    virtual void setSrvContext(std::shared_ptr<ba::ssl::context> _srvContext)
    {
        m_srvContext = std::move(_srvContext);
    }
    virtual void setClientContext(std::shared_ptr<ba::ssl::context> _clientContext)
    {
        m_clientContext = std::move(_clientContext);
    }

    virtual boost::asio::deadline_timer newTimer(uint32_t timeout)
    {
        return {*(m_timerIOService), boost::posix_time::milliseconds(timeout)};
    }

    virtual std::shared_ptr<SocketFace> newSocket(
        bool _server, NodeIPEndpoint nodeIPEndpoint = NodeIPEndpoint())
    {
        std::shared_ptr<SocketFace> m_socket =
            std::make_shared<Socket>(m_ioServicePool->getIOService(),
                _server ? *m_srvContext : *m_clientContext, nodeIPEndpoint);
        return m_socket;
    }

    virtual std::shared_ptr<bi::tcp::acceptor> acceptor() { return m_acceptor; }

    virtual void init(std::string listenHost, uint16_t listenPort)
    {
        m_strand =
            std::make_shared<boost::asio::io_context::strand>(*(m_ioServicePool->getIOService()));
        m_resolver = std::make_shared<bi::tcp::resolver>(*(m_ioServicePool->getIOService()));
        m_acceptor = std::make_shared<bi::tcp::acceptor>(*(m_ioServicePool->getIOService()),
            bi::tcp::endpoint(bi::make_address(listenHost), listenPort));
        boost::asio::socket_base::reuse_address optionReuseAddress(true);
        m_acceptor->set_option(optionReuseAddress);
    }

    virtual void start() { m_ioServicePool->start(); }
    virtual void stop() { m_ioServicePool->stop(); }

    virtual void asyncAccept(const std::shared_ptr<SocketFace>& socket, Handler_Type handler,
        boost::system::error_code /*unused*/ = boost::system::error_code())
    {
        m_acceptor->async_accept(socket->ref(), handler);
    }

    virtual void asyncResolveConnect(
        const std::shared_ptr<SocketFace>& socket, Handler_Type handler);

    void asyncWrite(const std::shared_ptr<SocketFace>& socket, const auto& buffers, auto handler)
    {
        auto type = m_type;
        if (socket->isConnected())
        {
            auto& ioService = socket->ioService();
            ioService.post([type, socket, &buffers, handler = std::move(handler)]() mutable {
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
        boost::asio::mutable_buffers_1 buffers, ReadWriteHandler handler)
    {
        switch (m_type)
        {
        case TCP_ONLY:
        {
            ba::async_read(socket->ref(), buffers, std::move(handler));
            break;
        }
        case SSL:
        {
            ba::async_read(socket->sslref(), buffers, std::move(handler));
            break;
        }
        }
    }

    virtual void asyncReadSome(const std::shared_ptr<SocketFace>& socket,
        boost::asio::mutable_buffers_1 buffers, ReadWriteHandler handler)
    {
        switch (m_type)
        {
        case TCP_ONLY:
        {
            socket->ref().async_read_some(buffers, std::move(handler));
            break;
        }
        case SSL:
        {
            socket->sslref().async_read_some(buffers, std::move(handler));
            break;
        }
        }
    }

    virtual void asyncHandshake(const std::shared_ptr<SocketFace>& socket,
        ba::ssl::stream_base::handshake_type type, Handler_Type handler)
    {
        socket->sslref().async_handshake(type, std::move(handler));
    }

    virtual void setVerifyCallback(
        const std::shared_ptr<SocketFace>& socket, VerifyCallback callback, bool /*unused*/ = true)
    {
        socket->sslref().set_verify_callback(std::move(callback));
    }

    virtual void strandPost(Base_Handler handler) { m_strand->post(handler); }

protected:
    IOServicePool::Ptr m_ioServicePool;
    std::shared_ptr<ba::io_context> m_timerIOService;
    std::shared_ptr<ba::io_context::strand> m_strand;
    std::shared_ptr<bi::tcp::acceptor> m_acceptor;
    std::shared_ptr<bi::tcp::resolver> m_resolver;

    std::shared_ptr<ba::ssl::context> m_srvContext;
    std::shared_ptr<ba::ssl::context> m_clientContext;
    int m_type = 0;
};
}  // namespace bcos::gateway
