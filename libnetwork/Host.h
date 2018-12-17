/*
        This file is part of FISCO-BCOS

        cpp-ethereum is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        (at your option) any later version.

        cpp-ethereum is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Host.h
 * @author monan <651932351@qq.com>
 * @date 2018
 */
#pragma once

#include <libdevcore/Guards.h>
#include <libdevcore/ThreadPool.h>
#include <libdevcore/Worker.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/ECDHE.h>
#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "ASIOInterface.h"
#include "Common.h"
#include "Session.h"
#include "SessionFace.h"
#include "Socket.h"

namespace dev
{
namespace p2p
{
class Host : public std::enable_shared_from_this<Host>
{
public:
    Host(){};
    virtual ~Host();

    typedef std::shared_ptr<Host> Ptr;

    virtual uint16_t listenPort() const { return m_listenPort; }

    virtual void start();
    virtual void stop();

    virtual void asyncConnect(NodeIPEndpoint const& _nodeIPEndpoint,
        std::function<void(NetworkException, NodeID, std::shared_ptr<SessionFace>)> callback);

    virtual bool haveNetwork() const { return m_run; }

    virtual std::string listenHost() { return m_listenHost; }
    virtual uint16_t listenPort() { return m_listenPort; }
    virtual void setHostPort(std::string host, uint16_t port)
    {
        m_listenHost = host;
        m_listenPort = port;
    }

    virtual std::function<void(NetworkException, NodeID, std::shared_ptr<SessionFace>)>
    connectionHandler()
    {
        return m_connectionHandler;
    }
    virtual void setConnectionHandler(
        std::function<void(NetworkException, NodeID, std::shared_ptr<SessionFace>)>
            connectionHandler)
    {
        m_connectionHandler = connectionHandler;
    }

    virtual std::shared_ptr<dev::ThreadPool> threadPool() { return m_threadPool; }
    virtual void setThreadPool(std::shared_ptr<dev::ThreadPool> threadPool)
    {
        m_threadPool = threadPool;
    }

    virtual std::shared_ptr<ASIOInterface> asioInterface() { return m_asioInterface; }
    virtual void setASIOInterface(std::shared_ptr<ASIOInterface> asioInterface)
    {
        m_asioInterface = asioInterface;
    }

    virtual std::shared_ptr<SessionFactory> sessionFactory() { return m_sessionFactory; }
    virtual void setSessionFactory(std::shared_ptr<SessionFactory> sessionFactory)
    {
        m_sessionFactory = sessionFactory;
    }

    virtual MessageFactory::Ptr messageFactory() { return m_messageFactory; }
    virtual void setMessageFactory(MessageFactory::Ptr _messageFactory)
    {
        m_messageFactory = _messageFactory;
    }
    bi::tcp::endpoint tcpClient() const { return m_tcpClient; }

    virtual void setCRL(std::vector<std::string> const& crl) { m_crl = crl; }
    virtual const std::vector<std::string>& crl() const { return m_crl; }

private:
    /// called by 'startedWorking' to accept connections
    void startAccept(boost::system::error_code ec = boost::system::error_code());
    /// functions called after openssl handshake,
    /// maily to get node id and verify whether the certificate has been expired
    /// @return: node id of the connected peer
    std::function<bool(bool, boost::asio::ssl::verify_context&)> newVerifyCallback(
        std::shared_ptr<std::string> nodeIDOut);
    /// server calls handshakeServer to after handshake, mainly calls RLPxHandshake to obtain
    /// informations(client version, caps, etc),start peer session and start accepting procedure
    /// repeatedly
    void handshakeServer(const boost::system::error_code& error,
        std::shared_ptr<std::string>& endpointPublicKey, std::shared_ptr<SocketFace> socket);

    void startPeerSession(NodeID nodeID, std::shared_ptr<SocketFace> const& socket,
        std::function<void(NetworkException, NodeID, std::shared_ptr<SessionFace>)> handler);

    void handshakeClient(const boost::system::error_code& error, std::shared_ptr<SocketFace> socket,
        std::shared_ptr<std::string>& endpointPublicKey,
        std::function<void(NetworkException, NodeID, std::shared_ptr<SessionFace>)> callback,
        NodeIPEndpoint _nodeIPEndpoint);

    std::shared_ptr<dev::ThreadPool> m_threadPool;

    /// representing to the network state
    std::shared_ptr<ASIOInterface> m_asioInterface;
    std::shared_ptr<SessionFactory> m_sessionFactory;

    MessageFactory::Ptr m_messageFactory;

    std::string m_listenHost = "";
    uint16_t m_listenPort = 0;

    // ip and port information of the connected peer
    bi::tcp::endpoint m_tcpClient;

    std::function<void(NetworkException, NodeID, std::shared_ptr<SessionFace>)> m_connectionHandler;

    Mutex x_runTimer;
    bool m_run = false;

    std::shared_ptr<std::thread> m_hostThread;

    // certificate rejected list of nodeID
    std::vector<std::string> m_crl;
};
}  // namespace p2p

}  // namespace dev
