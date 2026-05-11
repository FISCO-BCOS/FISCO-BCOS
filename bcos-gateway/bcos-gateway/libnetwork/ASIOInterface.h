/**
 * @brief: inteface for boost::asio(for unittest)
 *
 * @file AsioInterface.h
 * @author: yujiechen
 * @date 2018-09-13
 */
#pragma once
#include "bcos-gateway/libnetwork/Socket.h"
#include "bcos-utilities/IOServicePool.h"
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

    virtual ~ASIOInterface();
    virtual void setType(int type);

    virtual std::shared_ptr<ba::io_context> ioService();
    virtual void setIOServicePool(IOServicePool::Ptr _ioServicePool);

    virtual std::shared_ptr<ba::ssl::context> srvContext();
    virtual std::shared_ptr<ba::ssl::context> clientContext();

    virtual void setSrvContext(std::shared_ptr<ba::ssl::context> _srvContext);
    virtual void setClientContext(std::shared_ptr<ba::ssl::context> _clientContext);

    virtual boost::asio::deadline_timer newTimer(uint32_t timeout);

    virtual std::shared_ptr<SocketFace> newSocket(
        bool _server, NodeIPEndpoint nodeIPEndpoint = NodeIPEndpoint());

    virtual std::shared_ptr<bi::tcp::acceptor> acceptor();

    virtual void init(std::string listenHost, uint16_t listenPort);

    virtual void start();
    virtual void stop();

    virtual void asyncAccept(const std::shared_ptr<SocketFace>& socket, Handler_Type handler,
        boost::system::error_code /*unused*/ = boost::system::error_code());

    virtual void asyncResolveConnect(
        const std::shared_ptr<SocketFace>& socket, Handler_Type handler);

    void asyncWrite(const std::shared_ptr<SocketFace>& socket, const auto& buffers, auto handler)
    {
        auto type = m_type;
        if (socket->isConnected())
        {
            auto& ioService = socket->ioService();
            boost::asio::post(
                ioService, [type, socket, &buffers, handler = std::move(handler)]() mutable {
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

    virtual void strandPost(Base_Handler handler);

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
