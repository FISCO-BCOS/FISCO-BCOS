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
class P2PPeerIdentity;

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

    // p2pInfo is null when the handshake produced no peer identity; onConnect drops such peers
    virtual void onConnect(
        NetworkException e, std::shared_ptr<P2PInfo> p2pInfo, Session::Ptr session);
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

    // The cert black/white lists live in the P2P identity object (libp2p/P2PIdentity.h), not in
    // the generic Host; updatePeerBlacklist/updatePeerWhitelist update them through this pointer.
    void setPeerIdentity(std::shared_ptr<P2PPeerIdentity> _peerIdentity);

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
    // so the final destination can reply to it directly. Defined at the bottom of
    // ServiceRouter.h: the body touches the RouterState pimpl, which only that header completes.
    template <::ranges::input_range Payloads>
        requires std::convertible_to<::ranges::range_reference_t<Payloads>, bytesConstRef>
    task::Task<std::optional<Message>> forwardMessageByNodeID(
        P2pID nodeID, Message& message, Payloads payloads, Options options = Options());
    // the direct-send path shared by the router-less sendMessageByNodeID and by the router
    // module's last-hop/forward fallback (split out so the fallback cannot recurse back into the
    // router path). Defined at the bottom of this header, next to fastSendP2PMessage.
    template <::ranges::input_range Payloads>
        requires std::convertible_to<::ranges::range_reference_t<Payloads>, bytesConstRef>
    task::Task<std::optional<Message>> directSendMessageByNodeID(
        P2pID nodeID, Message& message, Payloads payloads, Options options = Options());

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
    // cert black/white-list admission lists (owned by the P2P identity object); null in tests
    // that never wire one — updatePeerBlacklist/Whitelist then skip the list update
    std::shared_ptr<P2PPeerIdentity> m_peerIdentity;

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

// ---------------------------------------------------------------------------
// Template send-path definitions. They live at the bottom of this header — after both Service
// and P2PSession are complete — because P2PSession::fastSendP2PMessage drives Service
// (compression policy, resetP2pID, the pre-send rate-limit hook) while Service's own send
// template drives P2PSession; the declarations sit in the classes' headers. The payloads
// parameters are plain ranges (no any_view type erasure): every call site instantiates the view
// type it actually has, and the per-element access compiles down to direct reads.
// (Service::forwardMessageByNodeID is defined at the bottom of ServiceRouter.h instead — its
// body touches the RouterState pimpl, which only that header completes.)
#include "bcos-gateway/libp2p/Common.h"
#include "bcos-utilities/ZstdCompress.h"
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <range/v3/view/all.hpp>
#include <range/v3/view/single.hpp>
#include <utility>

namespace bcos::gateway
{
template <::ranges::input_range Payloads>
    requires std::convertible_to<::ranges::range_reference_t<Payloads>, bytesConstRef>
task::Task<std::optional<Message>> P2PSession::fastSendP2PMessage(
    Message& message, Payloads payloads, Options options)
{
    if (!m_session || !m_session->active()) [[unlikely]]
    {
        P2PSESSION_LOG(WARNING) << LOG_DESC("fastSendP2PMessage failed for invalid session")
                                << LOG_KV("from", message.printSrcP2PNodeID())
                                << LOG_KV("dst", message.printDstP2PNodeID());
        co_return {};
    }
    auto service = m_service.lock();
    if (!service)
    {
        co_return {};
    }
    // reset message using original long nodeID or short nodeID according to the protocol version
    // Note: m_protocolInfo be setted when create P2PSession
    service->resetP2pID(message, (bcos::protocol::ProtocolVersion)m_protocolInfo->version());
    // the p2p message version must match the negotiated protocol version of this session: the
    // encodeHeaderImpl of Message only encodes the ttl/src/dst routing fields for version > V0,
    // so sending with the default (V0) version would silently drop the V2 routing fields and break
    // multi-hop forwarding through the router tables
    message.setVersion((uint16_t)m_protocolInfo->version());

    // Materialize the payload views once: the input range may be single-pass, while the
    // size/join passes below each iterate the payloads.
    boost::container::small_vector<bytesConstRef, 3> payloadRefs;
    uint32_t payloadSize = 0;
    for (auto const& payloadRef : payloads)
    {
        payloadSize += payloadRef.size();
        payloadRefs.push_back(payloadRef);
    }

    // The wire-format work that used to live in libnetwork's Session (compression, header
    // encoding, length stamping, outgoing rate limiting) is collected here: the session now
    // sends pure bytes and knows nothing about the P2P message format. Compression and its
    // COMPRESS flag are decided per send and applied to the wire header only — the caller's
    // message is never mutated, so a shared message (broadcast fan-out) cannot leak the flag
    // to a peer that receives an uncompressed frame.
    bcos::bytes joinedPayload;
    bcos::bytes compressedPayload;
    uint16_t wireExt = message.ext();
    bool hasWirePayloadOverride = false;
    bytesConstRef wirePayloadOverride;
    if (service->enableCompress() && message.compressionSupported() &&
        payloadSize > c_compressThreshold)
    {
        joinedPayload.reserve(payloadSize);
        for (auto const& payloadRef : payloadRefs)
        {
            joinedPayload.insert(joinedPayload.end(), payloadRef.begin(), payloadRef.end());
        }
        if (ZstdCompress::compress(
                ref(joinedPayload), compressedPayload, (int)c_zstdCompressLevel))
        {
            wireExt |= Message::COMPRESS_EXT_FLAG;
            wirePayloadOverride = ref(std::as_const(compressedPayload));
        }
        else
        {
            wirePayloadOverride = ref(std::as_const(joinedPayload));
        }
        hasWirePayloadOverride = true;
    }

    bytes headerBuffer;
    if (!message.encodeHeaderWithExt(headerBuffer, wireExt)) [[unlikely]]
    {
        // e.g. P2PMessageOptions::encode failed (empty/oversized src/dst IDs). Sending a frame
        // whose header claims "has options" while the options are missing would make the peer
        // drop the connection instead of the message.
        BOOST_THROW_EXCEPTION(makeNetworkException(-1, "encode header failed"));
    }
    uint32_t totalLength = static_cast<uint32_t>(headerBuffer.size()) +
                           (hasWirePayloadOverride ? wirePayloadOverride.size() : payloadSize);
    Message::stampLength(headerBuffer, totalLength);

    // The fast path must honour the same pre-send (outgoing rate-limit) check that the callback
    // path (asyncSendMessage) enforces; judged on the ACTUAL wire bytes (a zero-copy message does
    // not carry its payload, so message.length() alone would under-count the outgoing traffic).
    // A rejection surfaces as a thrown NetworkException (e.g. OutBWOverflow / InQPSOverflow) so
    // coroutine retry loops can stop.
    if (auto result = service->onBeforeMessage(*m_session, message, totalLength))
    {
        const auto& error = result.value();
        BOOST_THROW_EXCEPTION(makeNetworkException((int64_t)error.errorCode(), error.errorMessage()));
    }

    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        P2PSESSION_LOG(TRACE) << LOG_DESC("P2PSession fastSendP2PMessage")
                              << LOG_KV("endpoint", m_session->nodeIPEndpoint())
                              << LOG_KV("seq", message.seq())
                              << LOG_KV("packetType", message.packetType())
                              << LOG_KV("ext", wireExt) << LOG_KV("wireLength", totalLength);
    }

    // headerBuffer / joinedPayload / compressedPayload live in this coroutine frame; the co_await
    // keeps them alive until the session's write no longer references the views.
    std::optional<FrameMeta> response;
    if (hasWirePayloadOverride)
    {
        response = co_await m_session->fastSendMessage(
            ref(headerBuffer), ::ranges::views::single(wirePayloadOverride), message.seq(),
            options);
    }
    else
    {
        response = co_await m_session->fastSendMessage(
            ref(headerBuffer), ::ranges::views::all(payloadRefs), message.seq(), options);
    }
    if (!response)
    {
        co_return std::nullopt;
    }
    // Decode the response frame back into a Message (the session delivers raw frames now).
    Message respMessage;
    if (respMessage.decode(ref(response->frame)) < 0) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(makeNetworkException(
            P2PExceptionType::ProtocolError, "ProtocolError(decode response message error)"));
    }
    co_return respMessage;
}

template <::ranges::input_range Payloads>
    requires std::convertible_to<::ranges::range_reference_t<Payloads>, bytesConstRef>
task::Task<std::optional<Message>> Service::directSendMessageByNodeID(
    P2pID nodeID, Message& header, Payloads payloads, Options options)
{
    if (nodeID == id())
    {
        co_return {};
    }

    auto session = getP2PSessionByNodeId(nodeID);
    if (!session || !session->active())
    {
        BOOST_THROW_EXCEPTION(
            makeNetworkException(-1, "send message failed for no network established"));
    }
    if (header.seq() == 0)
    {
        header.setSeq(newSeq());
    }

    co_return co_await session->fastSendP2PMessage(header, std::move(payloads), options);
}
}  // namespace bcos::gateway
