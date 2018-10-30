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
#include <boost/asio/strand.hpp>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>
#include "AsioInterface.h"
#include "Common.h"
#include "Network.h"
#include "P2pFactory.h"
#include "Peer.h"
#include "SessionFace.h"
#include "Socket.h"

namespace ba = boost::asio;
namespace bi = ba::ip;

namespace dev
{
namespace p2p
{
class Host: public std::enable_shared_from_this<Host>
{
public:
    Host();
    virtual ~Host();

    virtual NodeID id() const { return m_alias.pub(); }
    virtual uint16_t listenPort() const { return m_listenPort; }

    virtual void start(boost::system::error_code const& error);
    virtual void stop();

    /// get the network status
    virtual bool haveNetwork() const { return m_run; }

    virtual std::shared_ptr<dev::ThreadPool> threadPool() { return m_threadPool; }
    virtual void setThreadPool(std::shared_ptr<dev::ThreadPool> threadPool) { m_threadPool = threadPool; }

    virtual std::shared_ptr<ba::io_service> ioService() { return m_ioService; }
    virtual void setIOService(std::shared_ptr<ba::io_service> ioService) { m_ioService = ioService; }

    virtual std::shared_ptr<ba::io_context::strand> strand() { return m_strand; }
    virtual void setStrand(virtual std::shared_ptr<ba::io_context::strand> strand) { m_strand = strand; }

    virtual std::shared_ptr<bi::tcp::acceptor> acceptor() { return m_acceptor; }
    virtual void setAcceptor(std::shared_ptr<bi::tcp::acceptor> acceptor) { m_acceptor = acceptor; }

    virtual std::shared_ptr<AsioInterface>  asioInterface() { return m_asioInterface; }
    virtual void setASIOInterface(std::shared_ptr<AsioInterface> asioInterface) { m_asioInterface = asioInterface; }

    virtual std::shared_ptr<SocketFactory> socketFactory() { return m_socketFactory; }
    virtual void setSocketFactory(std::shared_ptr<SocketFactory> socketFactory) { m_socketFactory = socketFactory; }

    virtual std::shared_ptr<SessionFactory> sessionFactory() { return m_sessionFactory; }
    virtual void setSessionFactory(std::shared_ptr<SessionFactory> sessionFactory) { m_sessionFactory = sessionFactory; }

    virtual std::shared_ptr<ba::ssl::context> sslContext() { return m_sslContext; }
    void void setSSLContext(std::shared_ptr<ba::ssl::context> sslContext) { m_sslContext = sslContext; }

    virtual MessageFactory::Ptr messageFactory() { return m_messageFactory; }
    virtual void setMessageFactory(MessageFactory::Ptr _messageFactory) { m_messageFactory = _messageFactory; }

    virtual KeyPair keyPair() { return m_alias; }
    virtual void setKeyPair(KeyPair keyPair) { m_alias = keyPair; }

    virtual std::string listenHost() { return m_listenHost; }
    virtual uint16_t listenPort() { return m_listenPort; }
    virtual void setHostPort(std::string host, uint16_t port) { m_listenHost = host; m_listenPort = port; }

    virtual std::function<void(boost::system::error_code, std::shared_ptr<SessionFace>)> connectionHandler() { return m_connectionHandler; }
    virtual void setConnectionHandler(std::function<void(boost::system::error_code, std::shared_ptr<SessionFace>)> connectionHandler) { m_connectionHandler = connectionHandler; }

protected:
    /// called by 'startedWorking' to accept connections
    virtual void startAccept(boost::system::error_code ec = boost::system::error_code());
    /// functions called after openssl handshake,
    /// maily to get node id and verify whether the certificate has been expired
    /// @return: node id of the connected peer
    virtual std::function<bool(bool, boost::asio::ssl::verify_context&)> newVerifyCallback(
        std::shared_ptr<std::string> nodeIDOut);
    /// @return true: the given certificate has been expired
    /// @return false: the given certificate has not been expired
    bool isExpired(ba::ssl::verify_context& ctx);
    /// server calls handshakeServer to after handshake, mainly calls RLPxHandshake to obtain
    /// informations(client version, caps, etc),start peer session and start accepting procedure
    /// repeatedly
    virtual void handshakeServer(const boost::system::error_code& error,
        std::shared_ptr<std::string>& endpointPublicKey, std::shared_ptr<SocketFace> socket);

    virtual void startPeerSession(NodeID nodeID,
        std::shared_ptr<SocketFace> const& socket,
        std::function<void(boost::system::error_code, std::shared_ptr<SessionFace>)> handler);

    virtual void asyncConnect(
        NodeIPEndpoint const& _nodeIPEndpoint,
        std::function<void(boost::system::error_code, std::shared_ptr<SessionFace>)> callback,
        boost::system::error_code ec = boost::system::error_code()
    );

    virtual void handshakeClient(const boost::system::error_code& error,
        std::shared_ptr<SocketFace> socket,
        std::shared_ptr<std::string>& endpointPublicKey,
        std::function<void(boost::system::error_code, std::shared_ptr<SessionFace>)> callback,
        NodeIPEndpoint& _nodeIPEndpoint);

    std::shared_ptr<dev::ThreadPool> m_threadPool;
    /// I/O handler
    std::shared_ptr<ba::io_service> m_ioService;
    std::shared_ptr<ba::io_context::strand > m_strand;
    std::shared_ptr<bi::tcp::acceptor> m_acceptor;

    /// representing to the network state
    std::shared_ptr<AsioInterface> m_asioInterface;
    std::shared_ptr<SocketFactory> m_socketFactory;
    std::shared_ptr<SessionFactory> m_sessionFactory;
    std::shared_ptr<ba::ssl::context> m_sslContext;
    MessageFactory::Ptr m_messageFactory;

    KeyPair m_alias;
    std::string m_listenHost = "";
    uint16_t m_listenPort = 0;

    std::function<void(boost::system::error_code, std::shared_ptr<SessionFace>)> m_connectionHandler;

    Mutex x_runTimer;
    bool m_run = false;

    std::shared_ptr<boost::asio::deadline_timer> m_timer;

    std::set<std::string> m_pendingPeerConns;
    Mutex x_pendingNodeConns;
};
}  // namespace p2p

}  // namespace dev
