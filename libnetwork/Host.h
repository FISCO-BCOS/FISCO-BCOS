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
/** @file Host.h
 * @author monan <651932351@qq.com>
 * @date 2018
 */
#pragma once

#include "Common.h"                       // for MessageFactory::Ptr, NodeIP...
#include "PeerWhitelist.h"                // for PeerWhitelist
#include <libdevcore/Guards.h>            // for Guard, Mutex
#include <boost/asio/deadline_timer.hpp>  // for deadline_timer
#include <boost/system/error_code.hpp>    // for error_code
#include <set>                            // for set
#include <string>                         // for string
#include <thread>                         // for thread
#include <utility>                        // for swap, move
#include <vector>                         // for vector

namespace boost
{
namespace asio
{
namespace ssl
{
class verify_context;
}
}  // namespace asio
}  // namespace boost
namespace dev
{
class ThreadPool;

namespace network
{
class SessionFactory;
class SessionFace;
class SocketFace;
class ASIOInterface;
class Host : public std::enable_shared_from_this<Host>
{
public:
    Host(){};
    virtual ~Host() { stop(); };

    typedef std::shared_ptr<Host> Ptr;

    virtual uint16_t listenPort() const { return m_listenPort; }

    virtual void start();
    virtual void stop();

    virtual void asyncConnect(NodeIPEndpoint const& _nodeIPEndpoint,
        std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)>
            callback);

    virtual bool haveNetwork() const { return m_run; }

    virtual std::string listenHost() const { return m_listenHost; }
    virtual void setHostPort(std::string host, uint16_t port)
    {
        m_listenHost = host;
        m_listenPort = port;
    }

    virtual std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)>
    connectionHandler() const
    {
        return m_connectionHandler;
    }
    virtual void setConnectionHandler(
        std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)>
            connectionHandler)
    {
        m_connectionHandler = connectionHandler;
    }

    virtual std::shared_ptr<dev::ThreadPool> threadPool() const { return m_threadPool; }
    virtual void setThreadPool(std::shared_ptr<dev::ThreadPool> threadPool)
    {
        m_threadPool = threadPool;
    }

    virtual std::shared_ptr<ASIOInterface> asioInterface() const { return m_asioInterface; }
    virtual void setASIOInterface(std::shared_ptr<ASIOInterface> asioInterface)
    {
        m_asioInterface = asioInterface;
    }

    virtual std::shared_ptr<SessionFactory> sessionFactory() const { return m_sessionFactory; }
    virtual void setSessionFactory(std::shared_ptr<SessionFactory> sessionFactory)
    {
        m_sessionFactory = sessionFactory;
    }

    virtual MessageFactory::Ptr messageFactory() const { return m_messageFactory; }
    virtual void setMessageFactory(MessageFactory::Ptr _messageFactory)
    {
        m_messageFactory = _messageFactory;
    }
    virtual void setCRL(std::vector<std::string> const& _certBlacklist)
    {
        m_certBlacklist = _certBlacklist;
    }
    virtual const std::vector<std::string>& certBlacklist() const { return m_certBlacklist; }

    virtual void setWhitelist(dev::PeerWhitelist::Ptr _whitelist) { m_whitelist = _whitelist; }
    virtual dev::PeerWhitelist::Ptr whitelist() { return m_whitelist; }
    virtual NodeInfo nodeInfo();


private:
    /// obtain the common name from the subject:
    /// the subject format is: /CN=xx/O=xxx/OU=xxx/ commonly
    std::string obtainCommonNameFromSubject(std::string const& subject);

    /// called by 'startedWorking' to accept connections
    void startAccept(boost::system::error_code ec = boost::system::error_code());
    /// functions called after openssl handshake,
    /// maily to get node id and verify whether the certificate has been expired
    /// @return: node id of the connected peer
    std::function<bool(bool, boost::asio::ssl::verify_context&)> newVerifyCallback(
        std::shared_ptr<std::string> nodeIDOut);

    /// obtain nodeInfo from given vector
    void obtainNodeInfo(NodeInfo& info, std::string const& node_info);

    /// server calls handshakeServer to after handshake, mainly calls RLPxHandshake to obtain
    /// informations(client version, caps, etc),start peer session and start accepting procedure
    /// repeatedly
    void handshakeServer(const boost::system::error_code& error,
        std::shared_ptr<std::string>& endpointPublicKey, std::shared_ptr<SocketFace> socket);

    void startPeerSession(NodeInfo const& nodeInfo, std::shared_ptr<SocketFace> const& socket,
        std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)>
            handler);

    void handshakeClient(const boost::system::error_code& error, std::shared_ptr<SocketFace> socket,
        std::shared_ptr<std::string>& endpointPublicKey,
        std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)>
            callback,
        NodeIPEndpoint _nodeIPEndpoint, std::shared_ptr<boost::asio::deadline_timer> timerPtr);

    void erasePendingConns(NodeIPEndpoint const& _nodeIPEndpoint)
    {
        Guard l(x_pendingConns);
        if (m_pendingConns.count(_nodeIPEndpoint))
            m_pendingConns.erase(_nodeIPEndpoint);
    }

    void insertPendingConns(NodeIPEndpoint const& _nodeIPEndpoint)
    {
        Guard l(x_pendingConns);
        if (!m_pendingConns.count(_nodeIPEndpoint))
            m_pendingConns.insert(_nodeIPEndpoint);
    }

    std::shared_ptr<dev::ThreadPool> m_threadPool;

    /// representing to the network state
    std::shared_ptr<ASIOInterface> m_asioInterface;
    std::shared_ptr<SessionFactory> m_sessionFactory;
    int m_connectTimeThre = 50000;
    std::set<NodeIPEndpoint> m_pendingConns;
    Mutex x_pendingConns;

    MessageFactory::Ptr m_messageFactory;

    std::string m_listenHost = "";
    uint16_t m_listenPort = 0;

    std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)>
        m_connectionHandler;

    bool m_run = false;

    std::shared_ptr<std::thread> m_hostThread;

    // certificate rejected list of nodeID
    std::vector<std::string> m_certBlacklist;

    NodeInfo m_nodeInfo;

    // certificate accepted list of nodeID
    dev::PeerWhitelist::Ptr m_whitelist;
};
}  // namespace network

}  // namespace dev
