
/** @file SessionFace.h
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 *
 * @author: yujiechen
 * @date: 2018-09-19
 * @modification: remove addNote interface
 */

#pragma once
#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/FrameMeta.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Error.h"
#include <boost/asio.hpp>
#include <optional>
#include <range/v3/view/any_view.hpp>


namespace bcos::gateway
{
class SocketFace;

// The session interface of a framed byte-stream transport. Deliberately message-agnostic:
// outbound, the caller supplies an already-encoded header plus payload views (the wire format
// belongs to the layer above); inbound, frames arrive as FrameMeta produced by the session's
// decoder (see FrameMeta.h).
class SessionFace
{
public:
    using Ptr = std::shared_ptr<SessionFace>;

    SessionFace() = default;
    SessionFace(const SessionFace&) = delete;
    SessionFace(SessionFace&&) = delete;
    SessionFace& operator=(SessionFace&&) = delete;
    SessionFace& operator=(const SessionFace&) = delete;
    virtual ~SessionFace() noexcept = default;

    virtual void start() = 0;
    virtual void disconnect(DisconnectReason) = 0;

    // Send one frame: header (already encoded, WITHOUT the payload) followed by the payload
    // views. The caller keeps every referenced buffer alive until the returned task completes.
    // `seq` correlates a response when options.response is set; the response arrives as the
    // decoded FrameMeta (nullopt on failure/timeout).
    virtual task::Task<std::optional<FrameMeta>> fastSendMessage(bytesConstRef header,
        ::ranges::any_view<bytesConstRef> payloads, uint32_t seq, Options options) = 0;

    virtual std::shared_ptr<SocketFace> socket() = 0;

    // The handler is invoked with the decoded frame by value (move it out if it must outlive
    // the call); on error delivery (e.g. teardown notification) the frame is
    // default-constructed and the error carries the cause.
    virtual void setMessageHandler(
        std::function<void(NetworkException, SessionFace::Ptr, FrameMeta)> messageHandler) = 0;

    virtual NodeIPEndpoint nodeIPEndpoint() const = 0;

    // FIB-184: bind an opaque object to this session's lifetime (released when the session is
    // destroyed). Non-pure so existing test doubles need not implement it.
    virtual void setLifetimeGuard(std::shared_ptr<void> /*guard*/) {}

    virtual bool active() const = 0;

    virtual std::size_t writeQueueSize() = 0;

    // Response-correlation bookkeeping: the response-callback manager is shared host-wide, so a
    // routed response can be claimed on a different session than the request went out on — the
    // claiming session clears the OWNER's pending seq through this hook (see
    // ResponseCallback::owner).
    virtual void removePendingResponseSeq(uint32_t seq) = 0;
};
}  // namespace bcos::gateway
