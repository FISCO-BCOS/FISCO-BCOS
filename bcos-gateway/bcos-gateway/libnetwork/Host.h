/** @file Host.h
 * @author monan <651932351@qq.com>
 * @date 2018
 */
#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-gateway/libnetwork/PeerBlackWhitelistInterface.h"
#include "bcos-gateway/libnetwork/SessionCallback.h"
#include "bcos-utilities/Common.h"
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>
#include <openssl/x509.h>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/system/error_code.hpp>
#include <memory>
#include <string>
#include <utility>


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
    Host(bcos::crypto::Hash::Ptr _hash, std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<SessionFactory> _sessionFactory, MessageFactory::Ptr _messageFactory);
    virtual ~Host();

    using Ptr = std::shared_ptr<Host>;

    virtual uint16_t listenPort() const;

    virtual void start();
    virtual void stop();

    virtual void asyncConnect(NodeIPEndpoint const& _nodeIPEndpoint,
        std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
            callback);

    virtual bool haveNetwork() const;

    virtual std::string listenHost() const;
    virtual void setHostPort(std::string host, uint16_t port);

    virtual std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
    connectionHandler() const;
    virtual void setConnectionHandler(
        std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
            connectionHandler);

    virtual std::function<bool(X509* x509, std::string& pubHex)> sslContextPubHandler();

    virtual void setSSLContextPubHandler(
        std::function<bool(X509* x509, std::string& pubHex)> _sslContextPubHandler);

    virtual std::function<bool(X509* x509, std::string& pubHex)>
    sslContextPubHandlerWithoutExtInfo();

    virtual void setSSLContextPubHandlerWithoutExtInfo(
        std::function<bool(X509* x509, std::string& pubHex)> _sslContextPubHandlerWithoutExtInfo);

    virtual void setSessionCallbackManager(
        SessionCallbackManagerInterface::Ptr sessionCallbackManager);

    virtual std::shared_ptr<ASIOInterface> asioInterface() const;
    virtual std::shared_ptr<SessionFactory> sessionFactory() const;
    virtual MessageFactory::Ptr messageFactory() const;
    virtual P2PInfo p2pInfo();

    virtual void setPeerBlacklist(PeerBlackWhitelistInterface::Ptr _peerBlacklist);
    virtual PeerBlackWhitelistInterface::Ptr peerBlacklist();
    virtual void setPeerWhitelist(PeerBlackWhitelistInterface::Ptr _peerWhitelist);
    virtual PeerBlackWhitelistInterface::Ptr peerWhitelist();

    template <class F>
    void asyncTo(F f)
    {
        m_asyncGroup.run(std::move(f));
    }

    void setEnableSslVerify(bool _enableSSLVerify);

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

    void erasePendingConns(NodeIPEndpoint const& nodeIPEndpoint);

    void insertPendingConns(NodeIPEndpoint const& nodeIPEndpoint);

    tbb::task_arena m_taskArena;
    tbb::task_group m_asyncGroup;
    std::shared_ptr<SessionCallbackManagerInterface> m_sessionCallbackManager;

    bcos::crypto::Hash::Ptr m_hashImpl;
    /// representing to the network state
    std::shared_ptr<ASIOInterface> m_asioInterface;
    std::shared_ptr<SessionFactory> m_sessionFactory;
    int m_connectTimeThre = 50000;

    std::set<NodeIPEndpoint> m_pendingConns;
    bcos::Mutex x_pendingConns;
    MessageFactory::Ptr m_messageFactory;

    std::string m_listenHost;
    uint16_t m_listenPort = 0;
    // enable ssl verify or not
    bool m_enableSSLVerify = true;

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
