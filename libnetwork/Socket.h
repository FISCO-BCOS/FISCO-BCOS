/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
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

#include <libdevcore/FileSystem.h>
#include <libdevcore/easylog.h>
#include <openssl/ec.h>
#include <openssl/ssl.h>
#include <boost/filesystem.hpp>
#include "Common.h"
#include "SocketFace.h"

using namespace dev::eth;
using namespace dev::p2p;

namespace dev
{
namespace p2p
{
class Socket : public SocketFace, public std::enable_shared_from_this<Socket>
{
public:
    Socket(
        ba::io_service& _ioService, ba::ssl::context& _sslContext, NodeIPEndpoint _nodeIPEndpoint)
      : m_nodeIPEndpoint(_nodeIPEndpoint)
    {
        try
        {
            m_sslSocket =
                std::make_shared<ba::ssl::stream<bi::tcp::socket> >(_ioService, _sslContext);
        }
        catch (Exception const& _e)
        {
            LOG(ERROR) << "ERROR: " << diagnostic_information(_e);
            LOG(ERROR) << "Ssl Socket Init Fail! Please Check CERTIFICATE!";
        }
        LOG(INFO) << "CERTIFICATE LOAD SUC!";
    }
    ~Socket() { close(); }

    virtual bool isConnected() const { return m_sslSocket->lowest_layer().is_open(); }

    virtual void close()
    {
        try
        {
            boost::system::error_code ec;
            m_sslSocket->lowest_layer().shutdown(bi::tcp::socket::shutdown_both, ec);
            if (m_sslSocket->lowest_layer().is_open())
                m_sslSocket->lowest_layer().close();
        }
        catch (...)
        {
        }
    }

    virtual bi::tcp::endpoint remoteEndpoint()
    {
        boost::system::error_code ec;
        return m_sslSocket->lowest_layer().remote_endpoint(ec);
    }

    virtual bi::tcp::socket& ref() { return m_sslSocket->next_layer(); }
    virtual ba::ssl::stream<bi::tcp::socket>& sslref() { return *m_sslSocket; }

    virtual const NodeIPEndpoint& nodeIPEndpoint() const { return m_nodeIPEndpoint; }
    virtual void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) { m_nodeIPEndpoint = _nodeIPEndpoint; }
    virtual boost::asio::ip::tcp::endpoint remote_endpoint() { return ref().remote_endpoint(); }

protected:
    NodeIPEndpoint m_nodeIPEndpoint;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket> > m_sslSocket;
};

}  // namespace p2p
}  // namespace dev
