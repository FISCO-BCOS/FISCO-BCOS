#pragma once

#include "bcos-tars-protocol/tars/FrontService.h"
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-tars-protocol/protocol/GroupNodeInfoImpl.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/RefDataContainer.h>
#include <range/v3/view/any_view.hpp>

namespace bcostars
{
class FrontServiceClient : public bcos::front::FrontServiceInterface
{
public:
    void start() override;
    void stop() override;

    FrontServiceClient(bcostars::FrontServicePrx proxy, bcos::crypto::KeyFactory::Ptr keyFactory);

    bcos::task::Task<std::tuple<bcos::Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr>>
    getGroupNodeInfo() override;

    bcos::task::Task<bcos::Error::Ptr> onReceiveGroupNodeInfo(
        std::string _groupID, bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo) override;

    bcos::task::Task<bcos::Error::Ptr> onReceiveMessage(
        std::string _groupID, bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data) override;

    bcos::task::Task<bcos::Error::Ptr> onReceiveBroadcastMessage(std::string _groupID,
        bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data) override;

    bcos::task::Task<bcos::Error::Ptr> sendResponse(const std::string& _id, int _moduleID,
        bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data) override;

    // (coroutine) send message to one node and await the module-level response via the
    // front-service RPC
    bcos::task::Task<bcos::front::SendResult> sendMessageByNodeID(int _moduleID,
        bcos::crypto::NodeIDPtr _nodeID,
        ::ranges::any_view<bcos::bytesConstRef, ::ranges::category::forward> _payloads,
        uint32_t _timeout) override;

    bcos::task::Task<void> broadcastMessage(uint16_t _type, int _moduleID,
        ::ranges::any_view<bcos::bytesConstRef, ::ranges::category::forward> payloads) override;

private:
    // 30s
    const int c_frontServiceTimeout = 30000;

    bcostars::FrontServicePrx m_proxy;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    std::string const c_moduleName = "FrontServiceClient";
};
}  // namespace bcostars
