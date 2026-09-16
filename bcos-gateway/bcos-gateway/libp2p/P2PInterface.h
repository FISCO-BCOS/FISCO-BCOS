/** @file P2PInterface.h
 *  @author chaychen
 *  @date 20180911
 */

#pragma once

#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-task/Task.h"
#include <optional>

namespace bcos
{
namespace stat
{
class NetworkStatHandler;
class ChannelNetworkStatHandler;
}  // namespace stat

namespace gateway
{
class P2PSession;
using CallbackFuncWithSession =
    std::function<void(NetworkException, std::shared_ptr<P2PSession>, Message)>;
using DisconnectCallbackFuncWithSession =
    std::function<void(NetworkException, std::shared_ptr<P2PSession>)>;
class P2PInterface
{
public:
    using Ptr = std::shared_ptr<P2PInterface>;
    virtual ~P2PInterface() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual P2pID id() const = 0;

    virtual task::Task<std::optional<Message>> sendMessageByNodeID(P2pID nodeID, Message& header,
        ::ranges::any_view<bytesConstRef> payloads, Options options = {}) = 0;

    // (coroutine) broadcast a message to all connected/reachable nodes. The message is handed over
    // as a shared_ptr: the per-peer fan-out tasks keep it alive (the payload rides as a view,
    // zero-copy).
    virtual task::Task<void> broadcastMessageToAll(Message::Ptr message,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads,
        Options options = {}) = 0;

    // (coroutine) send a message to each of the given p2p nodes. One independent coroutine is
    // fanned out per node (no head-of-line blocking on a stalled peer's socket write); a
    // failed/unreachable node is logged and skipped (fire-and-forget per node). The payload bytes
    // are owned by the frame so the caller does not need to keep them alive.
    virtual task::Task<void> sendMessageByNodeIDs(uint16_t _type,
        const std::vector<P2pID>& _nodeIDs, bcos::bytes _payload, Options options = {}) = 0;

    virtual P2PInfos sessionInfos() = 0;
    virtual P2PInfo localP2pInfo() = 0;

    virtual bool isConnected(P2pID const& _nodeID) const = 0;
    virtual bool isReachable(P2pID const& _nodeID) const = 0;
    virtual std::shared_ptr<Host> host() = 0;

    // host-wide seq allocator (see Host::newSeq): unique within this node's response-callback
    // manager, which is shared by all sessions of the host
    virtual uint32_t newSeq() = 0;

    virtual std::shared_ptr<P2PSession> getP2PSessionByNodeId(P2pID const& _nodeID) const = 0;

    using MessageHandler =
        std::function<void(NetworkException, std::shared_ptr<P2PSession>, Message)>;

    virtual bool registerHandlerByMsgType(uint16_t _type, MessageHandler const& _msgHandler) = 0;

    virtual void eraseHandlerByMsgType(uint16_t _type) = 0;

    virtual void sendRespMessageBySession(bytesConstRef _payload, uint32_t _requestSeq,
        std::string _requestSrcP2PNodeID, std::shared_ptr<P2PSession> _p2pSession) = 0;

    virtual void updatePeerBlacklist(const std::set<std::string>& _strList, const bool _enable) = 0;
    virtual void updatePeerWhitelist(const std::set<std::string>& _strList, const bool _enable) = 0;
};

}  // namespace gateway

}  // namespace bcos
