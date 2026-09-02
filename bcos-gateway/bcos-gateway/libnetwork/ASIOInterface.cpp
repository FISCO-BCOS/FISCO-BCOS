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
    // The read path needs no runtime seam: awaitableReadSome compiles against the default policy
    // (DefaultReadPolicy), whose invoke() directly dispatches async_read_some on the socket
    // (TCP vs SSL by m_type) — see ASIOInterface.h.
}

ASIOInterface::~ASIOInterface() = default;

void ASIOInterface::setType(int type)
{
    m_type = type;
}

ba::ssl::context* ASIOInterface::srvContext()
{
    return m_srvContext.has_value() ? &*m_srvContext : nullptr;
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

boost::asio::steady_timer ASIOInterface::newAcceptorTimer(uint32_t timeout)
{
    return boost::asio::steady_timer(
        m_acceptor.get_executor(), std::chrono::milliseconds(timeout));
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

void ASIOInterface::setVerifyCallback(
    const std::shared_ptr<SocketFace>& socket, VerifyCallback callback, bool /*unused*/)
{
    socket->sslref().set_verify_callback(std::move(callback));
}
