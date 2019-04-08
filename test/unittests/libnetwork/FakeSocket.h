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
#include <libdevcore/FileSystem.h>
#include <libdevcore/easylog.h>
#include <openssl/ec.h>
#include <openssl/ssl.h>
#include <boost/beast.hpp>
#include <boost/filesystem.hpp>

namespace dev
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
        m_wsSocket =
            std::make_shared<boost::beast::websocket::stream<ba::ssl::stream<bi::tcp::socket>>>(
                _ioService, _sslContext);
    }
    ~FakeSocket() { close(); }

    bool isConnected() const override { return m_wsSocket->lowest_layer().is_open(); }

    void close() override
    {
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

    bi::tcp::endpoint remoteEndpoint() override
    {
        return m_wsSocket->lowest_layer().remote_endpoint();
    }

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
    bool m_alive = false;
    NodeIPEndpoint m_nodeIPEndpoint;
    std::shared_ptr<boost::beast::websocket::stream<ba::ssl::stream<bi::tcp::socket>>> m_wsSocket;
};

}  // namespace network
}  // namespace dev
