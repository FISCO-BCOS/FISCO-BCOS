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
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/router/RouterTableImpl.h"
#include "bcos-utilities/Timer.h"
#include <bcos-task/Task.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <array>
#include <shared_mutex>


namespace bcos::gateway
{
class Host;
class Session;
class P2PMessage;
class Gateway;

// The single concrete p2p service. The RIP-router behaviour (router-table sync, seq gossip,
// short/long p2pID mapping and multi-hop forwarding -- formerly ServiceV2) is selected at
// construction time by the (P2PInfo, io_context) constructor, mirroring the
// GatewayConfig::enable_rip_protocol option.
class Service : public std::enable_shared_from_this<Service>
{
public:
    Service(P2PInfo const& _p2pInfo);
    // RIP-router mode: the router seq timer is driven on the borrowed _ioContext.
    Service(P2PInfo const& _p2pInfo, boost::asio::io_context& _ioContext);
    virtual ~Service();

    using Ptr = std::shared_ptr<Service>;
    using MessageHandler =
        std::function<void(NetworkException, std::shared_ptr<P2PSession>, P2PMessage::Ptr)>;

    virtual void start();
    virtual void stop();
    void heartBeat();

    bool active();
    P2pID id() const;

    void onConnect(NetworkException e, P2PInfo const& p2pInfo, std::shared_ptr<Session> session);
    void onDisconnect(NetworkException e, P2PSession::Ptr p2pSession);
    void onMessage(NetworkException e, std::shared_ptr<Session> session, Message::Ptr message,
        std::weak_ptr<P2PSession> p2pSessionWeakPtr);

    std::optional<bcos::Error> onBeforeMessage(
        Session& _session, const Message& _message, uint32_t _wireLength);

    void registerUnreachableHandler(std::function<void(std::string)> _handler);

    void sendRespMessageBySession(
        bytesConstRef _payload, P2PMessage::Ptr _p2pMessage, P2PSession::Ptr _p2pSession);

    virtual task::Task<Message::Ptr> sendMessageByNodeID(P2pID nodeID, P2PMessage& header,
        ::ranges::any_view<bytesConstRef> payloads, Options options = Options());

    task::Task<void> sendMessageByNodeIDs(uint16_t _type, const std::vector<P2pID>& _nodeIDs,
        bcos::bytes _payload, Options options = Options());

    /**
     * @brief: (coroutine) broadcast a message to all connected sessions (or, in RIP-router mode,
     *         to all reachable nodes through the router table). The message is handed over as a
     *         shared_ptr: broadcastMessageToAll fans out one coroutine per peer and each task
     *         keeps the message alive (the payload rides as a view, zero-copy).
     */
    task::Task<void> broadcastMessageToAll(P2PMessage::Ptr message,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads,
        Options options = Options());

    /**
     * @brief: (coroutine) broadcast a message to the directly connected sessions only (m_sessions),
     *         without going through the router table. Used for router-table seq gossip which must
     *         only be exchanged between neighbors and propagated hop-by-hop. The message is handed
     *         over as a shared_ptr and kept alive by the per-peer fan-out tasks.
     */
    task::Task<void> broadcastMessageToNeighbors(P2PMessage::Ptr message,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads,
        Options options = Options());

    std::map<NodeIPEndpoint, P2pID> staticNodes();
    void setStaticNodes(const std::set<NodeIPEndpoint>& staticNodes);

    P2PInfos sessionInfos();  ///< Only connected node
    P2PInfo localP2pInfo();
    virtual bool isConnected(P2pID const& nodeID) const;
    virtual bool isReachable(P2pID const& _nodeID) const;

    std::shared_ptr<Host> host();
    void setHost(std::shared_ptr<Host> host);

    std::shared_ptr<MessageFactory> messageFactory();
    void setMessageFactory(std::shared_ptr<MessageFactory> _messageFactory);

    std::shared_ptr<bcos::crypto::KeyFactory> keyFactory();

    void setKeyFactory(std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory);
    void updateStaticNodes(std::shared_ptr<Socket> const& _s, P2pID const& nodeId);

    void registerDisconnectHandler(std::function<void(NetworkException, P2PSession::Ptr)> _handler);

    std::shared_ptr<P2PSession> getP2PSessionByNodeId(P2pID const& _nodeID) const
    {
        std::shared_lock lock(x_sessions);
        return getP2PSessionByNodeIdWithoutLock(_nodeID);
    }

    void setBeforeMessageHandler(std::function<std::optional<bcos::Error>(
        Session&, const Message&, uint32_t)> _handler);

    bool registerHandlerByMsgType(uint16_t _type, MessageHandler const& _msgHandler);

    MessageHandler getMessageHandlerByMsgType(uint16_t _type);

    void eraseHandlerByMsgType(uint16_t _type);

    void setOnMessageHandler(
        std::function<std::optional<bcos::Error>(std::shared_ptr<Session>, Message::Ptr)> _handler);

    void updatePeerBlacklist(const std::set<std::string>& _strList, const bool _enable);
    void updatePeerWhitelist(const std::set<std::string>& _strList, const bool _enable);

    std::string getShortP2pID(std::string const& rawP2pID) const;
    std::string getRawP2pID(std::string const& shortP2pID) const;

    void resetP2pID(P2PMessage&, bcos::protocol::ProtocolVersion const&);

protected:
    std::shared_ptr<P2PSession> getP2PSessionByNodeIdWithoutLock(P2pID const& _nodeID) const;

    // handshake protocol
    void asyncSendProtocol(P2PSession::Ptr _session);
    void onReceiveProtocol(
        NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message);
    void onReceiveHeartbeat(
        NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message);

    // handlers called when new-session
    void registerOnNewSession(std::function<void(P2PSession::Ptr)> _handler);
    // handlers called when delete-session
    void registerOnDeleteSession(std::function<void(P2PSession::Ptr)> _handler);

    void callNewSessionHandlers(const P2PSession::Ptr& _session);
    void callDeleteSessionHandlers(const P2PSession::Ptr& _session);

    // the direct (single-hop) send over an established session; the RIP forwarding path falls
    // back to it once the next hop is resolved
    task::Task<Message::Ptr> directSendMessageByNodeID(P2pID nodeID, P2PMessage& header,
        ::ranges::any_view<bytesConstRef> payloads, Options options);

    // RIP-router mode (formerly ServiceV2)
    // (coroutine) forward a received message to its destination through the router table. Unlike
    // sendMessageByNodeID it does NOT rewrite srcP2PNodeID: the original sender must be preserved
    // so the final destination can reply to it directly.
    task::Task<Message::Ptr> forwardMessageByNodeID(P2pID nodeID, P2PMessage& message,
        ::ranges::any_view<bytesConstRef> payloads, Options options = Options());

    void onReceivePeersRouterTable(
        NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message);
    void joinRouterTable(std::shared_ptr<P2PSession> _session, RouterTable::Ptr _routerTable);
    void onReceiveRouterTableRequest(
        NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message);
    virtual void broadcastRouterSeq();
    // FIB-186 (vector B): advance is done by the caller; this broadcasts the seq on the leading
    // edge of a membership/route-change burst and coalesces the rest (see m_routerSeqDirty).
    void markRouterSeqChanged();
    void onReceiveRouterSeq(
        NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message);

    void onNewSession(P2PSession::Ptr _session);
    void onEraseSession(P2PSession::Ptr _session);
    bool tryToUpdateSeq(std::string const& _p2pNodeID, uint32_t _seq);
    bool eraseSeq(std::string const& _p2pNodeID);

    void updateP2pInfo(P2PInfo const& p2pInfo);
    void tryToUpdateRawP2pInfo(P2PInfo const& p2pInfo);
    void tryToUpdateP2pInfo(P2PInfo const& p2pInfo);
    // called when the nodes become unreachable
    void onP2PNodesUnreachable(std::set<std::string> const& _p2pNodeIDs);

    using SessionsType = std::map<std::string, P2PSession::Ptr>;
    std::vector<std::function<void(NetworkException, P2PSession::Ptr)>> m_disconnectionHandlers;

    std::shared_ptr<bcos::crypto::KeyFactory> m_keyFactory;

    std::map<NodeIPEndpoint, P2pID> m_staticNodes;
    std::shared_mutex x_nodes;
    std::shared_ptr<Host> m_host;

    // long p2pID to session
    SessionsType m_sessions;
    mutable std::shared_mutex x_sessions;

    std::shared_ptr<MessageFactory> m_messageFactory;
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
    std::function<std::optional<bcos::Error>(std::shared_ptr<Session>, Message::Ptr)> m_onMessageHandler;

    // ---- RIP-router mode state (only used when m_enableRIPProtocol) ----
    bool m_enableRIPProtocol = false;
    // for message forward
    // Note: must use ptr here, for the timer uses enable_shared_from_this
    std::shared_ptr<bcos::Timer> m_routerTimer;
    std::atomic<uint32_t> m_statusSeq{1};
    // FIB-186 (vector B): coalesce router-seq broadcasts. The first membership/route change of a
    // burst broadcasts immediately (leading edge) and sets this flag; further changes within the
    // window only advance m_statusSeq, and the m_routerTimer flush resets the flag. This bounds the
    // broadcast fan-out so connection churn cannot cascade a full-mesh gossip storm on the PBFT
    // delivery pool. See markRouterSeqChanged().
    std::atomic_bool m_routerSeqDirty{false};

    RouterTable::Ptr m_routerTable;

    std::map<std::string, uint32_t> m_node2Seq;
    mutable SharedMutex x_node2Seq;

    // rawP2pID->p2pID
    std::map<std::string, std::string> m_rawP2pIDInfo;
    mutable SharedMutex x_rawP2pIDInfo;
    // p2pID->rawP2pID
    std::map<std::string, std::string> m_p2pIDInfo;
    mutable SharedMutex x_p2pIDInfo;

    const int c_unreachableDistance = 10;

    // called when the given node unreachable
    std::vector<std::function<void(std::string)>> m_unreachableHandlers;
    mutable SharedMutex x_unreachableHandlers;
};

}  // namespace bcos::gateway
