/**
 * @brief: inteface for boost::asio(for unittest)
 *
 * @file AsioInterface.cpp
 * @author: bxq2011hust
 * @date 2019-07-244
 */
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "Socket.h"
#include <chrono>

namespace ba = boost::asio;
namespace bi = ba::ip;
using namespace bcos;
using namespace bcos::gateway;
using namespace std;

Socket::Socket(std::shared_ptr<ba::io_context> _ioService, ba::ssl::context& _sslContext,
    NodeIPEndpoint _nodeIPEndpoint)
  : m_nodeIPEndpoint(std::move(_nodeIPEndpoint)),
    m_ioService(std::move(_ioService)),
    m_sslSocket(*m_ioService, _sslContext)
{}

Socket::~Socket()
{
    close();
}

bool Socket::isConnected() const
{
    return m_sslSocket.lowest_layer().is_open();
}

void Socket::close()
{
    try
    {
        boost::system::error_code ec;
        m_sslSocket.lowest_layer().shutdown(bi::tcp::socket::shutdown_both, ec);
        if (m_sslSocket.lowest_layer().is_open())
        {
            m_sslSocket.lowest_layer().close();
        }
    }
    catch (...)
    {}
}

bi::tcp::endpoint Socket::remoteEndpoint(boost::system::error_code ec)
{
    return m_sslSocket.lowest_layer().remote_endpoint(ec);
}

bi::tcp::endpoint Socket::localEndpoint(boost::system::error_code ec)
{
    return m_sslSocket.lowest_layer().local_endpoint(ec);
}

bi::tcp::socket& Socket::ref()
{
    return m_sslSocket.next_layer();
}

ba::ssl::stream<bi::tcp::socket>& Socket::sslref()
{
    return m_sslSocket;
}

const NodeIPEndpoint& Socket::nodeIPEndpoint() const
{
    return m_nodeIPEndpoint;
}

void Socket::setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint)
{
    m_nodeIPEndpoint = std::move(_nodeIPEndpoint);
}

ba::io_context& Socket::ioService()
{
    return *m_ioService;
}

ASIOInterface::ASIOInterface(
    IOServicePool::Ptr _ioServicePool, std::string listenHost, uint16_t listenPort)
  : m_ioServicePool(std::move(_ioServicePool)),
    m_acceptor(*m_ioServicePool->getIOService(),
        bi::tcp::endpoint(bi::make_address(listenHost), listenPort)),
    m_resolver(*m_ioServicePool->getIOService())
{
    boost::asio::socket_base::reuse_address optionReuseAddress(true);
    m_acceptor.set_option(optionReuseAddress);
}

ASIOInterface::~ASIOInterface() = default;

void ASIOInterface::setType(int type)
{
    m_type = type;
}

void ASIOInterface::setIOServicePool(IOServicePool::Ptr _ioServicePool)
{
    m_ioServicePool = std::move(_ioServicePool);
}

ba::ssl::context* ASIOInterface::srvContext()
{
    return m_srvContext.has_value() ? &*m_srvContext : nullptr;
}

ba::ssl::context* ASIOInterface::clientContext()
{
    return m_clientContext.has_value() ? &*m_clientContext : nullptr;
}

void ASIOInterface::setSrvContext(ba::ssl::context _srvContext)
{
    m_srvContext.emplace(std::move(_srvContext));
}

void ASIOInterface::setClientContext(ba::ssl::context _clientContext)
{
    m_clientContext.emplace(std::move(_clientContext));
}

boost::asio::steady_timer ASIOInterface::newTimer(uint32_t timeout)
{
    return boost::asio::steady_timer(
        *(m_ioServicePool->getIOService()), std::chrono::milliseconds(timeout));
}

std::shared_ptr<SocketFace> ASIOInterface::newSocket(bool _server, NodeIPEndpoint nodeIPEndpoint)
{
    std::shared_ptr<SocketFace> socket = std::make_shared<Socket>(m_ioServicePool->getIOService(),
        _server ? *m_srvContext : *m_clientContext, nodeIPEndpoint);
    return socket;
}

bi::tcp::acceptor* ASIOInterface::acceptor()
{
    return &m_acceptor;
}

void ASIOInterface::asyncAccept(
    const std::shared_ptr<SocketFace>& socket, Handler_Type handler, boost::system::error_code)
{
    m_acceptor.async_accept(socket->ref(), std::move(handler));
}

void ASIOInterface::asyncRead(const std::shared_ptr<SocketFace>& socket,
    boost::asio::mutable_buffer buffers, ReadWriteHandler handler)
{
    switch (m_type)
    {
    case TCP_ONLY:
    {
        ba::async_read(socket->ref(), buffers, std::move(handler));
        break;
    }
    case SSL:
    {
        ba::async_read(socket->sslref(), buffers, std::move(handler));
        break;
    }
    }
}

void ASIOInterface::asyncReadSome(const std::shared_ptr<SocketFace>& socket,
    boost::asio::mutable_buffer buffers, ReadWriteHandler handler)
{
    switch (m_type)
    {
    case TCP_ONLY:
    {
        socket->ref().async_read_some(buffers, std::move(handler));
        break;
    }
    case SSL:
    {
        socket->sslref().async_read_some(buffers, std::move(handler));
        break;
    }
    }
}

void ASIOInterface::asyncHandshake(const std::shared_ptr<SocketFace>& socket,
    ba::ssl::stream_base::handshake_type type, Handler_Type handler)
{
    socket->sslref().async_handshake(type, std::move(handler));
}

void ASIOInterface::setVerifyCallback(
    const std::shared_ptr<SocketFace>& socket, VerifyCallback callback, bool)
{
    socket->sslref().set_verify_callback(std::move(callback));
}

void ASIOInterface::asyncResolveConnect(
    const std::shared_ptr<SocketFace>& socket, Handler_Type handler)
{
    auto protocol = socket->nodeIPEndpoint().isIPv6() ? bi::tcp::tcp::v6() : bi::tcp::tcp::v4();
    m_resolver.async_resolve(protocol, socket->nodeIPEndpoint().address(),
        to_string(socket->nodeIPEndpoint().port()),
        [=](const boost::system::error_code& ec, bi::tcp::resolver::results_type results) {
            if (ec || results.empty())
            {
                ASIO_LOG(WARNING) << LOG_DESC("asyncResolve failed")
                                  << LOG_KV("host", socket->nodeIPEndpoint().address())
                                  << LOG_KV("port", socket->nodeIPEndpoint().port());
                return;
            }

            // results is a iterator, but only use first endpoint.
            auto it = results.begin();
            socket->ref().async_connect(it->endpoint(), handler);
            ASIO_LOG(INFO) << LOG_DESC("asyncResolveConnect") << LOG_KV("endpoint", it->endpoint());
        });
}
