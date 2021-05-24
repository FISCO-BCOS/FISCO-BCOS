/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief: inteface for boost::asio(for unittest)
 *
 * @file AsioInterface.h
 * @author: yujiechen
 * @date 2018-09-13
 */
#pragma once
#include "Socket.h"
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>

namespace ba = boost::asio;
namespace bi = ba::ip;

namespace dev
{
namespace network
{
class ASIOInterface
{
public:
    enum ASIO_TYPE
    {
        TCP_ONLY = 0,
        SSL = 1,
        WEBSOCKET = 2
    };

    /// CompletionHandler
    typedef boost::function<void()> Base_Handler;
    /// accept handler
    typedef boost::function<void(const boost::system::error_code)> Handler_Type;
    /// write handler
    typedef boost::function<void(const boost::system::error_code, std::size_t)> ReadWriteHandler;
    typedef boost::function<bool(bool, boost::asio::ssl::verify_context&)> VerifyCallback;

    virtual ~ASIOInterface(){};
    virtual void setType(int type) { m_type = type; }

    virtual std::shared_ptr<ba::io_service> ioService() { return m_ioService; }
    virtual void setIOService(std::shared_ptr<ba::io_service> ioService)
    {
        m_ioService = ioService;
    }

    virtual std::shared_ptr<ba::ssl::context> sslContext() { return m_sslContext; }
    virtual void setSSLContext(std::shared_ptr<ba::ssl::context> sslContext)
    {
        m_sslContext = sslContext;
    }

    virtual std::shared_ptr<boost::asio::deadline_timer> newTimer(uint32_t timeout)
    {
        return std::make_shared<boost::asio::deadline_timer>(
            *m_ioService, boost::posix_time::milliseconds(timeout));
    }

    virtual std::shared_ptr<SocketFace> newSocket(NodeIPEndpoint nodeIPEndpoint = NodeIPEndpoint())
    {
        std::shared_ptr<SocketFace> m_socket =
            std::make_shared<Socket>(*m_ioService, *m_sslContext, nodeIPEndpoint);
        return m_socket;
    }

    virtual std::shared_ptr<bi::tcp::acceptor> acceptor() { return m_acceptor; }

    virtual void init(std::string listenHost, uint16_t listenPort)
    {
        m_strand = std::make_shared<boost::asio::io_service::strand>(*m_ioService);
        m_resolver = std::make_shared<bi::tcp::resolver>(*m_ioService);
        m_acceptor = std::make_shared<bi::tcp::acceptor>(
            *m_ioService, bi::tcp::endpoint(bi::make_address(listenHost), listenPort));
        boost::asio::socket_base::reuse_address optionReuseAddress(true);
        m_acceptor->set_option(optionReuseAddress);
    }

    virtual void run() { m_ioService->run(); }

    virtual void stop()
    {
        // shutdown acceptor
        if (m_acceptor && m_acceptor->is_open())
        {
            m_acceptor->cancel();
            m_acceptor->close();
        }

        m_ioService->stop();
    }

    virtual void reset()
    {
        if (m_ioService->stopped())
        {
            m_ioService->reset();
        }
    }

    virtual void asyncAccept(std::shared_ptr<SocketFace> socket, Handler_Type handler,
        boost::system::error_code = boost::system::error_code())
    {
        m_acceptor->async_accept(socket->ref(), m_strand->wrap(handler));
    }
#if 0
    virtual void asyncConnect(std::shared_ptr<SocketFace> socket,
        const bi::tcp::endpoint peer_endpoint, Handler_Type handler,
        boost::system::error_code = boost::system::error_code())
    {
        socket->ref().async_connect(peer_endpoint, handler);
    }
#endif
    virtual void asyncResolveConnect(std::shared_ptr<SocketFace> socket, Handler_Type handler);

    virtual void asyncWrite(std::shared_ptr<SocketFace> socket,
        boost::asio::mutable_buffers_1 buffers, ReadWriteHandler handler)
    {
        auto type = m_type;
        m_ioService->post([type, socket, buffers, handler]() {
            if (socket->isConnected())
            {
                switch (type)
                {
                case TCP_ONLY:
                {
                    ba::async_write(socket->ref(), buffers, handler);
                    break;
                }
                case SSL:
                {
                    ba::async_write(socket->sslref(), buffers, handler);
                    break;
                }
                case WEBSOCKET:
                {
                    // ba::async_write(socket->wsref(), buffers, handler);
                    socket->wsref().async_write(buffers, handler);
                    break;
                }
                }
            }
        });
    }

    virtual void asyncRead(std::shared_ptr<SocketFace> socket,
        boost::asio::mutable_buffers_1 buffers, ReadWriteHandler handler)
    {
        switch (m_type)
        {
        case TCP_ONLY:
        {
            ba::async_read(socket->ref(), buffers, handler);
            break;
        }
        case SSL:
        {
            ba::async_read(socket->sslref(), buffers, handler);
            break;
        }
        case WEBSOCKET:
        {
            ba::async_read(socket->wsref(), buffers, handler);
            break;
        }
        }
    }

    virtual void asyncReadSome(std::shared_ptr<SocketFace> socket,
        boost::asio::mutable_buffers_1 buffers, ReadWriteHandler handler)
    {
        switch (m_type)
        {
        case TCP_ONLY:
        {
            socket->ref().async_read_some(buffers, handler);
            break;
        }
        case SSL:
        {
            socket->sslref().async_read_some(buffers, handler);
            break;
        }
        case WEBSOCKET:
        {
            socket->wsref().async_read_some(buffers, handler);
            break;
        }
        }
    }

    virtual void asyncHandshake(std::shared_ptr<SocketFace> socket,
        ba::ssl::stream_base::handshake_type type, Handler_Type handler)
    {
            socket->sslref().async_handshake(type, handler);
        
    }

    virtual void asyncWait(boost::asio::deadline_timer* m_timer,
        boost::asio::io_service::strand& m_strand, Handler_Type handler,
        boost::system::error_code = boost::system::error_code())
    {
        if (m_timer)
            m_timer->async_wait(m_strand.wrap(handler));
    }

    virtual void setVerifyCallback(
        std::shared_ptr<SocketFace> socket, VerifyCallback callback, bool = true)
    {
        socket->sslref().set_verify_callback(callback);
    }

    virtual void strandPost(Base_Handler handler) { m_strand->post(handler); }

protected:
    std::shared_ptr<ba::io_service> m_ioService;
    std::shared_ptr<ba::io_service::strand> m_strand;
    std::shared_ptr<bi::tcp::acceptor> m_acceptor;
    std::shared_ptr<bi::tcp::resolver> m_resolver;
    std::shared_ptr<ba::ssl::context> m_sslContext;
    int m_type = 0;
};
}  // namespace network
}  // namespace dev
