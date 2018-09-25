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
#include "SocketFace.h"
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
namespace ba = boost::asio;
namespace bi = ba::ip;

namespace dev
{
namespace p2p
{
/**
 * @brief interface of boost::asio related callback
 * (added for unittest)
 */
class AsioInterface
{
public:
    /// accept handler
    typedef std::function<void(const boost::system::error_code&)> Handler_Type;
    /// write handler
    typedef std::function<void(const boost::system::error_code&, std::size_t)> ReadWriteHandler;
    typedef std::function<bool(bool, boost::asio::ssl::verify_context&)> VerifyCallback;
    /// default implemetation of async_accept
    virtual void async_accept(bi::tcp::acceptor& tcp_acceptor, std::shared_ptr<SocketFace>& socket,
        boost::asio::io_service::strand& m_strand, Handler_Type handler,
        boost::system::error_code ec = boost::system::error_code())
    {
        tcp_acceptor.async_accept(socket->ref(), m_strand.wrap(handler));
    }

    /// default implementation of async_connect
    virtual void async_connect(std::shared_ptr<SocketFace>& socket,
        boost::asio::io_service::strand& m_strand, const bi::tcp::endpoint& peer_endpoint,
        Handler_Type handler, boost::system::error_code ec = boost::system::error_code())
    {
        socket->ref().async_connect(peer_endpoint, m_strand.wrap(handler));
    }

    /// default implementation of async_write
    virtual void async_write(std::shared_ptr<SocketFace> const& socket,
        boost::asio::mutable_buffers_1 buffers, ReadWriteHandler handler,
        std::size_t transferred_bytes = 0)
    {
        ba::async_write(socket->sslref(), buffers, handler);
    }

    /// default implementation of async_read
    virtual void async_read(std::shared_ptr<SocketFace> const& socket,
        boost::asio::io_service::strand& m_strand, boost::asio::mutable_buffers_1 buffers,
        ReadWriteHandler handler)
    {
        ba::async_read(socket->sslref(), buffers, m_strand.wrap(handler));
    }

    /// default implementation of async_handshake
    virtual void async_handshake(std::shared_ptr<SocketFace> const& socket,
        boost::asio::io_service::strand& m_strand, ba::ssl::stream_base::handshake_type const& type,
        Handler_Type handler, boost::system::error_code ec = boost::system::error_code())
    {
        socket->sslref().async_handshake(type, m_strand.wrap(handler));
    }

    virtual void async_wait(boost::asio::deadline_timer* m_timer,
        boost::asio::io_service::strand& m_strand, Handler_Type handler,
        boost::system::error_code ec = boost::system::error_code())
    {
        if (m_timer)
            return m_timer->async_wait(m_strand.wrap(handler));
    }

    virtual void set_verify_callback(
        std::shared_ptr<SocketFace> const& socket, VerifyCallback callback, bool verify_succ = true)
    {
        socket->sslref().set_verify_callback(callback);
    }
};
}  // namespace p2p
}  // namespace dev
