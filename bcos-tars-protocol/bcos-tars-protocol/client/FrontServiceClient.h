#pragma once

#include "bcos-tars-protocol/tars/FrontService.h"
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/front/FrontServiceTypeDef.h>
#include <bcos-tars-protocol/protocol/GroupNodeInfoImpl.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/RefDataContainer.h>
#include <memory>
#include <range/v3/view/any_view.hpp>
#include <range/v3/view/single.hpp>

namespace bcostars
{
/**
 * @brief: tars client of a remote front service (pro/max mode). Exposes the same send/receive
 *         method signatures as the in-process bcos::front::FrontService so the gateway can
 *         dispatch statically over both (see bcos-gateway's FrontServiceHandle).
 *
 * enable_shared_from_this lets the owned-payload bridges pass an owning Ptr as the coroutine
 * parameter, so the client (always shared_ptr-owned) stays alive for the whole detached send
 * instead of holding a raw `this`.
 */
class FrontServiceClient : public std::enable_shared_from_this<FrontServiceClient>
{
public:
    using Ptr = std::shared_ptr<FrontServiceClient>;

    void start();
    void stop();

    FrontServiceClient(bcostars::FrontServicePrx proxy, bcos::crypto::KeyFactory::Ptr keyFactory);

    void asyncGetGroupNodeInfo(bcos::front::GetGroupNodeInfoFunc _onGetGroupNodeInfo);

    void onReceiveGroupNodeInfo(const std::string& _groupID,
        bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo,
        bcos::front::ReceiveMsgFunc _receiveMsgCallback);

    void onReceiveMessage(const std::string& _groupID, const bcos::crypto::NodeIDPtr& _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback);

    // Note: the _receiveMsgCallback maybe null in some cases
    void onReceiveBroadcastMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback);

    void asyncSendResponse(const std::string& _id, int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback);

    // (coroutine) send message to one node and await the module-level response via the
    // front-service RPC
    bcos::task::Task<bcos::front::SendResult> sendMessageByNodeID(int _moduleID,
        bcos::crypto::NodeIDPtr _nodeID,
        ::ranges::any_view<bcos::bytesConstRef, ::ranges::category::forward> _payloads,
        uint32_t _timeout);

    bcos::task::Task<void> broadcastMessage(uint16_t _type, int _moduleID,
        ::ranges::any_view<bcos::bytesConstRef, ::ranges::category::forward> payloads);

    /**
     * @brief broadcast an already-encoded message, taking ownership of the payload so the send can
     *        be deferred without copying the message body. Bridges to broadcastMessage on the
     *        caller thread: the tars client has no in-process gateway lock to contend, so the
     *        synchronous bridge cannot block on a lock the caller may already hold. The owned
     *        payload is kept alive for the duration of the send.
     */
    void asyncBroadcastMessageByOwnedPayload(uint16_t type, int moduleID, bcos::bytesPointer payload)
    {
        bcos::task::wait([](FrontServiceClient::Ptr self, uint16_t _type, int _moduleID,
                             bcos::bytesPointer _payload) -> bcos::task::Task<void> {
            co_await self->broadcastMessage(
                _type, _moduleID, ::ranges::views::single(bcos::ref(*_payload)));
        }(shared_from_this(), type, moduleID, std::move(payload)));
    }

    /**
     * @brief send an already-encoded message to one node, taking ownership of the payload so the
     *        send can be deferred without the caller keeping the buffer alive. Same rationale as
     *        asyncBroadcastMessageByOwnedPayload; bridges to the coroutine sendMessageByNodeID
     *        (fire-and-forget).
     */
    void asyncSendMessageByNodeIDByOwnedPayload(
        int moduleID, bcos::crypto::NodeIDPtr nodeID, bcos::bytesPointer payload)
    {
        bcos::task::wait([](FrontServiceClient::Ptr self, int _moduleID,
                             bcos::crypto::NodeIDPtr _nodeID,
                             bcos::bytesPointer _payload) -> bcos::task::Task<void> {
            co_await self->sendMessageByNodeID(_moduleID, std::move(_nodeID),
                ::ranges::views::single(bcos::ref(*_payload)), 0);
        }(shared_from_this(), moduleID, std::move(nodeID), std::move(payload)));
    }

private:
    // 30s
    const int c_frontServiceTimeout = 30000;

    bcostars::FrontServicePrx m_proxy;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    std::string const c_moduleName = "FrontServiceClient";
};
}  // namespace bcostars
