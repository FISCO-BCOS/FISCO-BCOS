#pragma once

#include "../Common/TarsUtils.h"
#include "GatewayInitializer.h"
#include "libinitializer/ProtocolInitializer.h"
#include <bcos-tars-protocol/Common.h>
#include <bcos-tars-protocol/ErrorConverter.h>
#include <bcos-tars-protocol/protocol/GroupNodeInfoImpl.h>
#include <bcos-tars-protocol/tars/GatewayService.h>
#include <chrono>
#include <mutex>

namespace bcostars
{
struct GatewayServiceParam
{
    GatewayInitializer::Ptr gatewayInitializer;
};
class GatewayServiceServer : public bcostars::GatewayService
{
public:
    GatewayServiceServer(GatewayServiceParam const& _param);
    void initialize() override;
    void destroy() override;

    bcostars::Error asyncSendBroadcastMessage(tars::Int32 _type, const std::string& groupID,
        tars::Int32 moduleID, const vector<tars::Char>& srcNodeID,
        const std::vector<tars::Char>& payload, tars::TarsCurrentPtr current) override;

    bcostars::Error asyncGetPeers(bcostars::GatewayInfo&, std::vector<bcostars::GatewayInfo>&,
        tars::TarsCurrentPtr current) override;

    bcostars::Error asyncSendMessageByNodeID(const std::string& groupID, tars::Int32 moduleID,
        const vector<tars::Char>& srcNodeID, const vector<tars::Char>& dstNodeID,
        const vector<tars::Char>& payload, tars::TarsCurrentPtr current) override;

    bcostars::Error asyncSendMessageByNodeIDs(const std::string& groupID, tars::Int32 moduleID,
        const vector<tars::Char>& srcNodeID, const vector<vector<tars::Char>>& dstNodeID,
        const vector<tars::Char>& payload, tars::TarsCurrentPtr current) override;

    bcostars::Error asyncGetGroupNodeInfo(
        const std::string& groupID, GroupNodeInfo&, tars::TarsCurrentPtr current) override;
    bcostars::Error asyncNotifyGroupInfo(
        const bcostars::GroupInfo& groupInfo, tars::TarsCurrentPtr current) override;

    bcostars::Error asyncSendMessageByTopic(const std::string& _topic,
        const vector<tars::Char>& _data, tars::Int32& _type, vector<tars::Char>& _responseData,
        tars::TarsCurrentPtr current) override;
    bcostars::Error asyncSubscribeTopic(const std::string& _clientID, const std::string& _topicInfo,
        tars::TarsCurrentPtr current) override;
    bcostars::Error asyncSendBroadcastMessageByTopic(const std::string& _topic,
        const vector<tars::Char>& _data, tars::TarsCurrentPtr current) override;
    bcostars::Error asyncRemoveTopic(const std::string& _clientID,
        const vector<std::string>& _topicList, tars::TarsCurrentPtr current) override;

private:
    GatewayInitializer::Ptr m_gatewayInitializer;
};
}  // namespace bcostars