/**
 * @brief: wrapper for boost::asio network operations
 *
 * @file AsioInterface.cpp
 * @author: bxq2011hust
 * @date 2019-07-244
 */
#include "bcos-network/ASIOInterface.h"
#include "Socket.h"
#include <chrono>

namespace ba = boost::asio;
namespace bi = ba::ip;
using namespace bcos;
using namespace bcos::network;

ASIOInterface::ASIOInterface(IOServicePool::Ptr _ioServicePool, std::string listenHost,
    uint16_t listenPort, bool listen)
  : m_ioServicePool(std::move(_ioServicePool)),
    // ONE pool lookup shared by the acceptor, the resolver and acceptorIoContext(): the pool
    // round-robins, so a second getIOService() call would bind a different context.
    m_acceptorIoService(m_ioServicePool->getIOService()),
    m_resolver(*m_acceptorIoService)
{
    if (listen)
    {
        // The endpoint constructor is what opens, binds AND listens. For a pure client it must not
        // run at all: that is the difference between "a client that happens to have an idle
        // acceptor" and "a client with an OS-assigned listening port nobody drains".
        boost::system::error_code ec;
        bi::tcp::endpoint endpoint(bi::make_address(listenHost, ec), listenPort);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                      "ASIOInterface: invalid listen address " + listenHost));
        }
        m_acceptor.emplace(*m_acceptorIoService);
        m_acceptor->open(endpoint.protocol(), ec);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                      "ASIOInterface: cannot open acceptor: " + ec.message()));
        }
        boost::asio::socket_base::reuse_address optionReuseAddress(true);
        m_acceptor->set_option(optionReuseAddress);
        m_acceptor->bind(endpoint, ec);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                      "ASIOInterface: cannot bind " + listenHost + ":" +
                                      std::to_string(listenPort) + ": " + ec.message()));
        }
        m_acceptor->listen(boost::asio::socket_base::max_listen_connections, ec);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                      "ASIOInterface: cannot listen on " + listenHost + ":" +
                                      std::to_string(listenPort) + ": " + ec.message()));
        }
    }
    // The read path needs no runtime seam: awaitableReadSome compiles against the default policy
    // (DefaultReadPolicy), whose invoke() dispatches async_read_some on the socket's stream() —
    // the TCP-vs-SSL choice is the compile-time stream type of the socket (see Socket.h).
}

ASIOInterface::~ASIOInterface() = default;

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

boost::asio::steady_timer ASIOInterface::newAcceptorTimer(uint32_t timeout)
{
    // The only caller is Host::acceptLoop, which only runs when listening.
    return boost::asio::steady_timer(
        m_acceptor->get_executor(), std::chrono::milliseconds(timeout));
}

bi::tcp::acceptor* ASIOInterface::acceptor()
{
    return m_acceptor ? &*m_acceptor : nullptr;
}
