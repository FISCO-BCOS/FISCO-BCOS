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
#include "bcos-gateway/libp2p/PendingResponse.h"
#include <bcos-task/Channel.h>
#include <bcos-task/OneShot.h>
#include <bcos-task/Task.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <range/v3/view/any_view.hpp>
#include <shared_mutex>
#include <vector>


namespace bcos::gateway
{
class Gateway;
class RouterTableFactory;
class RouterTableInterface;
class P2PPeerIdentity;

// One inbound message delivered to a subscribe() consumer: the decoded message plus the
// session it arrived on (needed to reply / identify the peer).
struct InboundMessage
{
    P2PSession::Ptr session;
    Message message;
};
using InboundChannel = task::Channel<InboundMessage>;

// The byte measure the inbound channel charges for a queued message: the heap the message
// actually retains (see Message::ownedStorageBytes), not its plaintext length. Used both as the
// channel's sizeOf at construction and as the waitWritable() argument on the push side — the two
// must agree, which is why it exists as one function.
inline std::size_t inboundMessageBytes(const InboundMessage& inbound)
{
    return inbound.message.ownedStorageBytes();
}

class Service : public std::enable_shared_from_this<Service>
{
public:
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
    virtual bcos::network::P2pID id() const;

    // p2pInfo is null when the handshake produced no peer identity; onConnect drops such peers
    virtual void onConnect(
        bcos::network::NetworkException e, std::shared_ptr<P2PInfo> p2pInfo, Session::Ptr session);
    virtual void onDisconnect(bcos::network::NetworkException e, P2PSession::Ptr p2pSession);
    // Coroutine because the per-type inbound channel back-pressures: a full channel parks the
    // caller (a session's receive pump) in waitWritable() instead of dropping the message, so the
    // dispatch must be awaitable. The park ends when the type's pump drains space, or with false
    // once stop() closes the channel or the message can never fit the byte budget.
    virtual task::Task<void> onMessage(bcos::network::NetworkException e, Session::Ptr session,
        Message message, std::weak_ptr<P2PSession> p2pSessionWeakPtr);

    virtual std::optional<bcos::Error> onBeforeMessage(
        Session& _session, const Message& _message, uint32_t _wireLength);

    virtual void registerUnreachableHandler(std::function<void(std::string)> /*unused*/);

    // Reply on the session a request arrived on, as a COROUTINE the caller co_awaits.
    //
    // This used to be `void` and detached a task::wait frame per response, because the old
    // void-returning MessageHandler had nowhere to await it; the caller also had to hand over a
    // `bytes` copy so the frame owned the payload. Now the caller's handler is a coroutine, so it
    // awaits this directly: no detached frame, and the payload is MOVED into the response message
    // instead of being copied once more on the way in.
    //
    // `_payload` is taken by value deliberately: it must stay valid across the send's suspension,
    // and the rate-limited caller passes a temporary std::string. A caller that can guarantee its
    // buffer outlives the await (an inbound frame held by the awaiting frame) may pass a converted
    // copy or hoist the buffer; nothing here retains a view past the send.
    virtual task::Task<void> sendRespMessageBySession(bcos::bytes _payload, uint32_t _requestSeq,
        std::string _requestSrcP2PNodeID, P2PSession::Ptr _p2pSession);

    virtual task::Task<std::optional<Message>> sendMessageByNodeID(bcos::network::P2pID nodeID,
        Message& header, ::ranges::any_view<bytesConstRef> payloads,
        bcos::network::Options options = bcos::network::Options());

    virtual task::Task<void> sendMessageByNodeIDs(uint16_t _type,
        const std::vector<bcos::network::P2pID>& _nodeIDs, bcos::bytes _payload,
        bcos::network::Options options = bcos::network::Options());

    /**
     * @brief: (coroutine) broadcast a message to all connected sessions. The message is handed
     *         over as a shared_ptr: broadcastMessageToAll fans out one coroutine per peer and each
     *         task keeps the message alive (the payload rides as a view, zero-copy).
     */
    virtual task::Task<void> broadcastMessageToAll(Message::Ptr message,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads,
        bcos::network::Options options = bcos::network::Options());

    /**
     * @brief: (coroutine) broadcast a message to the directly connected sessions only (m_sessions),
     *         without going through the router table. Used for router-table seq gossip which must
     *         only be exchanged between neighbors and propagated hop-by-hop. The message is handed
     *         over as a shared_ptr and kept alive by the per-peer fan-out tasks.
     */
    virtual task::Task<void> broadcastMessageToNeighbors(Message::Ptr message,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads,
        bcos::network::Options options = bcos::network::Options());

    virtual std::map<NodeIPEndpoint, bcos::network::P2pID> staticNodes();
    virtual void setStaticNodes(const std::set<NodeIPEndpoint>& staticNodes);

    virtual P2PInfos sessionInfos();  ///< Only connected node
    virtual P2PInfo localP2pInfo();
    virtual bool isConnected(bcos::network::P2pID const& nodeID) const;
    virtual bool isReachable(bcos::network::P2pID const& _nodeID) const;

    P2PHost::Ptr host();
    virtual void setHost(P2PHost::Ptr host);

    // The cert black/white lists live in the P2P identity object (libp2p/P2PIdentity.h), not in
    // the generic Host; updatePeerBlacklist/updatePeerWhitelist update them through this pointer.
    void setPeerIdentity(std::shared_ptr<P2PPeerIdentity> _peerIdentity);

    // Node-wide message seq allocator (was Host::newSeq before correlation moved up from
    // libnetwork). Seqs key m_pendingResponses, whose scope is this whole node — a routed
    // response can arrive on any session — so the allocator is Service-wide and monotonic.
    virtual uint32_t newSeq();

    // --- request/response correlation (the pending-request table m_pendingResponses) ---
    // P2PSession::fastSendP2PMessage registers before sending; the receive path claims.
    bool registerPendingResponse(uint32_t _seq, PendingResponse::Ptr _pending);
    // Atomically erase and return the entry (nullptr when already settled) — the exactly-once
    // primitive for ack / timeout / disconnect-flush / stop-flush / write-failure reclaim.
    PendingResponse::Ptr claimPendingResponse(uint32_t _seq);
    // Timeout timer handler: fail the waiter with NetworkTimeout when its seq is still pending.
    void onResponseTimeout(uint32_t _seq);
    // Settle a response frame addressed to this node (dispatched from the session message
    // handler after Message::peekResponseFrameInfo). Completes the waiter inline — the handler
    // already runs on a posted pool task, matching the old claimResponse threading.
    void onResponseFrame(
        uint32_t _seq, Session::Ptr const& _session, bcos::network::FrameMeta _meta);
    // Fail every pending request whose outbound session is the given one (session drop: the old
    // Session::drop flush, moved up with the correlation table). A request is failed when its
    // OUTBOUND session dies even though a routed response could in principle arrive on another
    // session — preserving the old fail-fast-on-drop semantics.
    void failPendingResponsesOf(
        Session::Ptr const& _session, bcos::network::NetworkException _error);
    // Fail every pending request (Service::stop: no waiter may outlive the service).
    void failAllPendingResponses(bcos::network::NetworkException _error);

private:
    // Shared settlement for the two flushes: cancel each entry's timeout and complete its
    // waiter. Completions are POSTED to the shared pool while the network is up — drop() is
    // reachable from a sender's own await_suspend, where resuming the waiter inline is UB, and
    // onDisconnect runs on the dedicated single-thread teardown executor (FIB-186 vector D),
    // which a burst of waiter continuations must not occupy — and run inline once the host is
    // gone (a posted task would never run then; the old postCallback fallback).
    void settleClaimedPending(
        std::vector<PendingResponse::Ptr> _pendings, bcos::network::NetworkException const& _error);

public:
    std::shared_ptr<bcos::crypto::KeyFactory> keyFactory();

    void setKeyFactory(std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory);
    void updateStaticNodes(
        std::shared_ptr<bcos::network::Socket> const& _s, bcos::network::P2pID const& nodeId);

    void registerDisconnectHandler(
        std::function<void(bcos::network::NetworkException, P2PSession::Ptr)> _handler);

    virtual std::shared_ptr<P2PSession> getP2PSessionByNodeId(
        bcos::network::P2pID const& _nodeID) const
    {
        std::shared_lock lock(x_sessions);
        return getP2PSessionByNodeIdWithoutLock(_nodeID);
    }

    void setBeforeMessageHandler(
        std::function<std::optional<bcos::Error>(Session&, const Message&, uint32_t)> _handler);

    // Outbound payload compression policy (was a per-session flag in libnetwork; the wire-format
    // work moved up to P2PSession, so the flag lives here now).
    bool enableCompress() const { return m_enableCompress; }
    void setEnableCompress(bool _enableCompress) { m_enableCompress = _enableCompress; }

    // Default per-type inbound queue depth for subscribe().
    constexpr static std::size_t DEFAULT_INBOUND_QUEUE_SIZE = 1024;
    // Per-type inbound queued-heap budget, independently measured by inboundMessageBytes. Large
    // frames reduce the usable count below 1024 by design. The 96MB budget covers the upstream
    // 64MB frame-storage bound plus decoded options / node metadata; compressed decoding releases
    // its wire storage, so every successfully decoded production message can fit this budget.
    constexpr static std::size_t DEFAULT_INBOUND_QUEUE_BYTES = 96 * 1024 * 1024;

    // Pull-mode inbound subscription: one bounded channel per message type. The receive pump of
    // every session pushes matching inbound messages here; the consumer drains the channel with
    // `co_await channel->recv()` and chooses its own execution context through `poster` (where
    // the parked consumer resumes). A full channel PARKS the pushing receive pump in
    // waitWritable() — back-pressure that propagates through the session's recv queue to the TCP
    // window, never a silent drop. Channels are closed by stop(), which wakes the consumer's
    // recv() with a NetworkException and every parked producer's waitWritable() with false.
    std::shared_ptr<InboundChannel> subscribe(uint16_t _type, InboundChannel::Poster _poster,
        std::size_t _capacity = DEFAULT_INBOUND_QUEUE_SIZE);
    // The channel-registry half of subscribe(), split out as a virtual seam: subscribeHandler() is
    // deliberately NON-virtual (it takes a CoroutineHandler and starts a detached pump, so a test
    // cannot usefully intercept the whole call), while a test that only wants the channel — or that
    // must observe registration without running a pump — can override this. Production behaviour is
    // unchanged: subscribe() forwards here.
    virtual std::shared_ptr<InboundChannel> subscribeChannel(uint16_t _type,
        InboundChannel::Poster _poster, std::size_t _capacity = DEFAULT_INBOUND_QUEUE_SIZE);
    // The channel currently subscribed for _type, nullptr when none (lookup only, no creation).
    std::shared_ptr<InboundChannel> inboundChannel(uint16_t _type) const;

    // A per-type inbound handler as a COROUTINE. This is the shape every inbound message type uses:
    // the old `MessageHandler` returned void, so a handler that had to reply (the common case)
    // could not co_await its own send — it had to detach a coroutine per message and copy the
    // payload into it. Returning a Task lets the handler co_await its reply, which removes the
    // detached frame and the copy, and makes the handler's lifetime the channel pump's.
    using CoroutineHandler = std::function<task::Task<bool>(InboundMessage&&)>;

    // Subscribe `_type` and run `_handler` on every inbound message of that type, in arrival
    // order, on the shared pool (the poster). The pump is started once per type and torn down by
    // stop(), which closes the channels.
    //
    // The handler returns false to STOP the pump (used by the router's terminal paths); true (or a
    // normal return) continues. A handler that throws is logged and the pump continues: one bad
    // message must not kill the type's dispatch (the detached-task shape this replaces lost the
    // exception into whichever thread had resumed it).
    void subscribeHandler(uint16_t _type, CoroutineHandler _handler,
        std::size_t _capacity = DEFAULT_INBOUND_QUEUE_SIZE);

    // The pump body behind subscribeHandler, public so tests can drive a single step.
    task::Task<void> handlerPump(
        uint16_t _type, std::shared_ptr<InboundChannel> _channel, CoroutineHandler _handler);

    void setOnMessageHandler(
        std::function<std::optional<bcos::Error>(Session::Ptr, const Message&)> _handler);

    // The router (RIP) module's inbound entry points. The constructor (initRouter) wires them to
    // the RouterTableSyncSeq / RouterTableResponse / RouterTableRequest channels, so production
    // reaches them through the pump; public so tests can drive one message at a time (they used to
    // fetch the same functions out of the legacy handler table).
    void onReceivePeersRouterTable(std::shared_ptr<P2PSession> _session, const Message& _message);
    void onReceiveRouterTableRequest(std::shared_ptr<P2PSession> _session, const Message& _message);
    void onReceiveRouterSeq(std::shared_ptr<P2PSession> _session, const Message& _message);

    virtual void updatePeerBlacklist(const std::set<std::string>& _strList, const bool _enable);
    virtual void updatePeerWhitelist(const std::set<std::string>& _strList, const bool _enable);

    virtual std::string getShortP2pID(std::string const& rawP2pID) const;
    virtual std::string getRawP2pID(std::string const& shortP2pID) const;

    virtual void resetP2pID(Message&, bcos::protocol::ProtocolVersion const&);

protected:
    std::shared_ptr<P2PSession> getP2PSessionByNodeIdWithoutLock(
        bcos::network::P2pID const& _nodeID) const;

    // handshake protocol
    void sendProtocol(P2PSession::Ptr _session);
    // No error parameter: these are delivered through the type's channel, and the only place an
    // error can arrive (the session's receive pump) goes to teardownSession, never here. The old
    // NetworkException parameter was always success, so every error branch below it was dead.
    void onReceiveProtocol(std::shared_ptr<P2PSession> _session, const Message& _message);
    void onReceiveHeartbeat(std::shared_ptr<P2PSession> _session, const Message& _message);

    // handlers called when new-session
    void registerOnNewSession(std::function<void(P2PSession::Ptr)> _handler);
    // handlers called when delete-session
    void registerOnDeleteSession(std::function<void(P2PSession::Ptr)> _handler);

    virtual void callNewSessionHandlers(const P2PSession::Ptr& _session);
    virtual void callDeleteSessionHandlers(const P2PSession::Ptr& _session);

private:
    // Pull-mode receive pumps. acceptPump drains Host::acceptSession() into onConnect;
    // receiveLoop drains one session's recvMessage() into response correlation, the inbound
    // filters, router forwarding and the per-type subscribe() channels. A receiveLoop exits when
    // the session's channel is closed (drop), and its exit path IS the disconnect handling.
    task::Task<void> acceptPump();
    task::Task<void> receiveLoop(P2PSession::Ptr _p2pSession);
    // Shared teardown of a dead session: stop it (idempotent) and run onDisconnect. Called from
    // receiveLoop's exit path and from onConnect's early-reject branches (self / duplicate peer)
    // — the losing session never gets a pump, so its teardown is run inline here.
    void teardownSession(bcos::network::NetworkException _e, P2PSession::Ptr _p2pSession);

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
    task::Task<std::optional<Message>> forwardMessageByNodeID(bcos::network::P2pID nodeID,
        Message& message, Payloads payloads,
        bcos::network::Options options = bcos::network::Options());
    // the direct-send path shared by the router-less sendMessageByNodeID and by the router
    // module's last-hop/forward fallback (split out so the fallback cannot recurse back into the
    // router path). Defined at the bottom of this header, next to fastSendP2PMessage.
    template <::ranges::input_range Payloads>
        requires std::convertible_to<::ranges::range_reference_t<Payloads>, bytesConstRef>
    task::Task<std::optional<Message>> directSendMessageByNodeID(bcos::network::P2pID nodeID,
        Message& message, Payloads payloads,
        bcos::network::Options options = bcos::network::Options());

    void joinRouterTable(
        std::shared_ptr<P2PSession> _session, std::shared_ptr<RouterTableInterface> _routerTable);
    void broadcastRouterSeq();
    // FIB-186 (vector B): advance is done by the caller; this broadcasts the seq on the leading
    // edge of a membership/route-change burst and coalesces the rest (see
    // RouterState::routerSeqDirty).
    void markRouterSeqChanged();
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
    std::vector<std::function<void(bcos::network::NetworkException, P2PSession::Ptr)>>
        m_disconnectionHandlers;

    std::shared_ptr<bcos::crypto::KeyFactory> m_keyFactory;

    std::map<NodeIPEndpoint, bcos::network::P2pID> m_staticNodes;
    std::shared_mutex x_nodes;
    P2PHost::Ptr m_host;
    // Node-level pending-request table (see PendingResponse.h) and its seq source. Both are
    // Service-scoped: a routed response can arrive on any session of this node.
    PendingResponseTable m_pendingResponses;
    std::atomic<uint32_t> m_seq{1};
    // Count of read-timeout frames consumed by the receive pumps (Session::setReadTimeoutMs; 0
    // disabled by default). Reported by heartBeat so a silently-stalled peer is visible without
    // turning the per-frame path into a log flood.
    std::atomic<uint64_t> m_readTimeouts{0};
    // cert black/white-list admission lists (owned by the P2P identity object); null in tests
    // that never wire one — updatePeerBlacklist/Whitelist then skip the list update
    std::shared_ptr<P2PPeerIdentity> m_peerIdentity;

    // long p2pID to session
    SessionsType m_sessions;
    mutable std::shared_mutex x_sessions;

    P2PInfo m_selfInfo;
    bcos::network::P2pID m_nodeID;
    std::optional<boost::asio::steady_timer> m_timer;
    bool m_run = false;

    // Per-type pull channels (subscribe()/subscribeHandler()). THE inbound dispatch: the receive
    // pumps push into the channel for the message's type, and one pump per type drains it. Guarded
    // by x_channels — subscribe() creates lazily and may race a receive pump's lookup.
    std::array<std::shared_ptr<InboundChannel>, bcos::gateway::GatewayMessageType::All>
        m_channels{};
    mutable std::mutex x_channels;

    // the local protocol
    bcos::protocol::ProtocolInfo::ConstPtr m_localProtocol;
    bcos::protocol::ProtocolInfoCodec::ConstPtr m_codec;

    // handlers called when new-session
    std::vector<std::function<void(P2PSession::Ptr)>> m_newSessionHandlers;
    // handlers called when delete-session
    std::vector<std::function<void(P2PSession::Ptr)>> m_deleteSessionHandlers;

    std::function<std::optional<bcos::Error>(Session&, const Message&, uint32_t)>
        m_beforeMessageHandler;
    std::function<std::optional<bcos::Error>(Session::Ptr, const Message&)> m_onMessageHandler;

    // Compression moved here from Session, whose m_enableCompress defaulted to true; keep that
    // default so a Service built without GatewayFactory (tests, embedders) compresses like
    // before. GatewayFactory always overrides it from config (enableCompress, default true).
    bool m_enableCompress = true;
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
#include <range/v3/view/concat.hpp>
#include <range/v3/view/single.hpp>
#include <utility>

namespace bcos::gateway
{
template <::ranges::input_range Payloads>
    requires std::convertible_to<::ranges::range_reference_t<Payloads>, bytesConstRef>
task::Task<std::optional<Message>> P2PSession::fastSendP2PMessage(
    Message& message, Payloads payloads, bcos::network::Options options)
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
        payloadSize > bcos::network::c_compressThreshold)
    {
        joinedPayload.reserve(payloadSize);
        for (auto const& payloadRef : payloadRefs)
        {
            joinedPayload.insert(joinedPayload.end(), payloadRef.begin(), payloadRef.end());
        }
        if (ZstdCompress::compress(
                ref(joinedPayload), compressedPayload, (int)bcos::network::c_zstdCompressLevel))
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
        BOOST_THROW_EXCEPTION(bcos::network::makeNetworkException(-1, "encode header failed"));
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
        BOOST_THROW_EXCEPTION(
            bcos::network::makeNetworkException((int64_t)error.errorCode(), error.errorMessage()));
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
    // keeps them alive until the session's write no longer references the views. The session send
    // is pure frame transport: it returns false only when the session was already inactive (the
    // old nullopt outcome) and throws NetworkException on a write failure — it knows nothing
    // about responses, so the correlation below is orchestrated here against the Service-level
    // pending-request table.
    if (!options.response)
    {
        if (hasWirePayloadOverride)
        {
            co_await m_session->sendMessage(
                ::ranges::views::concat(::ranges::views::single(ref(headerBuffer)),
                    ::ranges::views::single(wirePayloadOverride)));
        }
        else
        {
            co_await m_session->sendMessage(::ranges::views::concat(
                ::ranges::views::single(ref(headerBuffer)), ::ranges::views::all(payloadRefs)));
        }
        co_return std::nullopt;
    }

    // options.response: register the pending request BEFORE sending — the ack may arrive (on ANY
    // session of this node, for a routed response) immediately after the write hits the wire. The
    // OneShot lives in this coroutine frame; the registered callback's reference into it stays
    // valid because every settlement path (ack / timeout / disconnect-flush / stop-flush /
    // write-failure reclaim) claims the table entry — destroying the callback — before or while
    // settling the slot, and this frame outlives the wait below.
    auto seq = message.seq();
    task::OneShot<bcos::network::NetworkException, std::optional<bcos::network::FrameMeta>> once;
    auto pending = std::make_shared<PendingResponse>();
    pending->callback = [&once](bcos::network::NetworkException exception,
                            std::optional<bcos::network::FrameMeta> responseFrame) {
        once.complete(std::move(exception), std::move(responseFrame));
    };
    pending->owner = m_session;
    if (options.timeout > 0)
    {
        // the timeout timer rides the shared pool — the same executor the old Session-level
        // response timer used
        pending->timeoutHandler.emplace(
            m_session->host().asioInterface()->newTimer(options.timeout));
        std::weak_ptr<Service> weakService = service;
        pending->timeoutHandler->async_wait(
            [weakService, seq](const boost::system::error_code& error) {
                if (error)
                {
                    return;  // cancelled when the entry was settled
                }
                if (auto lockedService = weakService.lock())
                {
                    lockedService->onResponseTimeout(seq);
                }
            });
    }
    if (!service->registerPendingResponse(seq, pending))
    {
        // duplicate seq: unreachable with the Service-wide monotonic allocator — fail loudly
        // instead of waiting on a response that would claim somebody else's entry
        BOOST_THROW_EXCEPTION(
            bcos::network::makeNetworkException(-1, "duplicate pending response seq"));
    }
    try
    {
        bool sent;
        if (hasWirePayloadOverride)
        {
            sent = co_await m_session->sendMessage(
                ::ranges::views::concat(::ranges::views::single(ref(headerBuffer)),
                    ::ranges::views::single(wirePayloadOverride)));
        }
        else
        {
            sent = co_await m_session->sendMessage(::ranges::views::concat(
                ::ranges::views::single(ref(headerBuffer)), ::ranges::views::all(payloadRefs)));
        }
        if (!sent)
        {
            // the session went inactive before the write: no ack can arrive — reclaim the
            // registration and report "no response" exactly like the old early-nullopt path
            if (auto claimed = service->claimPendingResponse(seq);
                claimed && claimed->timeoutHandler)
            {
                claimed->timeoutHandler->cancel();
            }
            co_return std::nullopt;
        }
    }
    catch (bcos::network::NetworkException& e)
    {
        // Write failed: no ack can arrive. Claim the registration back — unless a concurrent
        // ack/timeout/disconnect-flush already settled it, in which case the wait below returns
        // that result inline. Either way the callback never outlives this frame.
        if (auto claimed = service->claimPendingResponse(seq))
        {
            if (claimed->timeoutHandler)
            {
                claimed->timeoutHandler->cancel();
            }
            once.complete(e, std::nullopt);
        }
    }
    catch (...)
    {
        // non-network throw (e.g. allocation failure): reclaim so no dangling callback into this
        // frame's result slot survives, then rethrow
        if (auto claimed = service->claimPendingResponse(seq); claimed && claimed->timeoutHandler)
        {
            claimed->timeoutHandler->cancel();
        }
        throw;
    }

    // wait for ack / timeout / disconnect-flush (returns inline when already completed above)
    auto [exception, responseFrame] = co_await once;
    if (bcos::network::errorCodeOf(exception) != 0)
    {
        BOOST_THROW_EXCEPTION(exception);
    }
    if (!responseFrame)
    {
        co_return std::nullopt;
    }
    // Decode the response frame back into a Message (the session delivers raw frames).
    // decodeOwned takes over the frame storage: the payload becomes a view instead of a copy.
    Message respMessage;
    if (respMessage.decodeOwned(std::move(responseFrame->frame), responseFrame->frameOffset) < 0)
        [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(
            bcos::network::makeNetworkException(bcos::network::P2PExceptionType::ProtocolError,
                "ProtocolError(decode response message error)"));
    }
    co_return respMessage;
}

template <::ranges::input_range Payloads>
    requires std::convertible_to<::ranges::range_reference_t<Payloads>, bytesConstRef>
task::Task<std::optional<Message>> Service::directSendMessageByNodeID(
    bcos::network::P2pID nodeID, Message& header, Payloads payloads, bcos::network::Options options)
{
    if (nodeID == id())
    {
        co_return {};
    }

    auto session = getP2PSessionByNodeId(nodeID);
    if (!session || !session->active())
    {
        BOOST_THROW_EXCEPTION(bcos::network::makeNetworkException(
            -1, "send message failed for no network established"));
    }
    if (header.seq() == 0)
    {
        header.setSeq(newSeq());
    }

    co_return co_await session->fastSendP2PMessage(header, std::move(payloads), options);
}
}  // namespace bcos::gateway
