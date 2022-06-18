/** @file Socket.h
 * @ author: yujiechen
 * @ date: 2018-09-17
 * @ modification: rename RLPXSocket.h to Socket.h
 */

#pragma once

#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libnetwork/SocketFace.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/filesystem.hpp>

namespace bcos
{
namespace gateway
{
class Socket : public SocketFace, public std::enable_shared_from_this<Socket>
{
public:
    Socket(std::shared_ptr<ba::io_context> _ioService, ba::ssl::context& _sslContext,
        NodeIPEndpoint _nodeIPEndpoint)
      : m_nodeIPEndpoint(_nodeIPEndpoint), m_ioService(_ioService)
    {
        try
        {
            m_sslSocket =
                std::make_shared<ba::ssl::stream<bi::tcp::socket>>(*_ioService, _sslContext);
        }
        catch (const std::exception& _e)
        {
            SESSION_LOG(ERROR) << "ERROR: " << boost::diagnostic_information(_e);
            SESSION_LOG(ERROR) << "Ssl Socket Init Fail! Please Check CERTIFICATE!";
        }
    }
    ~Socket() { close(); }

    bool isConnected() const override { return m_sslSocket->lowest_layer().is_open(); }

    void close() override
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

    bi::tcp::endpoint remoteEndpoint(
        boost::system::error_code ec = boost::system::error_code()) override
    {
        return m_sslSocket->lowest_layer().remote_endpoint(ec);
    }

    bi::tcp::endpoint localEndpoint(
        boost::system::error_code ec = boost::system::error_code()) override
    {
        return m_sslSocket->lowest_layer().local_endpoint(ec);
    }

    bi::tcp::socket& ref() override { return m_sslSocket->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() override { return *m_sslSocket; }

    const NodeIPEndpoint& nodeIPEndpoint() const override { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) override
    {
        m_nodeIPEndpoint = _nodeIPEndpoint;
    }

    std::shared_ptr<ba::io_context> ioService() override { return m_ioService; }

protected:
    NodeIPEndpoint m_nodeIPEndpoint;
    std::shared_ptr<ba::io_context> m_ioService;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
};

}  // namespace gateway
}  // namespace bcos
