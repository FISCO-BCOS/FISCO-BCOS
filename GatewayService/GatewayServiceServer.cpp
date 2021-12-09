#include "GatewayService/GatewayServiceServer.h"
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
    return bcostars::Error();
}

bcostars::Error GatewayServiceServer::asyncSendMessageByTopic(const std::string& _topic,
    const vector<tars::Char>& _data, tars::Int32& _type, vector<tars::Char>&,
    tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_gatewayInitializer->gateway()->asyncSendMessageByTopic(_topic,
        bcos::bytesConstRef((const bcos::byte*)_data.data(), _data.size()),
        [current](bcos::Error::Ptr&& _error, int16_t _type, bcos::bytesPointer _responseData) {
            vector<tars::Char> response;
            if (_responseData)
            {
                response.assign(_responseData->begin(), _responseData->end());
            }
            async_response_asyncSendMessageByTopic(current, toTarsError(_error), _type, response);
        });
    return bcostars::Error();
}

bcostars::Error GatewayServiceServer::asyncSubscribeTopic(
    const std::string& _clientID, const std::string& _topicInfo, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_gatewayInitializer->gateway()->asyncSubscribeTopic(
        _clientID, _topicInfo, [current](bcos::Error::Ptr&& _error) {
            async_response_asyncSubscribeTopic(current, toTarsError(_error));
        });
    return bcostars::Error();
}
bcostars::Error GatewayServiceServer::asyncSendBroadbastMessageByTopic(
    const std::string& _topic, const vector<tars::Char>& _data, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_gatewayInitializer->gateway()->asyncSendBroadbastMessageByTopic(
        _topic, bcos::bytesConstRef((const bcos::byte*)_data.data(), _data.size()));
    async_response_asyncSendBroadbastMessageByTopic(current, toTarsError(nullptr));
    return bcostars::Error();
}

bcostars::Error GatewayServiceServer::asyncRemoveTopic(const std::string& _clientID,
    const vector<std::string>& _topicList, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_gatewayInitializer->gateway()->asyncRemoveTopic(
        _clientID, _topicList, [current](bcos::Error::Ptr&& _error) {
            async_response_asyncRemoveTopic(current, toTarsError(_error));
        });
    return bcostars::Error();
}