#include "GatewayServiceServer.h"
#include "bcos-task/Wait.h"
#include <bcos-tars-protocol/Common.h>
using namespace bcostars;
bcostars::Error GatewayServiceServer::asyncNotifyGroupInfo(
    const bcostars::GroupInfo& groupInfo, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto bcosGroupInfo = toBcosGroupInfo(m_gatewayInitializer->chainNodeInfoFactory(),
        m_gatewayInitializer->groupInfoFactory(), groupInfo);
    m_gatewayInitializer->gateway()->asyncNotifyGroupInfo(
        bcosGroupInfo, [current](bcos::Error::Ptr&& _error) {
            async_response_asyncNotifyGroupInfo(current, toTarsError(_error));
        });
    return {};
}

bcostars::Error GatewayServiceServer::asyncSendMessageByTopic(const std::string& _topic,
    const vector<tars::Char>& _data, tars::Int32& _type, vector<tars::Char>&,
    tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_gatewayInitializer->gateway()->asyncSendMessageByTopic(_topic,
        bcos::bytesConstRef((const bcos::byte*)_data.data(), _data.size()),
        [current](bcos::Error::Ptr&& _error, int16_t _type, bcos::bytesConstRef _responseData) {
            vector<tars::Char> response;
            if (_responseData)
            {
                response.assign(_responseData.begin(), _responseData.end());
            }
            async_response_asyncSendMessageByTopic(current, toTarsError(_error), _type, response);
        });
    return {};
}

bcostars::Error GatewayServiceServer::asyncSubscribeTopic(
    const std::string& _clientID, const std::string& _topicInfo, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_gatewayInitializer->gateway()->asyncSubscribeTopic(
        _clientID, _topicInfo, [current](bcos::Error::Ptr&& _error) {
            async_response_asyncSubscribeTopic(current, toTarsError(_error));
        });
    return {};
}
bcostars::Error GatewayServiceServer::asyncSendBroadcastMessageByTopic(
    const std::string& _topic, const vector<tars::Char>& _data, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_gatewayInitializer->gateway()->asyncSendBroadcastMessageByTopic(
        _topic, bcos::bytesConstRef((const bcos::byte*)_data.data(), _data.size()));
    async_response_asyncSendBroadcastMessageByTopic(
        current, toTarsError<bcos::Error::Ptr>(nullptr));
    return {};
}

bcostars::Error GatewayServiceServer::asyncRemoveTopic(const std::string& _clientID,
    const vector<std::string>& _topicList, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_gatewayInitializer->gateway()->asyncRemoveTopic(
        _clientID, _topicList, [current](bcos::Error::Ptr&& _error) {
            async_response_asyncRemoveTopic(current, toTarsError(_error));
        });
    return {};
}
bcostars::GatewayServiceServer::GatewayServiceServer(GatewayServiceParam const& _param)
  : m_gatewayInitializer(_param.gatewayInitializer)
{}
void bcostars::GatewayServiceServer::initialize() {}
void bcostars::GatewayServiceServer::destroy() {}
bcostars::Error bcostars::GatewayServiceServer::asyncSendBroadcastMessage(tars::Int32 _type,
    const std::string& groupID, tars::Int32 moduleID, const vector<tars::Char>& srcNodeID,
    const std::vector<tars::Char>& payload, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto bcosNodeID = m_gatewayInitializer->keyFactory()->createKey(
        bcos::bytesConstRef((const bcos::byte*)srcNodeID.data(), srcNodeID.size()));
    bcos::task::wait([](auto type, auto groupID, auto moduleID, auto srcNodeID, auto bcosNodeID,
                         auto gateway, auto payload) -> bcos::task::Task<void> {
        co_await gateway->broadcastMessage(type, groupID, moduleID, *bcosNodeID,
            ::ranges::views::single(
                bcos::bytesConstRef((const bcos::byte*)payload.data(), payload.size())));
    }(_type, groupID, moduleID, srcNodeID, bcosNodeID, m_gatewayInitializer->gateway(), payload));

    async_response_asyncSendBroadcastMessage(current, toTarsError<bcos::Error::Ptr>(nullptr));
    return {};
}
bcostars::Error bcostars::GatewayServiceServer::asyncGetPeers(
    bcostars::GatewayInfo&, std::vector<bcostars::GatewayInfo>&, tars::TarsCurrentPtr current)
{
    GATEWAYSERVICE_LOG(DEBUG) << LOG_DESC("asyncGetPeers: request");
    current->setResponse(false);
    m_gatewayInitializer->gateway()->asyncGetPeers(
        [current](const bcos::Error::Ptr _error, bcos::gateway::GatewayInfo::Ptr _localP2pInfo,
            bcos::gateway::GatewayInfosPtr _peers) {
            auto localtarsP2pInfo = toTarsGatewayInfo(_localP2pInfo);
            std::vector<bcostars::GatewayInfo> peersInfo;
            if (_peers)
            {
                for (auto const& peer : *_peers)
                {
                    peersInfo.emplace_back(toTarsGatewayInfo(peer));
                }
            }
            async_response_asyncGetPeers(current, toTarsError(_error), localtarsP2pInfo, peersInfo);
        });
    return {};
}
bcostars::Error bcostars::GatewayServiceServer::asyncSendMessageByNodeID(const std::string& groupID,
    tars::Int32 moduleID, const vector<tars::Char>& srcNodeID, const vector<tars::Char>& dstNodeID,
    const vector<tars::Char>& payload, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto keyFactory = m_gatewayInitializer->keyFactory();
    auto bcosSrcNodeID = keyFactory->createKey(
        bcos::bytesConstRef((const bcos::byte*)srcNodeID.data(), srcNodeID.size()));
    auto bcosDstNodeID = keyFactory->createKey(
        bcos::bytesConstRef((const bcos::byte*)dstNodeID.data(), dstNodeID.size()));

    m_gatewayInitializer->gateway()->asyncSendMessageByNodeID(groupID, moduleID, bcosSrcNodeID,
        bcosDstNodeID, bcos::bytesConstRef((const bcos::byte*)payload.data(), payload.size()),
        [current](bcos::Error::Ptr error) {
            async_response_asyncSendMessageByNodeID(current, toTarsError(error));
        });
    return {};
}
bcostars::Error bcostars::GatewayServiceServer::asyncSendMessageByNodeIDs(
    const std::string& groupID, tars::Int32 moduleID, const vector<tars::Char>& srcNodeID,
    const vector<vector<tars::Char>>& dstNodeID, const vector<tars::Char>& payload,
    tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto keyFactory = m_gatewayInitializer->keyFactory();
    auto bcosSrcNodeID = keyFactory->createKey(
        bcos::bytesConstRef((const bcos::byte*)srcNodeID.data(), srcNodeID.size()));
    std::vector<bcos::crypto::NodeIDPtr> nodeIDs;
    nodeIDs.reserve(dstNodeID.size());
    for (auto const& it : dstNodeID)
    {
        nodeIDs.push_back(
            keyFactory->createKey(bcos::bytesConstRef((const bcos::byte*)it.data(), it.size())));
    }

    m_gatewayInitializer->gateway()->asyncSendMessageByNodeIDs(groupID, moduleID, bcosSrcNodeID,
        nodeIDs, bcos::bytesConstRef((const bcos::byte*)payload.data(), payload.size()));

    async_response_asyncSendMessageByNodeIDs(current, toTarsError<bcos::Error::Ptr>(nullptr));
    return bcostars::Error();
}
bcostars::Error bcostars::GatewayServiceServer::asyncGetGroupNodeInfo(
    const std::string& groupID, GroupNodeInfo&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    m_gatewayInitializer->gateway()->asyncGetGroupNodeInfo(groupID,
        [current](bcos::Error::Ptr _error, bcos::gateway::GroupNodeInfo::Ptr _bcosGroupNodeInfo) {
            // Note: the nodeIDs maybe null if no connections
            if (!_bcosGroupNodeInfo || _bcosGroupNodeInfo->nodeIDList().empty())
            {
                async_response_asyncGetGroupNodeInfo(
                    current, toTarsError(_error), bcostars::GroupNodeInfo());
                return;
            }
            auto groupInfoImpl = std::dynamic_pointer_cast<bcostars::protocol::GroupNodeInfoImpl>(
                _bcosGroupNodeInfo);
            async_response_asyncGetGroupNodeInfo(
                current, toTarsError(_error), groupInfoImpl->inner());
        });

    return {};
}
