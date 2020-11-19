/*
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
 */
/** @file Socket.h
 * @author toxotguo
 * @date 2018
 *
 * @ author: yujiechen
 * @ date: 2018-09-17
 * @ modification: rename RLPXSocket.h to Socket.h
 */

#pragma once

#include "libnetwork/Common.h"
#include "libnetwork/SocketFace.h"
#include <libutilities/FileSystem.h>
#include <openssl/ec.h>
#include <openssl/ssl.h>
#include <boost/beast.hpp>
#include <boost/filesystem.hpp>

namespace bcos
{
namespace network
{
class FakeSocket : public SocketFace, public std::enable_shared_from_this<FakeSocket>
{
public:
    FakeSocket(
        ba::io_service& _ioService, ba::ssl::context& _sslContext, NodeIPEndpoint _nodeIPEndpoint)
      : m_nodeIPEndpoint(_nodeIPEndpoint)
    {
        try
        {
            m_wsSocket =
                std::make_shared<boost::beast::websocket::stream<ba::ssl::stream<bi::tcp::socket>>>(
                    _ioService, _sslContext);
        }
        catch (Exception const& _e)
        {
            SESSION_LOG(ERROR) << "ERROR: " << diagnostic_information(_e);
            SESSION_LOG(ERROR) << "Ssl Socket Init Fail! Please Check CERTIFICATE!";
        }
    }
    ~FakeSocket() { close(); }

    bool isConnected() const override { return m_alive; }

    void close() override
    {
        m_alive = false;
        try
        {
            boost::system::error_code ec;
            m_wsSocket->lowest_layer().shutdown(bi::tcp::socket::shutdown_both, ec);
            if (m_wsSocket->lowest_layer().is_open())
                m_wsSocket->lowest_layer().close();
        }
        catch (...)
        {
        }
    }
    void open() { m_alive = true; }
    void write(boost::asio::mutable_buffers_1 buffers)
    {
        auto b = std::make_shared<boost::asio::streambuf>();
        boost::asio::streambuf::mutable_buffers_type bufs = b->prepare(buffers.size());
        auto copydSize = boost::asio::buffer_copy(bufs, buffers);
        b->commit(copydSize);
        m_queue.push(b);
    }

    size_t doRead(boost::asio::mutable_buffers_1 buffers)
    {
        if (!m_alive || m_queue.size() == 0)
        {
            return 0;
        }
        auto p = m_queue.front();
        if (p->size() == 0)
        {
            return 0;
        }
        auto copydSize = boost::asio::buffer_copy(buffers, p->data(), buffers.size());
        p->consume(copydSize);
        if (p->size() == 0)
        {
            m_queue.pop();
        }
        return copydSize;
    }
    bi::tcp::endpoint remoteEndpoint(boost::system::error_code) override
    {
        return bi::tcp::endpoint(m_nodeIPEndpoint.address().empty() ?
                                     bi::address::from_string("0.0.0.0") :
                                     bi::address::from_string(m_nodeIPEndpoint.address()),
            m_nodeIPEndpoint.port());
    }
    bi::tcp::endpoint localEndpoint(boost::system::error_code) override
    {
        return bi::tcp::endpoint(m_nodeIPEndpoint.address().empty() ?
                                     bi::address::from_string("0.0.0.0") :
                                     bi::address::from_string(m_nodeIPEndpoint.address()),
            m_nodeIPEndpoint.port());
    }
    void setRemoteEndpoint(const bi::tcp::endpoint& end) { m_nodeIPEndpoint = end; }

    bi::tcp::socket& ref() override { return m_wsSocket->next_layer().next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() override { return m_wsSocket->next_layer(); }
    boost::beast::websocket::stream<ba::ssl::stream<bi::tcp::socket>>& wsref() override
    {
        return *m_wsSocket;
    }

    const NodeIPEndpoint& nodeIPEndpoint() const override { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) override
    {
        m_nodeIPEndpoint = _nodeIPEndpoint;
    }


protected:
    bool m_alive = true;
    NodeIPEndpoint m_nodeIPEndpoint;
    std::queue<std::shared_ptr<boost::asio::streambuf>> m_queue;
    std::shared_ptr<boost::beast::websocket::stream<ba::ssl::stream<bi::tcp::socket>>> m_wsSocket;
};

}  // namespace network
}  // namespace bcos
