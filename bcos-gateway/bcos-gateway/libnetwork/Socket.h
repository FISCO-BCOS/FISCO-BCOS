/** @file Socket.h
 * @ author: yujiechen
 * @ date: 2018-09-17
 * @ modification: rename RLPXSocket.h to Socket.h
 */

#pragma once

#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/SocketFace.h"
#include "bcos-utilities/BoostLog.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/filesystem.hpp>


namespace bcos::gateway
{
class Socket : public SocketFace, public std::enable_shared_from_this<Socket>
{
public:
    Socket(std::shared_ptr<ba::io_context> _ioService, ba::ssl::context& _sslContext,
        NodeIPEndpoint _nodeIPEndpoint);
    ~Socket();

    bool isConnected() const override;

    void close() override;

    bi::tcp::endpoint remoteEndpoint(
        boost::system::error_code ec = boost::system::error_code()) override;

    bi::tcp::endpoint localEndpoint(
        boost::system::error_code ec = boost::system::error_code()) override;

    bi::tcp::socket& ref() override;
    ba::ssl::stream<bi::tcp::socket>& sslref() override;

    const NodeIPEndpoint& nodeIPEndpoint() const override;
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) override;

    ba::io_context& ioService() override;

protected:
    NodeIPEndpoint m_nodeIPEndpoint;
    std::shared_ptr<ba::io_context> m_ioService;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
};

}  // namespace bcos::gateway
