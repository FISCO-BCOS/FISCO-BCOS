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
/**
 * @file: ChannelServer.h
 * @author: monan
 *
 * @date: 2018
 */

#pragma once

#include <libdevcore/FixedHash.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <queue>
#include <string>
#include <thread>

#include "ChannelException.h"
#include "ChannelSession.h"
#include "Message.h"
#include "libdevcore/ThreadPool.h"

namespace dev
{
namespace channel
{
class ChannelServer : public std::enable_shared_from_this<ChannelServer>
{
public:
    typedef std::shared_ptr<ChannelServer> Ptr;
    virtual ~ChannelServer() = default;
    virtual void run();

    void onAccept(const boost::system::error_code& error, ChannelSession::Ptr session);

    void startAccept();

    void setBind(const std::string& host, int port)
    {
        m_listenHost = host;
        m_listenPort = port;
    };

    void setEnableSSL(bool enableSSL) { m_enableSSL = enableSSL; };

    void setConnectionHandler(
        std::function<void(dev::channel::ChannelException, ChannelSession::Ptr)> handler)
    {
        m_connectionHandler = handler;
    };

    void setIOService(std::shared_ptr<boost::asio::io_service> ioService)
    {
        m_ioService = ioService;
    };
    void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext)
    {
        m_sslContext = sslContext;
    };

    MessageFactory::Ptr messageFactory() { return m_messageFactory; }
    void setMessageFactory(MessageFactory::Ptr messageFactory)
    {
        m_messageFactory = messageFactory;
    }

    virtual void stop();

private:
    void onHandshake(const boost::system::error_code& error, ChannelSession::Ptr session);

    std::function<bool(bool, boost::asio::ssl::verify_context&)> newVerifyCallback();

    std::shared_ptr<boost::asio::io_service> m_ioService;
    std::shared_ptr<boost::asio::ssl::context> m_sslContext;

    std::shared_ptr<std::thread> m_serverThread;

    std::shared_ptr<boost::asio::ip::tcp::acceptor> m_acceptor;
    ThreadPool::Ptr m_requestThreadPool;
    ThreadPool::Ptr m_responseThreadPool;

    std::function<void(dev::channel::ChannelException, ChannelSession::Ptr)> m_connectionHandler;
    MessageFactory::Ptr m_messageFactory;

    std::string m_listenHost = "";
    int m_listenPort = 0;
    bool m_enableSSL = false;
    std::string m_certIssuerName = "";
};

}  // namespace channel

}  // namespace dev
