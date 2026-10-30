/** @file Service.cpp
 *  @author chaychen
 *  @date 20180910
 */

#include "bcos-gateway/libp2p/Service.h"
#include "bcos-framework/Common.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-gateway/libp2p/Common.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PIdentity.h"    // for P2PPeerIdentity
#include "bcos-gateway/libp2p/P2PSession.h"     // for P2PSession
#include "bcos-gateway/libp2p/ServiceRouter.h"  // for Service::RouterState
#include "bcos-network/ASIOInterface.h"
#include "bcos-network/Common.h"
#include "bcos-network/Socket.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include <bcos-task/Wait.h>
#include <openssl/x509.h>
#include <boost/random.hpp>
#include <boost/throw_exception.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/transform.hpp>
#include <shared_mutex>
#include <utility>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;

static const uint32_t CHECK_INTERVAL = 10000;

Service::Service(P2PInfo const& _p2pInfo, std::shared_ptr<RouterTableFactory> _routerTableFactory,
    boost::asio::io_context* _ioContext)
  : m_selfInfo(_p2pInfo), m_nodeID(m_selfInfo.rawP2pID)
{
    m_localProtocol = g_BCOSConfig.protocolInfo(ProtocolModuleID::GatewayService);

    SERVICE_LOG(INFO) << LOG_BADGE("Service::Service") << LOG_DESC("local protocol")
                      << LOG_KV("protocolModuleID", m_localProtocol->protocolModuleID())
                      << LOG_KV("version", m_localProtocol->version())
                      << LOG_KV("minVersion", m_localProtocol->minVersion())
                      << LOG_KV("maxVersion", m_localProtocol->maxVersion());

    m_codec = g_BCOSConfig.codec();
    // Handshake (protocol negotiation) and Heartbeat, as per-type coroutine pumps. Both are
    // fire-and-forget replies: they do no async work, so their pumps just call through and report
    // "keep going".
    subscribeHandler(
        GatewayMessageType::Handshake, [this](InboundMessage&& inbound) -> task::Task<bool> {
            onReceiveProtocol(inbound.session, inbound.message);
            co_return true;
        });
    subscribeHandler(
        GatewayMessageType::Heartbeat, [this](InboundMessage&& inbound) -> task::Task<bool> {
            onReceiveHeartbeat(inbound.session, inbound.message);
            co_return true;
        });

    if (_routerTableFactory)
    {
        initRouter(std::move(_routerTableFactory), *_ioContext);
    }
}

Service::~Service()
{
    stop();
}

void Service::start()
{
    if (!m_run)
    {
        m_run = true;

        // Pull-mode inbound connection intake: drain Host::acceptSession() into onConnect. The
        // pump holds a strong Service reference; Host::stop() (called from Service::stop())
        // closes the accept channel, which ends the pump and releases it.
        task::wait([](std::shared_ptr<Service> service) -> task::Task<void> {
            try
            {
                co_await service->acceptPump();
            }
            catch (std::exception const& e)
            {
                SERVICE_LOG(WARNING) << LOG_DESC("accept pump exit")
                                     << LOG_KV("what", boost::diagnostic_information(e));
            }
        }(shared_from_this()));
        m_host->start();

        heartBeat();
        if (m_router)
        {
            m_router->routerTimer->start();
        }
    }
}

// The accept pump: one loop iteration per inbound connection. acceptSession() throws once the
// host stops — that is the pump's normal exit.
task::Task<void> Service::acceptPump()
{
    while (m_run)
    {
        auto conn = co_await m_host->acceptSession();
        // The IdentityToken is the P2PInfo that the injected P2PPeerIdentity filled during the
        // TLS handshake (bcos-network/PeerIdentity.h); a null token means no identity was
        // extracted — onConnect rejects it
        auto p2pInfo = P2PPeerIdentity::p2pInfoOf(conn.identity);
        if (!p2pInfo || p2pInfo->rawP2pID.empty())
        {
            // inbound: anyone who can reach the P2P port triggers this at their own rate — INFO
            bcos::network::logHandshakeFailed(
                bcos::LogLevel::INFO, conn.session->nodeIPEndpoint(), "no_node_id", "inbound");
        }
        onConnect(bcos::network::NetworkException{}, std::move(p2pInfo), std::move(conn.session));
    }
}

void Service::stop()
{
    closeDispatchBudgets();
    if (m_router)
    {
        m_router->routerTimer->stop();
    }
    if (m_run)
    {
        m_run = false;
        if (m_timer)
        {
            m_timer->cancel();
        }
        // Fail every pending request/response waiter before the network goes down: once the host
        // stops, no ack/timeout path is guaranteed to run and a waiter would hang. Completions
        // are posted to the shared pool while it is still up (see failPendingResponsesOf).
        failAllPendingResponses(bcos::network::makeNetworkException(
            bcos::network::P2PExceptionType::NetworkTimeout, "ServiceStopped"));
        // Close every inbound channel: parked subscribe() consumers AND the per-type handler pumps
        // wake with a NetworkException instead of hanging past the network teardown, which is also
        // what makes the pumps' frames reclaimable (they are detached, so the close is their only
        // exit). Every handler's captured owner `this` (Gateway, AMOPImpl, GatewayNodeManager) is
        // therefore released at stop() without any unregister step.
        {
            std::lock_guard lock(x_channels);
            for (auto& channel : m_channels)
            {
                if (channel)
                {
                    channel->close(std::make_exception_ptr(bcos::network::makeNetworkException(
                        bcos::network::P2PExceptionType::NetworkTimeout, "ServiceStopped")));
                }
            }
        }
        m_host->stop();

        std::vector<P2PSession::Ptr> sessions;
        {
            std::shared_lock lock(x_sessions);
            sessions.reserve(m_sessions.size());
            for (auto const& [p2pID, session] : m_sessions)
            {
                sessions.push_back(session);
            }
        }
        for (auto const& session : sessions)
        {
            session->stop(bcos::network::ClientQuit);
        }
        {
            std::unique_lock lock(x_sessions);
            m_sessions.clear();
        }
    }
}

void Service::reapIdleDispatchBudgets()
{
    std::set<bcos::network::P2pID> activePeers;
    {
        std::shared_lock lock(x_sessions);
        for (auto const& [p2pID, session] : m_sessions)
        {
            if (session->active())
            {
                activePeers.insert(p2pID);
            }
        }
    }
    std::lock_guard lock(x_dispatchBudgets);
    for (auto it = m_peerDispatchBudgets.begin(); it != m_peerDispatchBudgets.end();)
    {
        if (!activePeers.contains(it->first))
        {
            auto usage = it->second->usage();
            if (usage.usedCount == 0 && usage.usedBytes == 0 && usage.waiting == 0)
            {
                it = m_peerDispatchBudgets.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void Service::heartBeat()
{
    if (!m_run)
    {
        return;
    }

    reapIdleDispatchBudgets();
    // FIB-186 (vector D): snapshot the static-node list under x_nodes and release it BEFORE calling
    // isConnected()/connect(), both of which take x_sessions. Holding x_nodes across an
    // x_sessions acquisition here is the reverse of the order onConnect uses (x_sessions ->
    // x_nodes, via updateStaticNodes), which deadlocks under connection churn: onConnect holds
    // x_sessions(W) waiting for x_nodes(W) while heartBeat holds x_nodes(R) waiting for
    // x_sessions(R). Copying the list then dropping x_nodes makes every path acquire x_sessions
    // before x_nodes.
    std::vector<std::pair<NodeIPEndpoint, bcos::network::P2pID>> staticNodes;
    {
        std::shared_lock nodeLock(x_nodes);
        staticNodes.assign(m_staticNodes.begin(), m_staticNodes.end());
    }

    // Reconnect all nodes
    for (auto& it : staticNodes)
    {
        /// exclude myself
        if (it.second == id())
        {
            continue;
        }
        if (!it.second.empty() && isConnected(it.second))
        {
            SERVICE_LOG(TRACE) << LOG_DESC("heartBeat ignore connected")
                               << LOG_KV("endpoint", it.first)
                               << LOG_KV("nodeid", printShortP2pID(it.second));
            continue;
        }
        SERVICE_LOG(DEBUG) << LOG_DESC("heartBeat try to reconnect")
                           << LOG_KV("endpoint", it.first);
        // detached: each reconnect runs in its own coroutine so a stalled connect cannot block the
        // heartBeat pass; the connection result is fed to onConnect exactly like the old callback
        // (error-only on failure; success carries the established session)
        task::wait([](std::shared_ptr<Service> _service,
                       NodeIPEndpoint _endpoint) -> task::Task<void> {
            try
            {
                auto [error, peerIdentity, session] = co_await _service->m_host->connect(_endpoint);
                auto p2pInfo = P2PPeerIdentity::p2pInfoOf(peerIdentity);
                if (session && (!p2pInfo || p2pInfo->rawP2pID.empty()))
                {
                    // outbound static peer whose handshake produced no node id
                    bcos::network::logHandshakeFailed(
                        bcos::LogLevel::WARNING, _endpoint, "no_node_id", "outbound");
                }
                if (session || bcos::network::errorCodeOf(error) != 0)
                {
                    _service->onConnect(std::move(error), std::move(p2pInfo), std::move(session));
                }
            }
            catch (std::exception const& e)
            {
                SERVICE_LOG(WARNING)
                    << LOG_DESC("heartBeat reconnect exception") << LOG_KV("endpoint", _endpoint)
                    << LOG_KV("what", boost::diagnostic_information(e));
            }
        }(shared_from_this(), it.first));
    }

    DispatchBudget::Usage globalUsage;
    {
        std::lock_guard lock(x_dispatchBudgets);
        if (m_globalDispatchBudget)
        {
            globalUsage = m_globalDispatchBudget->usage();
        }
    }
    InboundChannel::Stats inboundStats;
    {
        std::lock_guard lock(x_channels);
        for (auto const& channel : m_channels)
        {
            if (!channel)
            {
                continue;
            }
            auto stats = channel->stats();
            inboundStats.count += stats.count;
            inboundStats.bytes += stats.bytes;
            inboundStats.producerWaiters += stats.producerWaiters;
            inboundStats.producerParks += stats.producerParks;
        }
    }

    std::shared_lock sessionLock(x_sessions);
    SERVICE_LOG(INFO) << METRIC << LOG_DESC("heartBeat")
                      << LOG_KV("connected count", m_sessions.size())
                      << LOG_KV("readTimeouts", m_readTimeouts.load(std::memory_order_relaxed))
                      << LOG_KV("globalDispatchBytes", globalUsage.usedBytes)
                      << LOG_KV("globalDispatchCount", globalUsage.usedCount)
                      << LOG_KV("globalDispatchWaiting", globalUsage.waiting)
                      << LOG_KV("globalDispatchParks", globalUsage.parks)
                      << LOG_KV("globalDispatchParkMs", globalUsage.parkTimeMs)
                      << LOG_KV("inboundDepth", inboundStats.count)
                      << LOG_KV("inboundBytes", inboundStats.bytes)
                      << LOG_KV("inboundProducerWaiters", inboundStats.producerWaiters)
                      << LOG_KV("inboundProducerParks", inboundStats.producerParks);
    if (globalUsage.longestWaitMs >= CHECK_INTERVAL)
    {
        SERVICE_LOG(WARNING) << METRIC << LOG_DESC("heartBeat") << LOG_KV("scope", "global")
                             << LOG_KV("dispatchWaitMs", globalUsage.longestWaitMs)
                             << LOG_KV("dispatchWaiting", globalUsage.waiting)
                             << LOG_KV("inboundProducerWaiters", inboundStats.producerWaiters);
    }
    for (auto const& it : m_sessions)
    {
        auto session = it.second;
        auto queueSize = session->session()->writeQueueSize();
        DispatchBudget::Usage usage;
        if (auto budget = findDispatchBudget(session->p2pID()))
        {
            usage = budget->usage();
        }
        if (usage.longestWaitMs >= CHECK_INTERVAL)
        {
            SERVICE_LOG(WARNING) << METRIC << LOG_DESC("heartBeat")
                                 << LOG_KV("endpoint", session->session()->nodeIPEndpoint())
                                 << LOG_KV("write queue size", queueSize)
                                 << LOG_KV("peerDispatchBytes", usage.usedBytes)
                                 << LOG_KV("peerDispatchCount", usage.usedCount)
                                 << LOG_KV("peerDispatchWaiting", usage.waiting)
                                 << LOG_KV("peerDispatchWaitMs", usage.longestWaitMs)
                                 << LOG_KV("peerDispatchParkMs", usage.parkTimeMs);
        }
        else if (queueSize > 0 || usage.waiting > 0)
        {
            SERVICE_LOG(INFO) << METRIC << LOG_DESC("heartBeat")
                              << LOG_KV("endpoint", session->session()->nodeIPEndpoint())
                              << LOG_KV("write queue size", queueSize)
                              << LOG_KV("peerDispatchBytes", usage.usedBytes)
                              << LOG_KV("peerDispatchCount", usage.usedCount)
                              << LOG_KV("peerDispatchWaiting", usage.waiting);
        }
        else
        {
            SERVICE_LOG(DEBUG) << METRIC << LOG_DESC("heartBeat")
                               << LOG_KV("endpoint", session->session()->nodeIPEndpoint())
                               << LOG_KV("write queue size", queueSize);
        }
    }
    sessionLock.unlock();

    auto self = std::weak_ptr<Service>(shared_from_this());
    m_timer.emplace(m_host->asioInterface()->newTimer(CHECK_INTERVAL));
    m_timer->async_wait([self](const boost::system::error_code& error) {
        if (error)
        {
            SERVICE_LOG(WARNING) << "timer canceled" << LOG_KV("code", error);
            return;
        }
        auto service = self.lock();
        if (service && service->host()->haveNetwork())
        {
            service->heartBeat();
        }
    });
}

/// update the staticNodes
void Service::updateStaticNodes(
    std::shared_ptr<bcos::network::Socket> const& _s, bcos::network::P2pID const& nodeID)
{
    NodeIPEndpoint endpoint(_s->nodeIPEndpoint());
    std::unique_lock nodeLock(x_nodes);
    auto it = m_staticNodes.find(endpoint);
    // modify m_staticNodes(including accept cases, namely the client endpoint)
    if (it != m_staticNodes.end())
    {
        SERVICE_LOG(DEBUG) << LOG_DESC("updateStaticNodes")
                           << LOG_KV("nodeid", printShortP2pID(nodeID))
                           << LOG_KV("endpoint", endpoint);
        it->second = nodeID;
    }
    else
    {
        SERVICE_LOG(DEBUG) << LOG_DESC("updateStaticNodes can't find endpoint")
                           << LOG_KV("nodeid", printShortP2pID(nodeID))
                           << LOG_KV("endpoint", endpoint);
    }
}

void Service::onConnect(
    bcos::network::NetworkException e, std::shared_ptr<P2PInfo> p2pInfo, Session::Ptr session)
{
    bcos::network::P2pID p2pID = p2pInfo ? p2pInfo->rawP2pID : bcos::network::P2pID();
    std::string peer = "unknown";
    if (session)
    {
        peer = session->nodeIPEndpoint().address() + ":" +
               std::to_string(session->nodeIPEndpoint().port());
    }
    if (bcos::network::errorCodeOf(e))
    {
        // DEBUG: every error path in Host::connect has already logged its cause
        // (PeerConnectFailed, rate limited per endpoint; HandshakeFailed; the session-cap
        // WARNING; the client-connect exception ERROR), and the heartbeat retries a down static
        // peer every few seconds
        SERVICE_LOG(DEBUG) << LOG_DESC("onConnect") << LOG_KV("code", bcos::network::errorCodeOf(e))
                           << LOG_KV("p2pid", printShortP2pID(p2pID))
                           << LOG_KV("nodeName", p2pInfo ? p2pInfo->nodeName : "")
                           << LOG_KV("endpoint", peer) << LOG_KV("message", e.what());

        return;
    }

    // A TLS handshake that produced no peer identity (the PeerIdentity skipped every
    // certificate) is unusable; drop it here. (This check used to live in Host before the
    // identity seam made the token opaque to the transport.) The HandshakeFailed event with
    // reason=no_node_id is emitted at the call sites, which know the direction.
    if (!p2pInfo || p2pInfo->rawP2pID.empty())
    {
        SERVICE_LOG(DEBUG) << LOG_DESC("onConnect: no peer identity, disconnect")
                           << LOG_KV("endpoint", peer);
        if (session)
        {
            session->disconnect(bcos::network::DisconnectReason::NullIdentity);
        }
        return;
    }

    if (m_dispatchStopped.load())
    {
        if (session)
        {
            session->disconnect(bcos::network::ClientQuit);
        }
        return;
    }

    SERVICE_LOG(DEBUG) << LOG_DESC("onConnect") << LOG_KV("p2pid", printShortP2pID(p2pID))
                       << LOG_KV("endpoint", peer);

    if (p2pID == id())
    {
        SERVICE_LOG(TRACE) << "Disconnect self";
        updateStaticNodes(session->socket(), id());
        session->disconnect(bcos::network::DuplicatePeer);
        return;
    }

    auto p2pSession = std::make_shared<P2PSession>();
    p2pSession->setSession(session);
    p2pSession->setP2PInfo(*p2pInfo);
    p2pSession->setService(weak_from_this());
    p2pSession->setProtocolInfo(m_localProtocol);
    p2pSession->setDispatchOwner(m_nextDispatchOwner++);
    auto owner = p2pSession->dispatchOwner();
    session->setDropHandler([weakService = weak_from_this(), p2pID, owner] {
        if (auto service = weakService.lock())
        {
            service->cancelDispatch(p2pID, owner);
        }
    });

    // Note: the lock must be here, otherwise there will be more than one started sessions,
    // and a session not maintained in m_sessions will be choosed when send messages in some cases
    // which will cause coredump
    bool duplicated = false;
    bool newSession = false;
    bool stopped = false;
    {
        std::unique_lock lock(x_sessions);
        if (m_dispatchStopped.load())
        {
            stopped = true;
        }
        else
        {
            auto existedSession = getP2PSessionByNodeIdWithoutLock(p2pID);
            if (existedSession && existedSession->active())
            {
                SERVICE_LOG(DEBUG)
                    << "Disconnect duplicate peer" << LOG_KV("p2pid", printShortP2pID(p2pID))
                    << LOG_KV("endpoint", peer);
                updateStaticNodes(session->socket(), p2pID);
                session->disconnect(bcos::network::DuplicatePeer);
                duplicated = true;
            }
            else
            {
                p2pSession->start();
                sendProtocol(p2pSession);
                updateStaticNodes(session->socket(), p2pID);

                if (existedSession)
                {
                    m_sessions[p2pID] = p2pSession;
                }
                else
                {
                    m_sessions.insert(std::make_pair(p2pID, p2pSession));
                    newSession = true;
                }
            }
        }
    }
    if (stopped)
    {
        session->disconnect(bcos::network::ClientQuit);
        return;
    }
    if (duplicated)
    {
        // The losing session never gets a receive pump, so its teardown runs inline here (the
        // old push-mode teardown notification reached onDisconnect asynchronously — running it
        // after releasing x_sessions keeps that off-lock property).
        teardownSession(bcos::network::makeNetworkException(
                            bcos::network::P2PExceptionType::DuplicateSession, "DuplicateSession"),
            p2pSession);
        return;
    }
    std::lock_guard connectStopLock(m_connectStopMutex);
    if (m_dispatchStopped.load())
    {
        p2pSession->stop(bcos::network::ClientQuit);
        return;
    }
    if (newSession)
    {
        callNewSessionHandlers(p2pSession);
    }
    // accepted sockets are bound to the listen port, dialled ones to an ephemeral port
    bool inbound = session->socket()->localEndpoint().port() == m_host->listenPort();
    SERVICE_LOG(INFO) << LOG_DESC("PeerConnected") << LOG_KV("peer", printShortP2pID(p2pID))
                      << LOG_KV("endpoint", session->nodeIPEndpoint())
                      << LOG_KV("direction", inbound ? "in" : "out")
                      << LOG_KV("shortP2pid", printShortP2pID(p2pInfo->p2pID));

    // Start the pull-mode receive pump for the surviving session. The pump frame holds strong
    // Service/P2PSession references; it exits when the session's channel closes (drop),
    // releasing them.
    task::wait([](std::shared_ptr<Service> self, P2PSession::Ptr _p2pSession) -> task::Task<void> {
        co_await self->receiveLoop(std::move(_p2pSession));
    }(shared_from_this(), p2pSession));
}

void Service::teardownSession(bcos::network::NetworkException _e, P2PSession::Ptr _p2pSession)
{
    _p2pSession->stop(bcos::network::UserReason);
    onDisconnect(std::move(_e), std::move(_p2pSession));
}

// The per-session receive pump (pull mode): pulls decoded frames out of the session's
// recvMessage() channel, correlates responses, decodes to Message and dispatches through
// onMessage. The loop's exit IS the disconnect path: recvMessage() rethrows the
// NetworkException the session's channel was closed with (drop / protocol error), and the
// catch below performs what the push-mode teardown notification used to reach asynchronously.
task::Task<void> Service::receiveLoop(P2PSession::Ptr _p2pSession)
{
    auto session = _p2pSession->session();
    try
    {
        while (m_run)
        {
            auto meta = co_await session->recvMessage();

            // Read-timeout frames (Session::setReadTimeoutMs > 0; empty frame, consumed == 0) are
            // NOT messages and must be handled here. Without this branch the empty frame falls
            // through to the decode below, where decodeOwned would report MESSAGE_INCOMPLETE and —
            // under the old `>= 0` test — be mistaken for a successfully decoded (bogus, all-zero)
            // Message and dispatched to the message handlers. Peer silence is not a protocol error:
            // the read timeout is a keepalive, so count it and keep the pump running.
            if (meta.status == bcos::network::FrameMeta::Status::Timeout) [[unlikely]]
            {
                m_readTimeouts.fetch_add(1, std::memory_order_relaxed);
                SERVICE_LOG(DEBUG) << LOG_DESC("recvMessage read timeout, peer silent")
                                   << LOG_KV("p2pid", _p2pSession->printP2pID());
                continue;
            }
            if (meta.status != bcos::network::FrameMeta::Status::Frame) [[unlikely]]
            {
                // The session's decoder contract (see FrameDecoder/FrameMeta): the read loop only
                // ever pushes Frame or Timeout. Reaching here means the session/decoder pair broke
                // that contract, so fail loudly instead of dispatching garbage.
                SERVICE_LOG(ERROR) << LOG_DESC("unexpected frame status from recvMessage")
                                   << LOG_KV("status", static_cast<int>(meta.status))
                                   << LOG_KV("p2pid", _p2pSession->printP2pID());
                BOOST_THROW_EXCEPTION(bcos::network::makeNetworkException(
                    bcos::network::P2PExceptionType::ProtocolError,
                    "ProtocolError(unexpected frame status)"));
            }

            // Response correlation is P2P policy, so it lives here rather than in libnetwork: a
            // response frame addressed to THIS node (or carrying no dstP2PNodeID, the V0 form)
            // settles the pending request in the node-level table; a frame addressed to another
            // node falls through to the router like any other — a routed response must never
            // consume a LOCAL pending callback on a seq collision. A frame that is not a response
            // (nullopt) falls through too.
            auto respInfo = Message::peekResponseFrameInfo(meta.frameData());
            if (respInfo && respInfo->isResp &&
                (respInfo->dstP2PNodeID.empty() || respInfo->dstP2PNodeID == m_nodeID ||
                    respInfo->dstP2PNodeID == m_selfInfo.p2pID))
            {
                onResponseFrame(respInfo->seq, session, std::move(meta));
                continue;
            }

            Message message;
            // decode's bounds checks (checkOffset) throw out_of_range on a malformed frame;
            // treat that exactly like a decode error: ProtocolError drops the session below.
            // decodeOwned takes over the frame storage: the payload becomes a view instead of
            // a second full-frame copy.
            try
            {
                // `> 0`, not `>= 0`: decodeImpl returns the decoded length on success and
                // MESSAGE_INCOMPLETE == 0 is a FAILURE (see network::MessageDecodeStatus) — it is
                // what decodeOwned returns for an empty/too-short frame. Testing `>= 0` conflates
                // "decoded" with "incomplete" and dispatches an all-zero Message.
                if (message.decodeOwned(std::move(meta.frame), meta.frameOffset) > 0) [[likely]]
                {
                    co_await onMessage(bcos::network::NetworkException{}, session,
                        std::move(message), _p2pSession);
                    continue;
                }
            }
            catch (std::exception const& e)
            {
                SERVICE_LOG(WARNING) << LOG_DESC("decode message exception")
                                     << LOG_KV("msg", boost::diagnostic_information(e));
            }
            // decode failure: a ProtocolError teardown, the same treatment the old in-session
            // decode gave it
            BOOST_THROW_EXCEPTION(
                bcos::network::makeNetworkException(bcos::network::P2PExceptionType::ProtocolError,
                    "ProtocolError(decode message error)"));
        }
    }
    catch (bcos::network::NetworkException& e)
    {
        SERVICE_LOG(INFO) << LOG_DESC("receiveLoop exit, teardown session")
                          << LOG_KV("p2pid", _p2pSession->printP2pID())
                          << LOG_KV("code", bcos::network::errorCodeOf(e))
                          << LOG_KV("msg", e.what());
        teardownSession(e, std::move(_p2pSession));
    }
    catch (std::exception const& e)
    {
        // never let an exception escape a detached pump
        SERVICE_LOG(WARNING) << LOG_DESC("receiveLoop exception")
                             << LOG_KV("what", boost::diagnostic_information(e));
        teardownSession(bcos::network::makeNetworkException(
                            bcos::network::P2PExceptionType::Disconnect, "Disconnect"),
            std::move(_p2pSession));
    }
}

void Service::onDisconnect(bcos::network::NetworkException e, P2PSession::Ptr p2pSession)
{
    // Fail the pending requests whose request went out on the dropped session (the old
    // Session::drop flush, moved up with the correlation table): a request is failed when its
    // OUTBOUND session dies even though a routed response could in principle arrive on another
    // session — preserving the old fail-fast-on-drop semantics.
    failPendingResponsesOf(p2pSession->session(),
        bcos::network::makeNetworkException(
            bcos::network::P2PExceptionType::NetworkTimeout, "NetworkTimeout"));
    // handle all registered handlers
    for (const auto& handler : m_disconnectionHandlers)
    {
        handler(e, p2pSession);
    }
    auto session = getP2PSessionByNodeId(p2pSession->p2pID());
    if (session && session == p2pSession)
    {
        SERVICE_LOG(TRACE) << "Service onDisconnect and remove from sessions"
                           << LOG_KV("p2pid", p2pSession->printP2pID())
                           << LOG_KV("endpoint", p2pSession->session()->nodeIPEndpoint());
        {
            std::unique_lock l(x_sessions);
            m_sessions.erase(p2pSession->p2pID());
        }
        callDeleteSessionHandlers(p2pSession);

        if (bcos::network::errorCodeOf(e) == bcos::network::P2PExceptionType::DuplicateSession)
        {
            return;
        }
        // Session::drop() puts the disconnectReasonTag into the exception message
        std::string_view reason = "error";
        if (bcos::network::errorCodeOf(e) == bcos::network::P2PExceptionType::NetworkTimeout)
        {
            reason = "timeout";
        }
        else if (bcos::network::errorCodeOf(e) == bcos::network::P2PExceptionType::Disconnect)
        {
            reason = e.what();
        }
        SERVICE_LOG(INFO) << LOG_DESC("PeerDisconnected")
                          << LOG_KV("peer", p2pSession->printP2pID())
                          << LOG_KV("endpoint", p2pSession->session()->nodeIPEndpoint())
                          << LOG_KV("reason", reason)
                          << LOG_KV("code", bcos::network::errorCodeOf(e))
                          << LOG_KV("detail", e.what());
        std::unique_lock nodeLock(x_nodes);
        for (auto& it : m_staticNodes)
        {
            if (it.second == p2pSession->p2pID())
            {
                it.second.clear();  // clear nodeid info when disconnect
                break;
            }
        }
    }
    // heartBeat();
}

task::Task<void> Service::sendRespMessageBySession(bcos::bytes _payload, uint32_t _requestSeq,
    std::string _requestSrcP2PNodeID, P2PSession::Ptr _p2pSession)
{
    // router path (merged from the former ServiceV2): the response carries dst/src so it can be
    // routed back through the network; V0 peers keep the plain response below
    if (m_router && _p2pSession->protocolInfo()->version() > bcos::protocol::ProtocolVersion::V0)
    {
        auto p2pid = _p2pSession->p2pID();
        try
        {
            Message respMessage;
            respMessage.setDstP2PNodeID(std::move(_requestSrcP2PNodeID));
            respMessage.setSrcP2PNodeID(m_nodeID);
            respMessage.setSeq(_requestSeq);
            respMessage.setRespPacket();
            respMessage.setPayload(std::move(_payload));
            // Registered directly on the original session (zero-copy view into respMessage, which
            // this frame owns for the whole send).
            co_await _p2pSession->fastSendP2PMessage(respMessage,
                ::ranges::views::single(respMessage.payload()), bcos::network::Options{});
            if (c_fileLogLevel <= TRACE) [[unlikely]]
            {
                SERVICE2_LOG(TRACE)
                    << LOG_BADGE("sendRespMessageBySession") << LOG_KV("seq", _requestSeq)
                    << LOG_KV("from", respMessage.printSrcP2PNodeID())
                    << LOG_KV("dst", respMessage.printDstP2PNodeID())
                    << LOG_KV("payload size", respMessage.payload().size());
            }
        }
        catch (std::exception const& e)
        {
            // A synchronous pre-send rejection (rate limit / max size) on the response path is
            // expected during bandwidth saturation — log the request seq and target so the
            // response-loss investigation keeps the routing dimension.
            SERVICE2_LOG(WARNING) << LOG_BADGE("sendRespMessageBySession")
                                  << LOG_DESC("send response failed") << LOG_KV("seq", _requestSeq)
                                  << LOG_KV("p2pid", printShortP2pID(p2pid))
                                  << LOG_KV("what", boost::diagnostic_information(e));
        }
        co_return;
    }

    auto p2pid = _p2pSession->p2pID();
    try
    {
        Message respMessage;
        respMessage.setSeq(_requestSeq);
        respMessage.setRespPacket();
        respMessage.setPayload(std::move(_payload));
        co_await _p2pSession->fastSendP2PMessage(
            respMessage, ::ranges::views::single(respMessage.payload()), bcos::network::Options{});
        if (c_fileLogLevel <= TRACE) [[unlikely]]
        {
            SERVICE_LOG(TRACE) << "sendRespMessageBySession" << LOG_KV("seq", _requestSeq)
                               << LOG_KV("p2pid", printShortP2pID(p2pid))
                               << LOG_KV("payload size", respMessage.payload().size());
        }
    }
    catch (std::exception const& e)
    {
        // Same shape as the router path above (which delegates V0 peers here): a synchronous
        // pre-send rejection on the response path is logged, not propagated — the caller is a
        // message handler, and a failed response must not fail the whole dispatch.
        SERVICE_LOG(WARNING) << LOG_BADGE("sendRespMessageBySession")
                             << LOG_DESC("send response failed") << LOG_KV("seq", _requestSeq)
                             << LOG_KV("p2pid", printShortP2pID(p2pid))
                             << LOG_KV("what", boost::diagnostic_information(e));
    }
    co_return;
}

std::optional<bcos::Error> Service::onBeforeMessage(
    Session& _session, const Message& _message, uint32_t _wireLength)
{
    if (m_beforeMessageHandler)
    {
        return m_beforeMessageHandler(_session, _message, _wireLength);
    }

    return std::nullopt;
}

task::Task<void> Service::onMessage(bcos::network::NetworkException e, Session::Ptr session,
    Message message, std::weak_ptr<P2PSession> p2pSessionWeakPtr)
{
    if (m_dispatchStopped.load())
    {
        co_return;
    }
    // router path (merged from the former ServiceV2::onMessage): a message whose dstP2PNodeID is
    // another node is forwarded through the router table; on local delivery the short p2pIDs are
    // translated back to the raw ids before the common handling below
    if (m_router)
    {
        if (bcos::network::errorCodeOf(e) != 0)
        {
            SERVICE2_LOG(WARNING) << LOG_BADGE("onMessage")
                                  << LOG_KV("code", bcos::network::errorCodeOf(e))
                                  << LOG_KV("msg", e.what());
            // fall through to the common handling below to trigger disconnectHandler
        }
        else if (auto dstNodeP2pID = getRawP2pID(message.dstP2PNodeID());
            !message.dstP2PNodeID().empty() && dstNodeP2pID != m_nodeID)
        {
            // forward the message again
            auto ttl = (int16_t)message.ttl();
            if (ttl <= 0)
            {
                SERVICE2_LOG(WARNING)
                    << LOG_BADGE("onMessage") << LOG_DESC("expired ttl")
                    << LOG_KV("seq", message.seq()) << LOG_KV("from", message.printSrcP2PNodeID())
                    << LOG_KV("dst", message.dstP2PNodeID()) << LOG_KV("type", message.packetType())
                    << LOG_KV("rsp", message.isRespPacket())
                    << LOG_KV("payLoadSize", message.payload().size()) << LOG_KV("ttl", ttl);
                co_return;
            }
            ttl -= 1;
            // recover to long p2p-node id for dispatcher through router
            message.setDstP2PNodeID(dstNodeP2pID);
            message.setTTL(ttl);
            if (c_fileLogLevel <= TRACE) [[unlikely]]
            {
                SERVICE2_LOG(TRACE)
                    << LOG_BADGE("onMessage") << LOG_DESC("forwardMessage")
                    << LOG_KV("seq", message.seq()) << LOG_KV("from", message.printSrcP2PNodeID())
                    << LOG_KV("dst", message.printDstP2PNodeID())
                    << LOG_KV("type", message.packetType()) << LOG_KV("seq", message.seq())
                    << LOG_KV("rsp", message.isRespPacket()) << LOG_KV("ttl", message.ttl())
                    << LOG_KV("payLoadSize", message.payload().size());
            }
            // forward through the coroutine fast path (zero-copy: the received message is moved
            // into the coroutine frame so it stays alive, and its payload is sent as a view).
            // Note: forwarding must NOT rewrite srcP2PNodeID (that is only done when this node
            // originates the message) — forwardMessageByNodeID only resolves the next hop.
            auto self = shared_from_this();
            task::wait(
                [](std::shared_ptr<Service> _self, Message _p2pMsg) mutable -> task::Task<void> {
                    try
                    {
                        co_await _self->forwardMessageByNodeID(_p2pMsg.dstP2PNodeID(), _p2pMsg,
                            ::ranges::views::single(_p2pMsg.payload()), bcos::network::Options{});
                    }
                    catch (std::exception const& e)
                    {
                        // A synchronous pre-send rejection (rate limit / max size) or an async
                        // write failure on the relay path is expected during bandwidth saturation —
                        // log the next hop so a "peer not receiving relayed messages"
                        // investigation keeps the routing dimension.
                        SERVICE2_LOG(WARNING)
                            << LOG_BADGE("onMessage") << LOG_DESC("forwardMessage failed")
                            << LOG_KV("dst", _p2pMsg.dstP2PNodeID()) << LOG_KV("seq", _p2pMsg.seq())
                            << LOG_KV("what", boost::diagnostic_information(e));
                    }
                }(self, std::move(message)));
            co_return;
        }
        else
        {
            // v0 message or the dstP2PNodeID is the nodeSelf or empty
            if (c_fileLogLevel <= TRACE) [[unlikely]]
            {
                SERVICE2_LOG(TRACE)
                    << LOG_BADGE("onMessage") << LOG_KV("from", message.printSrcP2PNodeID())
                    << LOG_KV("seq", message.seq()) << LOG_KV("dst", message.printDstP2PNodeID())
                    << LOG_KV("type", message.packetType()) << LOG_KV("rsp", message.isRespPacket())
                    << LOG_KV("ttl", message.ttl())
                    << LOG_KV("payLoadSize", message.payload().size());
            }
            // convert short-p2p-id to long-p2p-id when handle the message
            message.setDstP2PNodeID(dstNodeP2pID);
            message.setSrcP2PNodeID(getRawP2pID(message.srcP2PNodeID()));
        }
    }

    auto p2pSession = p2pSessionWeakPtr.lock();
    if (!p2pSession)
    {
        co_return;
    }

    try
    {
        bcos::network::P2pID p2pID = id();
        NodeIPEndpoint nodeIPEndpoint(boost::asio::ip::address(), 0);
        if (session && p2pSession)
        {
            p2pID = p2pSession->p2pID();
            nodeIPEndpoint = session->nodeIPEndpoint();
        }

        if (bcos::network::errorCodeOf(e))
        {
            SERVICE_LOG(DEBUG) << LOG_DESC("disconnect failed in P2PSession")
                               << LOG_KV("p2pid", printShortP2pID(p2pID))
                               << LOG_KV("endpoint", nodeIPEndpoint)
                               << LOG_KV("code", bcos::network::errorCodeOf(e))
                               << LOG_KV("message", e.what());

            if (p2pSession)
            {
                p2pSession->stop(bcos::network::UserReason);
                onDisconnect(e, p2pSession);
            }
            co_return;
        }

        if (auto result =
                (m_onMessageHandler ? m_onMessageHandler(session, message) : std::nullopt))
        {
            auto& error = result.value();
            // TODO:  For p2p basic message type, direct discard request ???
            SERVICE_LOG(TRACE) << LOG_DESC("onMessage receive message")
                               << LOG_DESC(error.errorMessage())
                               << LOG_KV("endpoint", nodeIPEndpoint) << LOG_KV("seq", message.seq())
                               << LOG_KV("version", message.version())
                               << LOG_KV("packetType", message.packetType());
            co_return;
        }

        if (c_fileLogLevel <= TRACE) [[unlikely]]
        {
            SERVICE_LOG(TRACE) << LOG_DESC("onMessage receive message")
                               << LOG_KV("p2pid", printShortP2pID(p2pID))
                               << LOG_KV("endpoint", nodeIPEndpoint) << LOG_KV("seq", message.seq())
                               << LOG_KV("version", message.version())
                               << LOG_KV("packetType", message.packetType());
        }

        auto packetType = message.packetType();
        auto ext = message.ext();
        auto version = message.version();
        auto seq = message.seq();
        // THE dispatch: push into the channel subscribed for this type; its pump runs the handler.
        // A full channel PARKS this receive pump in waitWritable() instead of dropping the
        // message: the back-pressure propagates through the session's recv queue to the TCP
        // window. The per-type handler never awaits a permit or remote I/O, and a peer's own
        // budget limits only its receive loop. The shared global ceiling can pause every session's
        // receive loop when saturated; either wait also pauses later packet types on that session.
        if (auto channel = inboundChannel(packetType))
        {
            auto inbound = InboundMessage{std::move(p2pSession), std::move(message), {}};
            // The same measure the channel's sizeOf charges (they must agree). push() leaves the
            // value intact on failure, so losing the wake race costs one more park, never the
            // message.
            const auto bytes = inboundMessageBytes(inbound);
            if (packetType == gateway::GatewayMessageType::PeerToPeerMessage)
            {
                auto peerBudget = dispatchBudgetFor(*inbound.session);
                auto globalBudget = ensureGlobalDispatchBudget();
                if (!peerBudget || !globalBudget)
                {
                    co_return;
                }
                auto owner = std::to_string(inbound.session->dispatchOwner());
                inbound.dispatchPermit.peer = co_await peerBudget->acquire(bytes, owner);
                if (!inbound.dispatchPermit.peer)
                {
                    if (bytes > peerBudget->maxBytes())
                    {
                        SERVICE_LOG(ERROR)
                            << LOG_DESC("inbound message exceeds the channel byte budget, drop")
                            << LOG_KV("scope", "peer") << LOG_KV("seq", seq)
                            << LOG_KV("bytes", bytes);
                    }
                    co_return;
                }
                inbound.dispatchPermit.global = co_await globalBudget->acquire(bytes, owner);
                if (!inbound.dispatchPermit.global)
                {
                    if (bytes > globalBudget->maxBytes())
                    {
                        SERVICE_LOG(ERROR)
                            << LOG_DESC("inbound message exceeds the channel byte budget, drop")
                            << LOG_KV("scope", "global") << LOG_KV("seq", seq)
                            << LOG_KV("bytes", bytes);
                    }
                    co_return;
                }
            }
            while (!channel->push(inbound))
            {
                if (!co_await channel->waitWritable(bytes))
                {
                    // false = closed (stop(): teardown owns the message) or can-never-fit.
                    // The 96MB budget covers the upstream 64MB frame-storage bound plus decoded
                    // metadata; compressed wire storage is released, so this is defensive.
                    if (!channel->closed())
                    {
                        SERVICE_LOG(ERROR)
                            << LOG_DESC("inbound message exceeds the channel byte budget, drop")
                            << LOG_KV("packetType", packetType) << LOG_KV("seq", seq)
                            << LOG_KV("bytes", bytes);
                    }
                    co_return;
                }
            }
            co_return;
        }

        if (packetType == gateway::AMOPMessageType)
        {
            // AMOP May be disable by config.ini
            SERVICE_LOG(DEBUG) << LOG_DESC("Unrecognized message type")
                               << LOG_DESC(": AMOP is disabled!") << LOG_KV("seq", message.seq())
                               << LOG_KV("packetType", packetType) << LOG_KV("ext", ext)
                               << LOG_KV("version", version)
                               << LOG_KV("dst p2p", message.printDstP2PNodeID());
            co_return;
        }
        SERVICE_LOG(ERROR) << LOG_DESC("Unrecognized message type") << LOG_KV("seq", message.seq())
                           << LOG_KV("packetType", packetType) << LOG_KV("ext", ext)
                           << LOG_KV("version", version)
                           << LOG_KV("dstp2p", message.printDstP2PNodeID());
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "onMessage error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

bcos::task::Task<void> Service::broadcastMessageToAll(Message::Ptr message,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads,
    bcos::network::Options options)
{
    if (m_router)
    {
        // router path (merged from the former ServiceV2): broadcast to all reachable nodes
        // through the router table
        auto reachableNodes = m_router->routerTable->getAllReachableNode();
        auto selfV2 = shared_from_this();
        // Fan out one independent coroutine per peer (see the router-less path below).
        for (auto const& node : reachableNodes)
        {
            task::wait([](std::shared_ptr<Service> _self, bcos::network::P2pID _node,
                           Message::Ptr _message,
                           ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads,
                           bcos::network::Options _options) mutable -> task::Task<void> {
                try
                {
                    co_await _self->sendMessageByNodeID(
                        _node, *_message, std::move(_payloads), std::move(_options));
                }
                catch (std::exception const& e)
                {
                    SERVICE2_LOG(WARNING) << LOG_BADGE("broadcastMessageToAll")
                                          << LOG_KV("node", printShortP2pID(_node))
                                          << LOG_KV("what", boost::diagnostic_information(e));
                }
            }(selfV2, node, message, payloads, options));
        }
        co_return;
    }
    std::vector<bcos::network::P2pID> nodeIDs;
    {
        std::shared_lock lock(x_sessions);
        nodeIDs.reserve(m_sessions.size());
        for (auto const& session : m_sessions)
        {
            nodeIDs.push_back(session.first);
        }
    }
    auto self = shared_from_this();
    // Fan out one independent coroutine per peer (see broadcastMessageToNeighbors): the caller's
    // message is handed over as a shared_ptr so every per-peer task keeps it alive; each task's
    // per-session src/dst/version stamping runs synchronously before its first suspension, so no
    // race on the shared header and no head-of-line blocking on a stalled peer's socket.
    for (auto const& nodeID : nodeIDs)
    {
        task::wait(
            [](std::shared_ptr<Service> _self, bcos::network::P2pID _nodeID, Message::Ptr _message,
                ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads,
                bcos::network::Options _options) mutable -> task::Task<void> {
                try
                {
                    co_await _self->sendMessageByNodeID(
                        _nodeID, *_message, std::move(_payloads), std::move(_options));
                }
                catch (std::exception const& e)
                {
                    SERVICE_LOG(WARNING) << LOG_DESC("broadcastMessageToAll failed")
                                         << LOG_KV("nodeid", printShortP2pID(_nodeID))
                                         << LOG_KV("what", boost::diagnostic_information(e));
                }
            }(self, nodeID, message, payloads, options));
    }
    co_return;
}

bcos::task::Task<void> Service::broadcastMessageToNeighbors(Message::Ptr message,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads,
    bcos::network::Options options)
{
    // Only directly connected sessions (m_sessions), unlike broadcastMessageToAll which may fan
    // out through the router table when the router module is enabled. This preserves the "router
    // table sync only between neighbors, propagated hop-by-hop" gossip model used by
    // broadcastRouterSeq.
    std::vector<bcos::network::P2pID> nodeIDs;
    {
        std::shared_lock lock(x_sessions);
        nodeIDs.reserve(m_sessions.size());
        for (auto const& session : m_sessions)
        {
            nodeIDs.push_back(session.first);
        }
    }
    auto self = shared_from_this();
    // Fan out one independent coroutine per peer: the caller's message
    // is handed over as a shared_ptr so every per-peer task keeps it alive; each task's
    // per-session src/dst/version stamping runs synchronously before its first suspension, so no
    // race on the shared header and no head-of-line blocking on a stalled peer's socket.
    for (auto const& nodeID : nodeIDs)
    {
        task::wait(
            [](std::shared_ptr<Service> _self, bcos::network::P2pID _nodeID, Message::Ptr _message,
                ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads,
                bcos::network::Options _options) mutable -> task::Task<void> {
                try
                {
                    co_await _self->sendMessageByNodeID(
                        _nodeID, *_message, std::move(_payloads), std::move(_options));
                }
                catch (std::exception const& e)
                {
                    SERVICE_LOG(WARNING) << LOG_DESC("broadcastMessageToNeighbors failed")
                                         << LOG_KV("nodeid", printShortP2pID(_nodeID))
                                         << LOG_KV("what", boost::diagnostic_information(e));
                }
            }(self, nodeID, message, payloads, options));
    }
    co_return;
}

P2PInfos Service::sessionInfos()
{
    std::shared_lock lock(x_sessions);
    return ::ranges::views::values(m_sessions) |
           ::ranges::views::transform([](auto const& session) { return session->p2pInfo(); }) |
           ::ranges::to<P2PInfos>();
}

bool Service::isConnected(bcos::network::P2pID const& nodeID) const
{
    auto session = getP2PSessionByNodeId(nodeID);
    return session && session->active();
}

bcos::task::Task<void> Service::sendMessageByNodeIDs(uint16_t _type,
    const std::vector<bcos::network::P2pID>& _nodeIDs, bcos::bytes _payload,
    bcos::network::Options _options)
{
    // value message held by shared_ptr: fan out one independent coroutine per node, so a stalled
    // peer's socket write cannot delay delivery to the peers behind it (same head-of-line-blocking
    // reasoning as broadcastMessageToAll). Each per-peer task keeps the message alive; its
    // per-session src/dst/version stamping runs synchronously before its first suspension, so the
    // shared header stays race-free. A failed/unreachable node is logged and skipped.
    auto message = std::make_shared<Message>();
    message->setPacketType(_type);
    message->setSeq(newSeq());
    message->setPayload(std::move(_payload));
    auto self = shared_from_this();
    for (auto const& nodeID : _nodeIDs)
    {
        task::wait([](std::shared_ptr<Service> _self, bcos::network::P2pID _nodeID,
                       Message::Ptr _message, bcos::network::Options _options) -> task::Task<void> {
            try
            {
                co_await _self->sendMessageByNodeID(_nodeID, *_message,
                    ::ranges::views::single(_message->payload()),
                    bcos::network::Options{_options.timeout, false});
            }
            catch (bcos::network::NetworkException const& e)
            {
                SERVICE_LOG(INFO) << LOG_DESC("sendMessageByNodeIDs send failed")
                                  << LOG_KV("nodeid", printShortP2pID(_nodeID))
                                  << LOG_KV("code", bcos::network::errorCodeOf(e))
                                  << LOG_KV("message", e.what());
            }
            catch (std::exception const& e)
            {
                // a non-network throw must not escape this detached fan-out task onto the io
                // thread that resumed it: log and skip the node, as for a send failure
                SERVICE_LOG(WARNING)
                    << LOG_DESC("sendMessageByNodeIDs unexpected exception, skip node")
                    << LOG_KV("nodeid", printShortP2pID(_nodeID))
                    << LOG_KV("message", boost::diagnostic_information(e));
            }
        }(self, nodeID, message, _options));
    }
    co_return;
}

// send the protocolInfo
void Service::sendProtocol(P2PSession::Ptr _session)
{
    auto self = shared_from_this();
    // value message in frame; payload owned by the frame. All state is passed as coroutine
    // parameters so it is copied into the frame and stays alive. The pre-send checks (outgoing
    // rate limit / max size) run synchronously on the caller thread inside fastSendP2PMessage and
    // may throw — catch so a handshake rejection cannot escape task::wait and abort onConnect's
    // session-registration tail (the session would stay live-but-unregistered, silently hidden
    // from the routing layer). A failed handshake is recoverable: the peer retries.
    task::wait([](std::shared_ptr<Service> _self, P2PSession::Ptr _session) -> task::Task<void> {
        try
        {
            auto payload = bytes();
            _self->m_codec->encode(_self->m_localProtocol, payload);
            Message message;
            message.setPacketType(GatewayMessageType::Handshake);
            message.setSeq(_self->newSeq());
            message.setPayload(std::move(payload));
            SERVICE_LOG(INFO) << LOG_DESC("sendProtocol")
                              << LOG_KV("payload", message.payload().size())
                              << LOG_KV("seq", message.seq());
            co_await _session->fastSendP2PMessage(
                message, ::ranges::views::single(message.payload()), bcos::network::Options{});
        }
        catch (std::exception const& e)
        {
            SERVICE_LOG(WARNING) << LOG_DESC("sendProtocol send exception")
                                 << LOG_KV("p2pid", printShortP2pID(_session->p2pID()))
                                 << LOG_KV("what", boost::diagnostic_information(e));
        }
    }(self, _session));
}

// receive the heartbeat msg
void Service::onReceiveHeartbeat(std::shared_ptr<P2PSession> _session, const Message& /*unused*/)
{
    std::string endpoint = "unknown";
    if (_session)
    {
        endpoint = _session->session()->nodeIPEndpoint().address();
    }

    SERVICE_LOG(TRACE) << LOG_BADGE("onReceiveHeartbeat") << LOG_DESC("receive heartbeat message")
                       << LOG_KV("endpoint", endpoint);
}

// receive the protocolInfo
void Service::onReceiveProtocol(std::shared_ptr<P2PSession> _session, const Message& _message)
{
    try
    {
        auto payload = _message.payload();
        auto protocolInfo = m_codec->decode(bytesConstRef(payload.data(), payload.size()));
        // negotiated version
        if (protocolInfo->minVersion() > m_localProtocol->maxVersion() ||
            protocolInfo->maxVersion() < m_localProtocol->minVersion())
        {
            bcos::network::logHandshakeFailed(bcos::LogLevel::WARNING,
                _session->session()->nodeIPEndpoint(), "protocol_mismatch",
                "peer=" + _session->printP2pID() + " peerVersion=[" +
                    std::to_string(protocolInfo->minVersion()) + "," +
                    std::to_string(protocolInfo->maxVersion()) + "] local=[" +
                    std::to_string(m_localProtocol->minVersion()) + "," +
                    std::to_string(m_localProtocol->maxVersion()) + "]");
            _session->session()->disconnect(bcos::network::DisconnectReason::NegotiateFailed);
            return;
        }
        auto version = std::min(m_localProtocol->maxVersion(), protocolInfo->maxVersion());
        protocolInfo->setVersion(version);
        _session->setProtocolInfo(protocolInfo);
        SERVICE_LOG(DEBUG) << LOG_DESC("onReceiveProtocol: protocolNegotiate success")
                           << LOG_KV("peer", _session->printP2pID())
                           << LOG_KV("minVersion", protocolInfo->minVersion())
                           << LOG_KV("maxVersion", protocolInfo->maxVersion())
                           << LOG_KV("supportMinVersion", m_localProtocol->minVersion())
                           << LOG_KV("supportMaxVersion", m_localProtocol->maxVersion())
                           << LOG_KV("negotiatedVersion", version);
    }
    catch (std::exception const& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("onReceiveProtocol exception")
                             << LOG_KV("peer", _session ? _session->printP2pID() : "unknown")
                             << LOG_KV("packetType", _message.packetType())
                             << LOG_KV("seq", _message.seq());
    }
}

void Service::updatePeerBlacklist(const std::set<std::string>& _strList, const bool _enable)
{
    // update the admission list (lives in the P2P identity object)
    if (m_peerIdentity)
    {
        m_peerIdentity->peerBlacklist().update(_strList, _enable);
    }
    else
    {
        SERVICE_LOG(WARNING) << LOG_DESC(
            "updatePeerBlacklist: no peer identity wired, "
            "skip list update");
    }
    // disconnect nodes in the blacklist
    if (_enable)
    {
        std::shared_lock lock(x_sessions);
        for (const auto& session : m_sessions)
        {
            auto p2pIdWithoutExtInfo = session.second->p2pInfo().p2pIDWithoutExtInfo;
            if (_strList.end() == _strList.find(p2pIdWithoutExtInfo))
            {
                continue;
            }

            SERVICE_LOG(INFO) << LOG_DESC("updatePeerBlacklist, disconnect peer in blacklist")
                              << LOG_KV("peer", p2pIdWithoutExtInfo);

            updateStaticNodes(session.second->session()->socket(), session.second->p2pID());
            session.second->session()->disconnect(
                bcos::network::DisconnectReason::InBlacklistReason);
        }
    }
}

void Service::updatePeerWhitelist(const std::set<std::string>& _strList, const bool _enable)
{
    // update the admission list (lives in the P2P identity object)
    if (m_peerIdentity)
    {
        m_peerIdentity->peerWhitelist().update(_strList, _enable);
    }
    else
    {
        SERVICE_LOG(WARNING) << LOG_DESC(
            "updatePeerWhitelist: no peer identity wired, "
            "skip list update");
    }
    // disconnect nodes not in the whitelist
    if (_enable)
    {
        std::shared_lock lock(x_sessions);
        for (auto const& session : m_sessions)
        {
            auto p2pIdWithoutExtInfo = session.second->p2pInfo().p2pIDWithoutExtInfo;
            if (_strList.end() != _strList.find(p2pIdWithoutExtInfo))
            {
                continue;
            }

            SERVICE_LOG(INFO) << LOG_DESC("updatePeerWhitelist, disconnect peer not in whitelist")
                              << LOG_KV("peer", p2pIdWithoutExtInfo);

            updateStaticNodes(session.second->session()->socket(), session.second->p2pID());
            session.second->session()->disconnect(
                bcos::network::DisconnectReason::NotInWhitelistReason);
        }
    }
}

bcos::task::Task<std::optional<Message>> bcos::gateway::Service::sendMessageByNodeID(
    bcos::network::P2pID nodeID, Message& header, ::ranges::any_view<bytesConstRef> payloads,
    bcos::network::Options options)
{
    if (m_router)
    {
        // router path (merged from the former ServiceV2): this node originates the message, so
        // stamp src/dst before routing
        header.setSrcP2PNodeID(m_nodeID);
        header.setDstP2PNodeID(nodeID);
        co_return co_await forwardMessageByNodeID(nodeID, header, std::move(payloads), options);
    }
    co_return co_await directSendMessageByNodeID(
        std::move(nodeID), header, std::move(payloads), options);
}
bool bcos::gateway::Service::active()
{
    return m_run;
}
bcos::network::P2pID bcos::gateway::Service::id() const
{
    return m_nodeID;
}
// Start a per-type pump that runs `_handler` on every inbound message of `_type`, in arrival
// order.
//
// The pump's lifetime is the CHANNEL's, not m_run's: Service::stop() closes every channel, which is
// what ends the pumps (see handlerPump). Keying the loop on m_run instead would be wrong twice
// over — it would kill pumps registered before start() (every handler is registered in a
// constructor, and the router's are registered before Service::start runs), and a channel with a
// dead pump silently accumulates messages.
void bcos::gateway::Service::subscribeHandler(
    uint16_t _type, CoroutineHandler _handler, std::size_t _capacity)
{
    auto channel = subscribe(
        _type,
        [this](std::function<void()> wake) {
            if (m_host)
            {
                m_host->asioInterface()->post(std::move(wake));
            }
            else
            {
                // No network wired in yet (only reachable from a test that registered a handler
                // before setHost): resume inline rather than dropping the wake-up.
                wake();
            }
        },
        _capacity);
    task::wait(handlerPump(_type, std::move(channel), std::move(_handler)));
}

// The pump: one long-lived coroutine per type, holding the channel and the handler for its whole
// life. It exits when the channel closes — Service::stop() closes them all — and that close
// rethrows the teardown NetworkException, which is the normal exit. Nothing else ends it, so no
// frame leaks while the service runs and no message is silently dropped.
task::Task<void> bcos::gateway::Service::handlerPump(
    uint16_t _type, std::shared_ptr<InboundChannel> _channel, CoroutineHandler _handler)
{
    while (true)
    {
        try
        {
            auto inbound = co_await _channel->recv();
            if (!co_await _handler(std::move(inbound)))
            {
                co_return;  // the handler asked to stop this type's pump
            }
        }
        catch (bcos::network::NetworkException const& e)
        {
            SERVICE_LOG(INFO) << LOG_DESC("handler pump exit, channel closed")
                              << LOG_KV("packetType", _type)
                              << LOG_KV("code", bcos::network::errorCodeOf(e));
            co_return;
        }
        catch (std::exception const& e)
        {
            // recv() throws ONLY once the channel is closed and drained (Channel's contract) —
            // the pump's normal exit, whatever the close error's type (a bare close() delivers a
            // plain runtime_error, which must not spin the loop). Anything from the HANDLER is
            // one bad message: log it and take the next one.
            if (_channel->closed())
            {
                SERVICE_LOG(INFO) << LOG_DESC("handler pump exit, channel closed")
                                  << LOG_KV("packetType", _type)
                                  << LOG_KV("what", boost::diagnostic_information(e));
                co_return;
            }
            SERVICE_LOG(WARNING) << LOG_DESC("handler pump message failed, continuing")
                                 << LOG_KV("packetType", _type)
                                 << LOG_KV("what", boost::diagnostic_information(e));
        }
    }
}

std::shared_ptr<bcos::gateway::InboundChannel> bcos::gateway::Service::subscribe(
    uint16_t _type, InboundChannel::Poster _poster, std::size_t _capacity)
{
    return subscribeChannel(_type, std::move(_poster), _capacity);
}

std::shared_ptr<bcos::gateway::InboundChannel> bcos::gateway::Service::subscribeChannel(
    uint16_t _type, InboundChannel::Poster _poster, std::size_t _capacity)
{
    std::lock_guard lock(x_channels);
    auto& slot = m_channels.at(_type);
    if (!slot)
    {
        // Count AND byte bounded (the byte measure is inboundMessageBytes): the session recv
        // channel already caps what one peer can pin — this budget caps what a slow per-type
        // consumer lets the whole node pin, keeping the FIB-184 bound intact one hop downstream.
        slot = std::make_shared<InboundChannel>(std::move(_poster), _capacity,
            DEFAULT_INBOUND_QUEUE_BYTES, inboundMessageBytes, InboundChannel::WaitPolicy::FirstFit);
    }
    return slot;
}

std::shared_ptr<bcos::gateway::InboundChannel> bcos::gateway::Service::inboundChannel(
    uint16_t _type) const
{
    std::lock_guard lock(x_channels);
    return m_channels.at(_type);
}

void bcos::gateway::Service::registerUnreachableHandler(std::function<void(std::string)> _handler)
{
    if (!m_router)
    {
        return;
    }
    WriteGuard writeGuard(m_router->x_unreachableHandlers);
    m_router->unreachableHandlers.emplace_back(std::move(_handler));
}
std::map<NodeIPEndpoint, bcos::network::P2pID> bcos::gateway::Service::staticNodes()
{
    // FIB-186 (vector D): read m_staticNodes under x_nodes -- the same member heartBeat now
    // snapshots under the lock. Returning it unlocked races with onConnect's updateStaticNodes
    // writer (the exact data race this fix closes inline in heartBeat).
    std::shared_lock lock(x_nodes);
    return m_staticNodes;
}
void bcos::gateway::Service::setStaticNodes(const std::set<NodeIPEndpoint>& staticNodes)
{
    std::unique_lock nodeLock(x_nodes);
    m_staticNodes.clear();
    for (const auto& endpoint : staticNodes)
    {
        m_staticNodes.insert(std::make_pair(endpoint, ""));
    }
}
bcos::gateway::P2PInfo bcos::gateway::Service::localP2pInfo()
{
    // the identity fields come from the P2P identity object (was Host::p2pInfo before the
    // identity seam moved P2PInfo out of libnetwork); the endpoint is transport state
    P2PInfo p2pInfo;
    if (m_peerIdentity && m_host && m_host->asioInterface() &&
        m_host->asioInterface()->srvContext())
    {
        auto* cert =
            SSL_CTX_get0_certificate(m_host->asioInterface()->srvContext()->native_handle());
        p2pInfo = m_peerIdentity->selfIdentity(cert);
        p2pInfo.nodeIPEndpoint = NodeIPEndpoint(m_host->listenHost(), m_host->listenPort());
    }
    p2pInfo.p2pID = m_nodeID;
    return p2pInfo;
}
bool bcos::gateway::Service::isReachable(bcos::network::P2pID const& _nodeID) const
{
    if (m_router)
    {
        auto reachableNodes = m_router->routerTable->getAllReachableNode();
        return reachableNodes.contains(_nodeID);
    }
    return isConnected(_nodeID);
}
bcos::gateway::P2PHost::Ptr bcos::gateway::Service::host()
{
    return m_host;
}
void bcos::gateway::Service::setHost(P2PHost::Ptr host)
{
    m_host = std::move(host);
}
void bcos::gateway::Service::setDispatchBudgetConfig(DispatchBudgetConfig _config)
{
    if (_config.peerBytes == 0 || _config.peerCount == 0 ||
        _config.globalBytes / 2 < _config.peerBytes || _config.globalCount <= _config.peerCount)
    {
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment("global dispatch budget must be at least 2x peer "
                                                  "bytes and greater than peer count"));
    }
    std::lock_guard lock(x_dispatchBudgets);
    if (m_globalDispatchBudget)
    {
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(
                "dispatch budget config must be set before the first PeerToPeerMessage"));
    }
    m_dispatchConfig = std::move(_config);
}

bcos::gateway::DispatchBudget::Poster bcos::gateway::Service::makeDispatchPoster()
{
    if (m_dispatchConfig.poster)
    {
        return m_dispatchConfig.poster;
    }
    return [weakService = weak_from_this()](std::function<void()> wake) {
        auto service = weakService.lock();
        if (service && service->m_host && service->m_host->haveNetwork())
        {
            service->m_host->asioInterface()->post(std::move(wake));
        }
        else
        {
            wake();
        }
    };
}

bcos::gateway::DispatchBudget::Ptr bcos::gateway::Service::ensureGlobalDispatchBudget()
{
    std::lock_guard lock(x_dispatchBudgets);
    if (m_dispatchStopped)
    {
        return nullptr;
    }
    if (!m_globalDispatchBudget)
    {
        m_globalDispatchBudget = std::make_shared<DispatchBudget>(m_dispatchConfig.globalBytes,
            m_dispatchConfig.globalCount, makeDispatchPoster(),
            DispatchBudget::WaitPolicy::FirstFit, std::chrono::milliseconds(CHECK_INTERVAL));
    }
    return m_globalDispatchBudget;
}

bcos::gateway::DispatchBudget::Ptr bcos::gateway::Service::dispatchBudgetFor(P2PSession& _session)
{
    std::lock_guard lock(x_dispatchBudgets);
    if (m_dispatchStopped)
    {
        return nullptr;
    }
    if (_session.service().expired())
    {
        _session.setService(weak_from_this());
    }
    if (_session.dispatchOwner() == 0)
    {
        _session.setDispatchOwner(m_nextDispatchOwner++);
    }
    auto const& p2pID = _session.p2pID();
    auto it = m_peerDispatchBudgets.find(p2pID);
    if (it != m_peerDispatchBudgets.end())
    {
        return it->second;
    }
    auto budget = std::make_shared<DispatchBudget>(
        m_dispatchConfig.peerBytes, m_dispatchConfig.peerCount, makeDispatchPoster());
    m_peerDispatchBudgets.emplace(p2pID, budget);
    return budget;
}

bcos::gateway::DispatchBudget::Ptr bcos::gateway::Service::findDispatchBudget(
    bcos::network::P2pID const& _p2pID) const
{
    std::lock_guard lock(x_dispatchBudgets);
    auto it = m_peerDispatchBudgets.find(_p2pID);
    return it == m_peerDispatchBudgets.end() ? nullptr : it->second;
}

void bcos::gateway::Service::cancelDispatch(
    bcos::network::P2pID const& _p2pID, std::uint64_t _owner)
{
    DispatchBudget::Ptr peerBudget;
    DispatchBudget::Ptr globalBudget;
    {
        std::lock_guard lock(x_dispatchBudgets);
        auto it = m_peerDispatchBudgets.find(_p2pID);
        if (it != m_peerDispatchBudgets.end())
        {
            peerBudget = it->second;
        }
        globalBudget = m_globalDispatchBudget;
    }
    auto owner = std::to_string(_owner);
    if (peerBudget)
    {
        peerBudget->cancelWaiters(owner);
    }
    if (globalBudget)
    {
        globalBudget->cancelWaiters(owner);
    }
}

void bcos::gateway::Service::closeDispatchBudgets()
{
    std::lock_guard connectStopLock(m_connectStopMutex);
    std::vector<DispatchBudget::Ptr> budgets;
    {
        std::lock_guard lock(x_dispatchBudgets);
        if (m_dispatchStopped)
        {
            return;
        }
        m_dispatchStopped = true;
        if (m_globalDispatchBudget)
        {
            budgets.push_back(m_globalDispatchBudget);
        }
        for (auto const& [p2pID, budget] : m_peerDispatchBudgets)
        {
            budgets.push_back(budget);
        }
    }
    for (auto const& budget : budgets)
    {
        budget->close();
    }
}

void bcos::gateway::Service::setPeerIdentity(std::shared_ptr<P2PPeerIdentity> _peerIdentity)
{
    m_peerIdentity = std::move(_peerIdentity);
}
uint32_t bcos::gateway::Service::newSeq()
{
    // Service-wide monotonic allocator (was Host::newSeq before correlation moved up from
    // libnetwork). Uniqueness scope must match m_pendingResponses: the whole node.
    return ++m_seq;
}
bool bcos::gateway::Service::registerPendingResponse(uint32_t _seq, PendingResponse::Ptr _pending)
{
    return m_pendingResponses.add(_seq, std::move(_pending));
}
bcos::gateway::PendingResponse::Ptr bcos::gateway::Service::claimPendingResponse(uint32_t _seq)
{
    return m_pendingResponses.claim(_seq);
}
void bcos::gateway::Service::onResponseTimeout(uint32_t _seq)
{
    auto pending = claimPendingResponse(_seq);
    if (!pending)
    {
        return;  // already settled by ack / disconnect-flush / write-failure reclaim
    }
    pending->callback(bcos::network::makeNetworkException(
                          bcos::network::P2PExceptionType::NetworkTimeout, "NetworkTimeout"),
        std::nullopt);
}
void bcos::gateway::Service::onResponseFrame(
    uint32_t _seq, Session::Ptr const& _session, bcos::network::FrameMeta _meta)
{
    auto pending = claimPendingResponse(_seq);
    // without a pending entry: the request already timed out or was settled elsewhere
    if (!pending)
    {
        SERVICE_LOG(WARNING) << LOG_BADGE("onResponseFrame")
                             << LOG_DESC("pending request not found, maybe it timed out")
                             << LOG_KV("endpoint",
                                    _session ? _session->nodeIPEndpoint() : NodeIPEndpoint())
                             << LOG_KV("seq", _seq);
        return;
    }
    if (pending->timeoutHandler)
    {
        pending->timeoutHandler->cancel();
    }
    // complete inline: this handler already runs on a pool thread (Session::onMessage posts the
    // delivery), so resuming the waiter here cannot land on a sender's stack
    pending->callback(bcos::network::NetworkException{}, std::move(_meta));
}
void bcos::gateway::Service::failPendingResponsesOf(
    Session::Ptr const& _session, bcos::network::NetworkException _error)
{
    settleClaimedPending(m_pendingResponses.claimAllOf(_session.get()), _error);
}
void bcos::gateway::Service::failAllPendingResponses(bcos::network::NetworkException _error)
{
    settleClaimedPending(m_pendingResponses.claimAll(), _error);
}
void bcos::gateway::Service::settleClaimedPending(
    std::vector<PendingResponse::Ptr> _pendings, bcos::network::NetworkException const& _error)
{
    for (auto& pending : _pendings)
    {
        if (pending->timeoutHandler)
        {
            pending->timeoutHandler->cancel();
        }
        // Complete OFF this stack while the network is up (see the declaration comment): the
        // resumed waiter's continuation must not run on the caller's stack — drop() is reachable
        // from a sender's own await_suspend — nor occupy the single-thread teardown executor.
        if (m_host && m_host->haveNetwork())
        {
            m_host->asioInterface()->post(
                [callback = std::move(pending->callback), _error]() mutable {
                    try
                    {
                        callback(_error, std::nullopt);
                    }
                    catch (std::exception const& e)
                    {
                        SERVICE_LOG(WARNING) << LOG_DESC("pending response callback exception")
                                             << LOG_KV("what", boost::diagnostic_information(e));
                    }
                });
        }
        else
        {
            try
            {
                pending->callback(_error, std::nullopt);
            }
            catch (std::exception const& e)
            {
                SERVICE_LOG(WARNING) << LOG_DESC("pending response callback exception")
                                     << LOG_KV("what", boost::diagnostic_information(e));
            }
        }
    }
}
std::shared_ptr<bcos::crypto::KeyFactory> bcos::gateway::Service::keyFactory()
{
    return m_keyFactory;
}
void bcos::gateway::Service::setKeyFactory(std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory)
{
    m_keyFactory = std::move(_keyFactory);
}
void bcos::gateway::Service::registerDisconnectHandler(
    std::function<void(bcos::network::NetworkException, P2PSession::Ptr)> _handler)
{
    m_disconnectionHandlers.push_back(std::move(_handler));
}
std::shared_ptr<P2PSession> bcos::gateway::Service::getP2PSessionByNodeIdWithoutLock(
    bcos::network::P2pID const& _nodeID) const
{
    if (auto it = m_sessions.find(_nodeID); it != m_sessions.end())
    {
        return it->second;
    }
    return nullptr;
}


void bcos::gateway::Service::setBeforeMessageHandler(
    std::function<std::optional<bcos::Error>(Session&, const Message&, uint32_t)> _handler)
{
    m_beforeMessageHandler = std::move(_handler);
}
void bcos::gateway::Service::setOnMessageHandler(
    std::function<std::optional<bcos::Error>(Session::Ptr, const Message&)> _handler)
{
    m_onMessageHandler = std::move(_handler);
}
void bcos::gateway::Service::registerOnNewSession(std::function<void(P2PSession::Ptr)> _handler)
{
    m_newSessionHandlers.emplace_back(std::move(_handler));
}
void bcos::gateway::Service::registerOnDeleteSession(std::function<void(P2PSession::Ptr)> _handler)
{
    m_deleteSessionHandlers.emplace_back(std::move(_handler));
}
void bcos::gateway::Service::callNewSessionHandlers(const P2PSession::Ptr& _session)
{
    try
    {
        for (auto const& handler : m_newSessionHandlers)
        {
            handler(_session);
        }
    }
    catch (std::exception const& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("callNewSessionHandlers exception")
                             << LOG_KV("msg", boost::diagnostic_information(e));
    }
}
void bcos::gateway::Service::callDeleteSessionHandlers(const P2PSession::Ptr& _session)
{
    try
    {
        for (auto const& handler : m_deleteSessionHandlers)
        {
            handler(_session);
        }
    }
    catch (std::exception const& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("callDeleteSessionHandlers exception")
                             << LOG_KV("msg", boost::diagnostic_information(e));
    }
}