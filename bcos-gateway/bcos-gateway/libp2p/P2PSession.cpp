/** @file P2PSession.cpp
 *  @author monan
 *  @date 20181112
 */

#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/Common.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/ZstdCompress.h"
#include <bcos-task/Wait.h>
#include <boost/container/small_vector.hpp>
#include <range/v3/view/all.hpp>
#include <range/v3/view/single.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;

P2PSession::P2PSession()
  : m_p2pInfo(std::make_shared<P2PInfo>()),
    m_protocolInfo(std::make_shared<bcos::protocol::ProtocolInfo>())
{
    // init with the minVersion
    m_protocolInfo->setVersion(m_protocolInfo->minVersion());
    P2PSESSION_LOG(INFO) << "[P2PSession::P2PSession] this=" << this;
}

P2PSession::~P2PSession()
{
    P2PSESSION_LOG(INFO) << "[P2PSession::~P2PSession] this=" << this;
}

bool P2PSession::active()
{
    return m_run;
}

SessionFace::Ptr P2PSession::session()
{
    return m_session;
}

void P2PSession::setSession(std::shared_ptr<SessionFace> session)
{
    m_session = std::move(session);
}

P2pID P2PSession::p2pID()
{
    return m_p2pInfo->rawP2pID;
}

std::string P2PSession::printP2pID()
{
    return printShortP2pID(m_p2pInfo->rawP2pID);
}

void P2PSession::setP2PInfo(P2PInfo const& p2pInfo)
{
    *m_p2pInfo = p2pInfo;
    m_p2pInfo->nodeIPEndpoint = m_session->nodeIPEndpoint();
}

std::shared_ptr<P2PInfo> P2PSession::mutableP2pInfo()
{
    return m_p2pInfo;
}

std::weak_ptr<Service> P2PSession::service()
{
    return m_service;
}

void P2PSession::setService(std::weak_ptr<Service> service)
{
    m_service = service;
}

void P2PSession::setProtocolInfo(bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo)
{
    WriteGuard guard(x_protocolInfo);
    *m_protocolInfo = *_protocolInfo;
}

bcos::protocol::ProtocolInfo::ConstPtr P2PSession::protocolInfo() const
{
    ReadGuard guard(x_protocolInfo);
    return m_protocolInfo;
}

void P2PSession::start()
{
    P2PSESSION_LOG(INFO) << "[P2PSession::start] this=" << this;
    if (!m_run && m_session)
    {
        m_run = true;

        m_session->start();
        heartBeat();
    }
}

void P2PSession::stop(DisconnectReason reason)
{
    if (m_run)
    {
        m_run = false;
        if (m_session && m_session->active())
        {
            m_session->disconnect(reason);
        }
    }
}

void P2PSession::heartBeat()
{
    auto service = m_service.lock();
    if (service && service->active())
    {
        if (m_session && m_session->active())
        {
            if (c_fileLogLevel <= TRACE) [[unlikely]]
            {
                P2PSESSION_LOG(TRACE) << LOG_DESC("P2PSession onHeartBeat")
                                      << LOG_KV("p2pid", printShortP2pID(m_p2pInfo->p2pID))
                                      << LOG_KV("endpoint", m_session->nodeIPEndpoint());
            }
            // value message in frame, sent through the fast path (zero-copy). The service shared_ptr
            // is passed as a coroutine parameter so it is copied into the frame and kept alive for
            // the whole (possibly deferred) send. The pre-send checks (outgoing rate limit / max
            // size) run synchronously on the caller thread and may throw — catch so the heartbeat
            // timer below is always re-armed (otherwise this session would be dropped by the peer's
            // idle timeout).
            auto self = shared_from_this();
            try
            {
                task::wait([](std::shared_ptr<P2PSession> _self) -> task::Task<void> {
                    Message message;
                    message.setPacketType(GatewayMessageType::Heartbeat);
                    ::ranges::any_view<bytesConstRef> emptyPayloads;
                    co_await _self->fastSendP2PMessage(
                        message, std::move(emptyPayloads), Options{});
                }(self));
            }
            catch (std::exception const& e)
            {
                P2PSESSION_LOG(WARNING) << LOG_DESC("heartBeat send exception")
                                        << LOG_KV("p2pid", printShortP2pID(m_p2pInfo->p2pID))
                                        << LOG_KV("what", boost::diagnostic_information(e));
            }
        }

        auto self = std::weak_ptr<P2PSession>(shared_from_this());
        m_timer.emplace(service->host()->asioInterface()->newTimer(HEARTBEAT_INTERVEL));
        m_timer->async_wait([self](boost::system::error_code e) {
            if (e)
            {
                P2PSESSION_LOG(TRACE) << "Timer canceled: " << e.message();
                return;
            }

            auto s = self.lock();
            if (s)
            {
                s->heartBeat();
            }
        });
    }
}

bcos::task::Task<std::optional<Message>> P2PSession::fastSendP2PMessage(
    Message& message, ::ranges::any_view<bytesConstRef> payloads, Options options)
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
    service->resetP2pID(message, (ProtocolVersion)m_protocolInfo->version());
    // the p2p message version must match the negotiated protocol version of this session: the
    // encodeHeaderImpl of Message only encodes the ttl/src/dst routing fields for version > V0,
    // so sending with the default (V0) version would silently drop the V2 routing fields and break
    // multi-hop forwarding through ServiceV2 router tables
    message.setVersion((uint16_t)m_protocolInfo->version());

    // Materialize the payload views once: the incoming any_view is category::input (single-pass),
    // while the size/join passes below each iterate the payloads.
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
        BOOST_THROW_EXCEPTION(NetworkException(-1, "encode header failed"));
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
        BOOST_THROW_EXCEPTION(NetworkException((int64_t)error.errorCode(), error.errorMessage()));
    }

    ::ranges::any_view<bytesConstRef> wirePayloads =
        hasWirePayloadOverride ?
            ::ranges::any_view<bytesConstRef>(::ranges::views::single(wirePayloadOverride)) :
            ::ranges::any_view<bytesConstRef>(::ranges::views::all(payloadRefs));

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
    auto response = co_await m_session->fastSendMessage(
        ref(headerBuffer), std::move(wirePayloads), message.seq(), options);
    if (!response)
    {
        co_return std::nullopt;
    }
    // Decode the response frame back into a Message (the session delivers raw frames now).
    Message respMessage;
    if (respMessage.decode(ref(response->frame)) < 0) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(NetworkException(
            P2PExceptionType::ProtocolError, "ProtocolError(decode response message error)"));
    }
    co_return respMessage;
}