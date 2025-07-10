#pragma once

#include "bcos-tars-protocol/tars/FrontService.h"
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-tars-protocol/protocol/GroupNodeInfoImpl.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/RefDataContainer.h>

namespace bcostars
{
class FrontServiceClient : public bcos::front::FrontServiceInterface
{
public:
    void start() override;
    void stop() override;

    FrontServiceClient(bcostars::FrontServicePrx proxy, bcos::crypto::KeyFactory::Ptr keyFactory);

    void asyncGetGroupNodeInfo(bcos::front::GetGroupNodeInfoFunc _onGetGroupNodeInfo) override;

    void onReceiveGroupNodeInfo(const std::string& _groupID,
        bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo,
        bcos::front::ReceiveMsgFunc _receiveMsgCallback) override;

    void onReceiveMessage(const std::string& _groupID, const bcos::crypto::NodeIDPtr& _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback) override;

    // Note: the _receiveMsgCallback maybe null in some cases
    void onReceiveBroadcastMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback) override;

    // Note: the _callback maybe null in some cases
    void asyncSendMessageByNodeID(int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, uint32_t _timeout, bcos::front::CallbackFunc _callback) override;

    void asyncSendResponse(const std::string& _id, int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback) override;

    void asyncSendMessageByNodeIDs(int _moduleID,
        const std::vector<bcos::crypto::NodeIDPtr>& _nodeIDs, bcos::bytesConstRef _data) override;

    void asyncSendBroadcastMessage(
        uint16_t _type, int _moduleID, bcos::bytesConstRef _data) override;

    bcos::task::Task<void> broadcastMessage(
        uint16_t _type, int _moduleID, ::ranges::any_view<bcos::bytesConstRef> payloads) override;

private:
    // 30s
    const int c_frontServiceTimeout = 30000;

    bcostars::FrontServicePrx m_proxy;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    std::string const c_moduleName = "FrontServiceClient";
};
}  // namespace bcostars
