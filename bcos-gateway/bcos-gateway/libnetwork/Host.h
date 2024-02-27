/** @file Host.h
 * @author monan <651932351@qq.com>
 * @date 2018
 */
#pragma once

#include "bcos-gateway/libnetwork/SessionCallback.h"
#include "bcos-utilities/ThreadPool.h"
#include <bcos-gateway/libnetwork/Common.h>   // for  NodeIP...
#include <bcos-gateway/libnetwork/Message.h>  // for Message
#include <bcos-gateway/libnetwork/PeerBlacklist.h>
#include <bcos-gateway/libnetwork/PeerWhitelist.h>
#include <bcos-utilities/Common.h>  // for Guard, Mutex
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>
#include <openssl/x509.h>
#include <boost/asio/deadline_timer.hpp>  // for deadline_timer
#include <boost/system/error_code.hpp>    // for error_code
#include <memory>
#include <set>      // for set
#include <string>   // for string
#include <thread>   // for thread
#include <utility>  // for swap, move
#include <vector>   // for vector


namespace boost::asio::ssl
{
class verify_context;
}  // namespace boost::asio::ssl
namespace bcos
{
class ThreadPool;

namespace gateway
{
class SessionFactory;
class SessionFace;
class SocketFace;
class ASIOInterface;

using x509PubHandler = std::function<bool(X509* x509, std::string& pubHex)>;

class Host : public std::enable_shared_from_this<Host>
{
public:
    Host(const Host&) = delete;
    Host(Host&&) = delete;
    Host& operator=(const Host&) = delete;
    Host& operator=(Host&&) = delete;
    Host(std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<SessionFactory> _sessionFactory, MessageFactory::Ptr _messageFactory)
      : m_asioInterface(std::move(_asioInterface)),
        m_sessionFactory(std::move(_sessionFactory)),
        m_messageFactory(std::move(_messageFactory)){};
    virtual ~Host() { stop(); };

    using Ptr = std::shared_ptr<Host>;

    virtual uint16_t listenPort() const { return m_listenPort; }

    virtual void start();
    virtual void stop();

    virtual void asyncConnect(NodeIPEndpoint const& _nodeIPEndpoint,
        std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
            callback);

    virtual bool haveNetwork() const { return m_run; }

    virtual std::string listenHost() const { return m_listenHost; }
    virtual void setHostPort(std::string host, uint16_t port)
    {
        m_listenHost = std::move(host);
        m_listenPort = port;
    }

    virtual std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
    connectionHandler() const
    {
        return m_connectionHandler;
    }
    virtual void setConnectionHandler(
        std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
            connectionHandler)
    {
        m_connectionHandler = std::move(connectionHandler);
    }

    virtual std::function<bool(X509* x509, std::string& pubHex)> sslContextPubHandler()
    {
        return m_sslContextPubHandler;
    }

    virtual void setSSLContextPubHandler(
        std::function<bool(X509* x509, std::string& pubHex)> _sslContextPubHandler)
    {
        m_sslContextPubHandler = std::move(_sslContextPubHandler);
    }

    virtual std::function<bool(X509* x509, std::string& pubHex)>
    sslContextPubHandlerWithoutExtInfo()
    {
        return m_sslContextPubHandlerWithoutExtInfo;
    }

    virtual void setSSLContextPubHandlerWithoutExtInfo(
        std::function<bool(X509* x509, std::string& pubHex)> _sslContextPubHandlerWithoutExtInfo)
    {
        m_sslContextPubHandlerWithoutExtInfo = std::move(_sslContextPubHandlerWithoutExtInfo);
    }

    virtual void setSessionCallbackManager(
        SessionCallbackManagerInterface::Ptr sessionCallbackManager)
    {
        m_sessionCallbackManager = std::move(sessionCallbackManager);
    }

    virtual std::shared_ptr<ASIOInterface> asioInterface() const { return m_asioInterface; }
    virtual std::shared_ptr<SessionFactory> sessionFactory() const { return m_sessionFactory; }
    virtual MessageFactory::Ptr messageFactory() const { return m_messageFactory; }
    virtual P2PInfo p2pInfo();

    virtual void setPeerBlacklist(PeerBlackWhitelistInterface::Ptr _peerBlacklist)
    {
        m_peerBlacklist = std::move(_peerBlacklist);
    }
    virtual PeerBlackWhitelistInterface::Ptr peerBlacklist() { return m_peerBlacklist; }
    virtual void setPeerWhitelist(PeerBlackWhitelistInterface::Ptr _peerWhitelist)
    {
        m_peerWhitelist = std::move(_peerWhitelist);
    }
    virtual PeerBlackWhitelistInterface::Ptr peerWhitelist() { return m_peerWhitelist; }

    template <class F>
    void asyncTo(F f)
    {
        m_asyncGroup.template run(std::move(f));
    }

protected:
    /// obtain the common name from the subject:
    /// the subject format is: /CN=xx/O=xxx/OU=xxx/ commonly
    std::string obtainCommonNameFromSubject(std::string const& subject);

    /// called by 'startedWorking' to accept connections
    void startAccept(boost::system::error_code error = boost::system::error_code());
    /// functions called after openssl handshake,
    /// maily to get node id and verify whether the certificate has been expired
    /// @return: node id of the connected peer
    std::function<bool(bool, boost::asio::ssl::verify_context&)> newVerifyCallback(
        std::shared_ptr<std::string> nodeIDOut);

    /// obtain nodeInfo from given vector
    void obtainNodeInfo(P2PInfo& info, std::string const& node_info);

    /// server calls handshakeServer to after handshake, mainly calls
    /// RLPxHandshake to obtain informations(client version, caps, etc),start peer
    /// session and start accepting procedure repeatedly
    void handshakeServer(const boost::system::error_code& error,
        std::shared_ptr<std::string> endpointPublicKey, std::shared_ptr<SocketFace> socket);

    void startPeerSession(P2PInfo const& p2pInfo, std::shared_ptr<SocketFace> const& socket,
        std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
            handler);

    void handshakeClient(const boost::system::error_code& error, std::shared_ptr<SocketFace> socket,
        std::shared_ptr<std::string> endpointPublicKey,
        std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
            callback,
        NodeIPEndpoint _nodeIPEndpoint, std::shared_ptr<boost::asio::deadline_timer> timerPtr);

    void erasePendingConns(NodeIPEndpoint const& nodeIPEndpoint)
    {
        bcos::Guard lock(x_pendingConns);
        auto it = m_pendingConns.find(nodeIPEndpoint);
        if (it != m_pendingConns.end())
        {
            m_pendingConns.erase(it);
        }
    }

    void insertPendingConns(NodeIPEndpoint const& nodeIPEndpoint)
    {
        bcos::Guard lock(x_pendingConns);
        auto it = m_pendingConns.lower_bound(nodeIPEndpoint);
        if (it == m_pendingConns.end() || *it != nodeIPEndpoint)
        {
            m_pendingConns.emplace_hint(it, nodeIPEndpoint);
        }
    }

    tbb::task_arena m_taskArena;
    tbb::task_group m_asyncGroup;
    std::shared_ptr<SessionCallbackManagerInterface> m_sessionCallbackManager;

    /// representing to the network state
    std::shared_ptr<ASIOInterface> m_asioInterface;
    std::shared_ptr<SessionFactory> m_sessionFactory;
    int m_connectTimeThre = 50000;
    std::set<NodeIPEndpoint> m_pendingConns;
    bcos::Mutex x_pendingConns;
    MessageFactory::Ptr m_messageFactory;

    std::string m_listenHost;
    uint16_t m_listenPort = 0;

    std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
        m_connectionHandler;

    // get the hex public key of the peer from the the SSL connection
    std::function<bool(X509* x509, std::string& pubHex)> m_sslContextPubHandler;
    std::function<bool(X509* x509, std::string& pubHex)> m_sslContextPubHandlerWithoutExtInfo;

    bool m_run = false;

    P2PInfo m_p2pInfo;

    // Peer black list
    PeerBlackWhitelistInterface::Ptr m_peerBlacklist{nullptr};
    PeerBlackWhitelistInterface::Ptr m_peerWhitelist{nullptr};
};
}  // namespace gateway

}  // namespace bcos
