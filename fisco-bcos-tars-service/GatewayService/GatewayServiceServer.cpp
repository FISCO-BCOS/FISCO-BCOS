#include "GatewayServiceServer.h"
#include "bcos-task/Wait.h"
#include <bcos-tars-protocol/Common.h>
#include <boost/exception/diagnostic_information.hpp>
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
    const std::vector<tars::Char>& _data, tars::Int32& _type, std::vector<tars::Char>&,
    tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto gateway = m_gatewayInitializer->gateway();
    // copy the payload and pass all state as coroutine parameters so it stays alive for the whole
    // (possibly deferred) send; try/catch guarantees the RPC is always answered even if the send
    // throws (current->setResponse(false) already disabled the automatic reply)
    auto requestData = std::make_shared<std::vector<tars::Char>>(_data);
    bcos::task::wait([](auto _gateway, auto _topic, auto _requestData,
                         auto _current) -> bcos::task::Task<void> {
        try
        {
            auto [error, type, responseData] = co_await _gateway->sendMessageByTopic(_topic,
                bcos::bytesConstRef(
                    (const bcos::byte*)_requestData->data(), _requestData->size()));
            std::vector<tars::Char> response(responseData.begin(), responseData.end());
            async_response_asyncSendMessageByTopic(
                _current, toTarsError(error), type, response);
        }
        catch (std::exception const& e)
        {
            GATEWAYSERVICE_LOG(WARNING) << LOG_DESC("asyncSendMessageByTopic send exception")
                                        << LOG_KV("topic", _topic)
                                        << LOG_KV("what", boost::diagnostic_information(e));
            async_response_asyncSendMessageByTopic(_current,
                toTarsError(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e))), 0, {});
        }
    }(gateway, _topic, requestData, current));
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
    const std::string& _topic, const std::vector<tars::Char>& _data, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto gateway = m_gatewayInitializer->gateway();
    // copy the payload into the coroutine frame so it stays alive until the send completes
    auto requestData = std::make_shared<std::vector<tars::Char>>(_data);
    bcos::task::wait([](auto _gateway, auto _topic,
                         auto _requestData) -> bcos::task::Task<void> {
        co_await _gateway->sendBroadcastMessageByTopic(_topic,
            bcos::bytesConstRef((const bcos::byte*)_requestData->data(), _requestData->size()));
    }(gateway, _topic, requestData));
    async_response_asyncSendBroadcastMessageByTopic(
        current, toTarsError<bcos::Error::Ptr>(nullptr));
    return {};
}

bcostars::Error GatewayServiceServer::asyncRemoveTopic(const std::string& _clientID,
    const std::vector<std::string>& _topicList, tars::TarsCurrentPtr current)
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
    const std::string& groupID, tars::Int32 moduleID, const std::vector<tars::Char>& srcNodeID,
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
    auto gateway = m_gatewayInitializer->gateway();
    bcos::task::wait(
        [](auto _gateway, auto _current) -> bcos::task::Task<void> {
            try
            {
                auto [error, localP2pInfo, peers] = co_await _gateway->getPeers();
                auto localtarsP2pInfo = toTarsGatewayInfo(localP2pInfo);
                std::vector<bcostars::GatewayInfo> peersInfo;
                if (peers)
                {
                    for (auto const& peer : *peers)
                    {
                        peersInfo.emplace_back(toTarsGatewayInfo(peer));
                    }
                }
                async_response_asyncGetPeers(
                    _current, toTarsError(error), localtarsP2pInfo, peersInfo);
            }
            catch (std::exception const& e)
            {
                GATEWAYSERVICE_LOG(WARNING) << LOG_DESC("asyncGetPeers exception")
                                            << LOG_KV("what", boost::diagnostic_information(e));
                async_response_asyncGetPeers(_current,
                    toTarsError(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e))),
                    bcostars::GatewayInfo(), {});
            }
        }(gateway, current));
    return {};
}
bcostars::Error bcostars::GatewayServiceServer::asyncSendMessageByNodeID(const std::string& groupID,
    tars::Int32 moduleID, const std::vector<tars::Char>& srcNodeID, const std::vector<tars::Char>& dstNodeID,
    const std::vector<tars::Char>& payload, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto keyFactory = m_gatewayInitializer->keyFactory();
    auto bcosSrcNodeID = keyFactory->createKey(
        bcos::bytesConstRef((const bcos::byte*)srcNodeID.data(), srcNodeID.size()));
    auto bcosDstNodeID = keyFactory->createKey(
        bcos::bytesConstRef((const bcos::byte*)dstNodeID.data(), dstNodeID.size()));
    auto gateway = m_gatewayInitializer->gateway();
    // keep the payload alive and pass all state as coroutine parameters so it is copied into the
    // frame and stays alive for the whole (possibly deferred) send; the send result is delivered
    // through the async RPC response. try/catch guarantees the RPC is always answered even if the
    // send throws (current->setResponse(false) already disabled the automatic reply).
    auto payloadData = std::make_shared<std::vector<tars::Char>>(payload);
    bcos::task::wait([](auto _gateway, auto _groupID, auto _moduleID, auto _srcNodeID,
                         auto _dstNodeID, auto _payloadData,
                         auto _current) -> bcos::task::Task<void> {
        try
        {
            auto error = co_await _gateway->sendMessageByNodeID(_groupID, _moduleID, _srcNodeID,
                _dstNodeID,
                ::ranges::views::single(bcos::bytesConstRef(
                    (const bcos::byte*)_payloadData->data(), _payloadData->size())));
            async_response_asyncSendMessageByNodeID(_current, toTarsError(error));
        }
        catch (std::exception const& e)
        {
            GATEWAYSERVICE_LOG(WARNING) << LOG_DESC("asyncSendMessageByNodeID send exception")
                                        << LOG_KV("groupID", _groupID)
                                        << LOG_KV("moduleID", _moduleID)
                                        << LOG_KV("dst", _dstNodeID->hex())
                                        << LOG_KV("what", boost::diagnostic_information(e));
            async_response_asyncSendMessageByNodeID(
                _current, toTarsError(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e))));
        }
    }(gateway, groupID, moduleID, bcosSrcNodeID, bcosDstNodeID, payloadData, current));
    return {};
}
bcostars::Error bcostars::GatewayServiceServer::asyncSendMessageByNodeIDs(
    const std::string& groupID, tars::Int32 moduleID, const std::vector<tars::Char>& srcNodeID,
    const std::vector<std::vector<tars::Char>>& dstNodeID, const std::vector<tars::Char>& payload,
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

    auto gateway = m_gatewayInitializer->gateway();
    auto payloadData = std::make_shared<std::vector<tars::Char>>(payload);
    // fan out one detached task::wait per destination (sharing the owned payload), so a slow or
    // unreachable destination does not head-of-line-block the others; each send is wrapped in
    // try/catch so an exception cannot leak out of a TARS/ASIO completion handler
    for (auto const& dstNodeID : nodeIDs)
    {
        bcos::task::wait([](auto _gateway, auto _groupID, auto _moduleID, auto _srcNodeID,
                             auto _dstNodeID, auto _payloadData) -> bcos::task::Task<void> {
            try
            {
                co_await _gateway->sendMessageByNodeID(_groupID, _moduleID, _srcNodeID, _dstNodeID,
                    ::ranges::views::single(bcos::bytesConstRef(
                        (const bcos::byte*)_payloadData->data(), _payloadData->size())));
            }
            catch (std::exception const& e)
            {
                GATEWAYSERVICE_LOG(WARNING)
                    << LOG_DESC("asyncSendMessageByNodeIDs send exception")
                    << LOG_KV("groupID", _groupID) << LOG_KV("moduleID", _moduleID)
                    << LOG_KV("dst", _dstNodeID->hex())
                    << LOG_KV("what", boost::diagnostic_information(e));
            }
        }(gateway, groupID, moduleID, bcosSrcNodeID, dstNodeID, payloadData));
    }

    async_response_asyncSendMessageByNodeIDs(current, toTarsError<bcos::Error::Ptr>(nullptr));
    return bcostars::Error();
}
bcostars::Error bcostars::GatewayServiceServer::asyncGetGroupNodeInfo(
    const std::string& groupID, GroupNodeInfo&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto gateway = m_gatewayInitializer->gateway();
    bcos::task::wait(
        [](auto _gateway, auto _groupID, auto _current) -> bcos::task::Task<void> {
            try
            {
                auto [error, bcosGroupNodeInfo] =
                    co_await _gateway->getGroupNodeInfo(_groupID);
                // Note: the nodeIDs maybe null if no connections
                if (!bcosGroupNodeInfo || bcosGroupNodeInfo->nodeIDList().empty())
                {
                    async_response_asyncGetGroupNodeInfo(
                        _current, toTarsError(error), bcostars::GroupNodeInfo());
                    co_return;
                }
                auto groupInfoImpl =
                    std::dynamic_pointer_cast<bcostars::protocol::GroupNodeInfoImpl>(
                        bcosGroupNodeInfo);
                async_response_asyncGetGroupNodeInfo(
                    _current, toTarsError(error), groupInfoImpl->inner());
            }
            catch (std::exception const& e)
            {
                GATEWAYSERVICE_LOG(WARNING) << LOG_DESC("asyncGetGroupNodeInfo exception")
                                            << LOG_KV("groupID", _groupID)
                                            << LOG_KV("what", boost::diagnostic_information(e));
                async_response_asyncGetGroupNodeInfo(_current,
                    toTarsError(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e))),
                    bcostars::GroupNodeInfo());
            }
        }(gateway, groupID, current));
    return {};
}
