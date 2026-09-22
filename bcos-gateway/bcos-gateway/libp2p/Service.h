/** @file Service.h
 *  @author monan
 *  @modify first draft
 *  @date 20180910
 *  @author chaychen
 *  @modify realize encode and decode, add timeout, code format
 *  @date 20180911
 */

#pragma once
#include "bcos-crypto/interfaces/crypto/KeyFactory.h"
#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-framework/protocol/ProtocolInfoCodec.h"
#include "bcos-gateway/libp2p/P2PDecoder.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include <bcos-task/Task.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <range/v3/view/any_view.hpp>
#include <array>
#include <memory>
#include <optional>
#include <shared_mutex>


namespace bcos::gateway
{
class Gateway;
class RouterTableFactory;
class RouterTableInterface;

class Service : public std::enable_shared_from_this<Service>
{
public:
    using MessageHandler =
        std::function<void(NetworkException, std::shared_ptr<P2PSession>, Message)>;

    // _routerTableFactory != nullptr enables the optional router (RIP) module (merged from the
    // former ServiceV2 subclass); _ioContext then drives the router-seq sync timer.
    Service(P2PInfo const& _p2pInfo,
        std::shared_ptr<RouterTableFactory> _routerTableFactory = nullptr,
        boost::asio::io_context* _ioContext = nullptr);
    virtual ~Service();

    using Ptr = std::shared_ptr<Service>;

    virtual void start();
    virtual void stop();
    virtual void heartBeat();

    virtual bool active();
    virtual P2pID id() const;

    virtual void onConnect(
        NetworkException e, P2PInfo const& p2pInfo, Session::Ptr session);
    virtual void onDisconnect(NetworkException e, P2PSession::Ptr p2pSession);
    virtual void onMessage(NetworkException e, Session::Ptr session, Message message,
        std::weak_ptr<P2PSession> p2pSessionWeakPtr);

    virtual std::optional<bcos::Error> onBeforeMessage(
        Session& _session, const Message& _message, uint32_t _wireLength);

    virtual void registerUnreachableHandler(std::function<void(std::string)> /*unused*/);

    virtual void sendRespMessageBySession(bytesConstRef _payload, uint32_t _requestSeq,
        std::string _requestSrcP2PNodeID, P2PSession::Ptr _p2pSession);

    virtual task::Task<std::optional<Message>> sendMessageByNodeID(P2pID nodeID, Message& header,
        ::ranges::any_view<bytesConstRef> payloads, Options options = Options());

    virtual task::Task<void> sendMessageByNodeIDs(uint16_t _type,
        const std::vector<P2pID>& _nodeIDs, bcos::bytes _payload, Options options = Options());

    /**
     * @brief: (coroutine) broadcast a message to all connected sessions. The message is handed
     *         over as a shared_ptr: broadcastMessageToAll fans out one coroutine per peer and each
     *         task keeps the message alive (the payload rides as a view, zero-copy).
     */
    virtual task::Task<void> broadcastMessageToAll(Message::Ptr message,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads,
        Options options = Options());

    /**
     * @brief: (coroutine) broadcast a message to the directly connected sessions only (m_sessions),
     *         without going through the router table. Used for router-table seq gossip which must
     *         only be exchanged between neighbors and propagated hop-by-hop. The message is handed
     *         over as a shared_ptr and kept alive by the per-peer fan-out tasks.
     */
    virtual task::Task<void> broadcastMessageToNeighbors(Message::Ptr message,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads,
        Options options = Options());

    virtual std::map<NodeIPEndpoint, P2pID> staticNodes();
    virtual void setStaticNodes(const std::set<NodeIPEndpoint>& staticNodes);

    virtual P2PInfos sessionInfos();  ///< Only connected node
    virtual P2PInfo localP2pInfo();
    virtual bool isConnected(P2pID const& nodeID) const;
    virtual bool isReachable(P2pID const& _nodeID) const;

    P2PHost::Ptr host();
    virtual void setHost(P2PHost::Ptr host);

    virtual uint32_t newSeq();

    std::shared_ptr<bcos::crypto::KeyFactory> keyFactory();

    void setKeyFactory(std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory);
    void updateStaticNodes(std::shared_ptr<Socket> const& _s, P2pID const& nodeId);

    void registerDisconnectHandler(std::function<void(NetworkException, P2PSession::Ptr)> _handler);

    virtual std::shared_ptr<P2PSession> getP2PSessionByNodeId(P2pID const& _nodeID) const
    {
        std::shared_lock lock(x_sessions);
        return getP2PSessionByNodeIdWithoutLock(_nodeID);
    }

    void setBeforeMessageHandler(std::function<std::optional<bcos::Error>(
        Session&, const Message&, uint32_t)> _handler);

    // Outbound payload compression policy (was a per-session flag in libnetwork; the wire-format
    // work moved up to P2PSession, so the flag lives here now).
    bool enableCompress() const { return m_enableCompress; }
    void setEnableCompress(bool _enableCompress) { m_enableCompress = _enableCompress; }

    virtual bool registerHandlerByMsgType(uint16_t _type, MessageHandler const& _msgHandler);

    MessageHandler getMessageHandlerByMsgType(uint16_t _type);

    virtual void eraseHandlerByMsgType(uint16_t _type);

    void setOnMessageHandler(
        std::function<std::optional<bcos::Error>(Session::Ptr, const Message&)> _handler);

    virtual void updatePeerBlacklist(const std::set<std::string>& _strList, const bool _enable);
    virtual void updatePeerWhitelist(const std::set<std::string>& _strList, const bool _enable);

    virtual std::string getShortP2pID(std::string const& rawP2pID) const;
    virtual std::string getRawP2pID(std::string const& shortP2pID) const;

    virtual void resetP2pID(Message&, bcos::protocol::ProtocolVersion const&);

protected:
    std::shared_ptr<P2PSession> getP2PSessionByNodeIdWithoutLock(P2pID const& _nodeID) const;

    // handshake protocol
    void sendProtocol(P2PSession::Ptr _session);
    void onReceiveProtocol(
        NetworkException _error, std::shared_ptr<P2PSession> _session, const Message& _message);
    void onReceiveHeartbeat(
        NetworkException _error, std::shared_ptr<P2PSession> _session, const Message& _message);

    // handlers called when new-session
    void registerOnNewSession(std::function<void(P2PSession::Ptr)> _handler);
    // handlers called when delete-session
    void registerOnDeleteSession(std::function<void(P2PSession::Ptr)> _handler);

    virtual void callNewSessionHandlers(const P2PSession::Ptr& _session);
    virtual void callDeleteSessionHandlers(const P2PSession::Ptr& _session);

private:
    // Optional router (RIP) module, present only when constructed with a RouterTableFactory
    // (merged from the former ServiceV2 subclass). All router state lives behind the RouterState
    // pimpl so Service.h carries no router/timer headers; definitions in ServiceRouter.cpp.
    void initRouter(std::shared_ptr<RouterTableFactory> _routerTableFactory,
        boost::asio::io_context& _ioContext);

    // (coroutine) forward a received message to its destination through the router table. Unlike
    // sendMessageByNodeID it does NOT rewrite srcP2PNodeID: the original sender must be preserved
    // so the final destination can reply to it directly.
    task::Task<std::optional<Message>> forwardMessageByNodeID(P2pID nodeID, Message& message,
        ::ranges::any_view<bytesConstRef> payloads, Options options = Options());
    // the direct-send path shared by the router-less sendMessageByNodeID and by the router
    // module's last-hop/forward fallback (split out so the fallback cannot recurse back into the
    // router path)
    task::Task<std::optional<Message>> directSendMessageByNodeID(P2pID nodeID, Message& message,
        ::ranges::any_view<bytesConstRef> payloads, Options options = Options());

    void onReceivePeersRouterTable(
        NetworkException _error, std::shared_ptr<P2PSession> _session, const Message& _message);
    void joinRouterTable(std::shared_ptr<P2PSession> _session,
        std::shared_ptr<RouterTableInterface> _routerTable);
    void onReceiveRouterTableRequest(
        NetworkException _error, std::shared_ptr<P2PSession> _session, const Message& _message);
    void broadcastRouterSeq();
    // FIB-186 (vector B): advance is done by the caller; this broadcasts the seq on the leading
    // edge of a membership/route-change burst and coalesces the rest (see
    // RouterState::routerSeqDirty).
    void markRouterSeqChanged();
    void onReceiveRouterSeq(
        NetworkException _error, std::shared_ptr<P2PSession> _session, const Message& _message);
    void onNewSession(P2PSession::Ptr _session);
    void onEraseSession(P2PSession::Ptr _session);
    bool tryToUpdateSeq(std::string const& _p2pNodeID, uint32_t _seq);
    bool eraseSeq(std::string const& _p2pNodeID);
    // called when the nodes become unreachable
    void onP2PNodesUnreachable(std::set<std::string> const& _p2pNodeIDs);
    void updateP2pInfo(P2PInfo const& p2pInfo);
    void tryToUpdateRawP2pInfo(P2PInfo const& p2pInfo);
    void tryToUpdateP2pInfo(P2PInfo const& p2pInfo);

    struct RouterState;
    std::unique_ptr<RouterState> m_router;

protected:
    using SessionsType = std::map<std::string, P2PSession::Ptr>;
    std::vector<std::function<void(NetworkException, P2PSession::Ptr)>> m_disconnectionHandlers;

    std::shared_ptr<bcos::crypto::KeyFactory> m_keyFactory;

    std::map<NodeIPEndpoint, P2pID> m_staticNodes;
    std::shared_mutex x_nodes;
    P2PHost::Ptr m_host;

    // long p2pID to session
    SessionsType m_sessions;
    mutable std::shared_mutex x_sessions;

    P2PInfo m_selfInfo;
    P2pID m_nodeID;
    std::optional<boost::asio::steady_timer> m_timer;
    bool m_run = false;

    std::array<MessageHandler, bcos::gateway::GatewayMessageType::All> m_msgHandlers{};

    // the local protocol
    bcos::protocol::ProtocolInfo::ConstPtr m_localProtocol;
    bcos::protocol::ProtocolInfoCodec::ConstPtr m_codec;

    // handlers called when new-session
    std::vector<std::function<void(P2PSession::Ptr)>> m_newSessionHandlers;
    // handlers called when delete-session
    std::vector<std::function<void(P2PSession::Ptr)>> m_deleteSessionHandlers;

    std::function<std::optional<bcos::Error>(
        Session&, const Message&, uint32_t)> m_beforeMessageHandler;
    std::function<std::optional<bcos::Error>(Session::Ptr, const Message&)>
        m_onMessageHandler;

    bool m_enableCompress = false;
};

}  // namespace bcos::gateway
