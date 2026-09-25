/**
 * @brief: wrapper for boost::asio network operations
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
    return boost::asio::steady_timer(
        m_acceptor.get_executor(), std::chrono::milliseconds(timeout));
}

bi::tcp::acceptor* ASIOInterface::acceptor()
{
    return &m_acceptor;
}
