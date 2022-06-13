/**
 * @brief: Socket inteface
 * @file SocketFace.h
 * @author yujiechen
 * @date 2018-09-17
 */

#pragma once
#include <bcos-gateway/libnetwork/Common.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast.hpp>

namespace bcos
{
namespace gateway
{
class SocketFace
{
public:
    SocketFace() = default;

    virtual ~SocketFace(){};
    virtual bool isConnected() const = 0;
    virtual void close() = 0;
    virtual bi::tcp::endpoint remoteEndpoint(
        boost::system::error_code ec = boost::system::error_code()) = 0;
    virtual bi::tcp::endpoint localEndpoint(
        boost::system::error_code ec = boost::system::error_code()) = 0;

    virtual bi::tcp::socket& ref() = 0;
    virtual ba::ssl::stream<bi::tcp::socket>& sslref() = 0;

    virtual const NodeIPEndpoint& nodeIPEndpoint() const = 0;
    virtual void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) = 0;
    virtual std::shared_ptr<ba::io_context> ioService() { return nullptr; }
};
}  // namespace gateway
}  // namespace bcos
