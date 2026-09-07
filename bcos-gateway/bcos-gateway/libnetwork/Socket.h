/** @file Socket.h
 * @ author: yujiechen
 * @ date: 2018-09-17
 * @ modification: rename RLPXSocket.h to Socket.h
 */

#pragma once

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-utilities/BoostLog.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/filesystem.hpp>


namespace bcos::gateway
{
class Socket : public std::enable_shared_from_this<Socket>
{
public:
    using Ptr = std::shared_ptr<Socket>;

    Socket(std::shared_ptr<ba::io_context> _ioService, ba::ssl::context& _sslContext,
        NodeIPEndpoint _nodeIPEndpoint);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket(Socket&&) = delete;
    Socket& operator=(Socket&&) = delete;
    Socket& operator=(const Socket&) = delete;

    bool isConnected() const;

    void close();

    bi::tcp::endpoint remoteEndpoint(boost::system::error_code ec = boost::system::error_code());

    bi::tcp::endpoint localEndpoint(boost::system::error_code ec = boost::system::error_code());

    bi::tcp::socket& ref();
    ba::ssl::stream<bi::tcp::socket>& sslref();

    const NodeIPEndpoint& nodeIPEndpoint() const;
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint);

    ba::io_context& ioService();

protected:
    NodeIPEndpoint m_nodeIPEndpoint;
    std::shared_ptr<ba::io_context> m_ioService;
    ba::ssl::stream<bi::tcp::socket> m_sslSocket;
};

}  // namespace bcos::gateway
