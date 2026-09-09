#include "FrontServiceServer.h"
#include "../Common/TarsUtils.h"
#include "bcos-task/Wait.h"
#include <bcos-tars-protocol/protocol/GroupNodeInfoImpl.h>
#include <boost/exception/diagnostic_information.hpp>

using namespace bcostars;

bcostars::Error FrontServiceServer::asyncGetGroupNodeInfo(
    GroupNodeInfo&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    bcos::task::wait([](auto _front, auto _current) -> bcos::task::Task<void> {
        try
        {
            auto [error, groupNodeInfo] = co_await _front->getGroupNodeInfo();
            // Note: the nodeIDs maybe null if no connections
            if (!groupNodeInfo)
            {
                async_response_asyncGetGroupNodeInfo(
                    _current, toTarsError(error), bcostars::GroupNodeInfo());
                co_return;
            }
            auto groupInfoImpl =
                std::dynamic_pointer_cast<bcostars::protocol::GroupNodeInfoImpl>(groupNodeInfo);
            async_response_asyncGetGroupNodeInfo(
                _current, toTarsError(error), groupInfoImpl->inner());
        }
        catch (std::exception const& e)
        {
            // ensure the RPC is always answered: current->setResponse(false) already disabled the
            // automatic reply, so an exception before async_response would leave the caller blocked
            FRONTSERVICE_LOG(WARNING) << LOG_DESC("asyncGetGroupNodeInfo exception")
                                      << LOG_KV("what", boost::diagnostic_information(e));
            async_response_asyncGetGroupNodeInfo(_current,
                toTarsError(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e))),
                bcostars::GroupNodeInfo());
        }
    }(m_frontServiceInitializer->front(), current));

    return bcostars::Error();
}

void FrontServiceServer::asyncSendBroadcastMessage(tars::Int32 _nodeType, tars::Int32 moduleID,
    const std::vector<tars::Char>& data, tars::TarsCurrentPtr)
{
    bcos::task::wait([](auto front, auto nodeType, auto moduleID,
                         auto data) -> bcos::task::Task<void> {
        co_await front->broadcastMessage(nodeType, moduleID,
            ::ranges::views::single(bcos::bytesConstRef((bcos::byte*)data.data(), data.size())));
    }(m_frontServiceInitializer->front(), _nodeType, moduleID, data));
}

bcostars::Error FrontServiceServer::asyncSendMessageByNodeID(tars::Int32 moduleID,
    const std::vector<tars::Char>& nodeID, const std::vector<tars::Char>& data, tars::UInt32 timeout,
    tars::Bool requireRespCallback, std::vector<tars::Char>& responseNodeID,
    std::vector<tars::Char>& responseData, std::string& seq, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    auto bcosNodeID = m_frontServiceInitializer->keyFactory()->createKey(
        bcos::bytesConstRef((bcos::byte*)nodeID.data(), nodeID.size()));
    auto front = m_frontServiceInitializer->front();
    // keep the payload, the request nodeID and the request seq alive by passing them as coroutine
    // parameters (they are copied into the frame and stay alive for the whole send)
    auto payloadData = std::make_shared<std::vector<tars::Char>>(data);
    auto requestNodeID = std::make_shared<std::vector<tars::Char>>(nodeID);
    auto requestSeq = std::make_shared<std::string>(seq);

    bcos::task::wait([](auto _front, auto _moduleID, auto _bcosNodeID, auto _payloadData,
                         auto _requestNodeID, auto _requestSeq, auto _timeout,
                         auto _requireRespCallback, auto _current) -> bcos::task::Task<void> {
        try
        {
            // requireRespCallback == false maps to a fire-and-forget send (timeout 0); the send
            // result (module response, timeout or gateway failure) is delivered through the async
            // RPC response
            auto result = co_await _front->sendMessageByNodeID(_moduleID, _bcosNodeID,
                ::ranges::views::single(bcos::bytesConstRef(
                    (const bcos::byte*)_payloadData->data(), _payloadData->size())),
                _requireRespCallback ? _timeout : 0);

            bcos::bytes encodedNodeID;
            if (result.nodeID)
            {
                encodedNodeID = result.nodeID->encode();
            }
            else
            {
                // fire-and-forget (or failed) send: echo the request nodeID, matching the previous
                // handler behaviour
                encodedNodeID.assign(_requestNodeID->begin(), _requestNodeID->end());
            }
            // fire-and-forget sends return an empty uuid: echo the request seq so clients that
            // correlate responses by seq still get a correlation ID
            std::string replySeq = result.uuid.empty() ? *_requestSeq : result.uuid;
            async_response_asyncSendMessageByNodeID(_current, toTarsError(result.error),
                std::vector<char>(encodedNodeID.begin(), encodedNodeID.end()),
                std::vector<char>(result.payload.begin(), result.payload.end()), replySeq);
        }
        catch (std::exception const& e)
        {
            // ensure the RPC is always answered: current->setResponse(false) already disabled the
            // automatic reply, so an exception before async_response would leave the caller blocked
            FRONTSERVICE_LOG(WARNING) << LOG_DESC("asyncSendMessageByNodeID send exception")
                                      << LOG_KV("moduleID", _moduleID)
                                      << LOG_KV("nodeID", _bcosNodeID->hex())
                                      << LOG_KV("what", boost::diagnostic_information(e));
            async_response_asyncSendMessageByNodeID(_current,
                toTarsError(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e))),
                std::vector<char>(_requestNodeID->begin(), _requestNodeID->end()),
                std::vector<tars::Char>(), *_requestSeq);
        }
    }(front, moduleID, bcosNodeID, payloadData, requestNodeID, requestSeq, timeout,
        requireRespCallback, current));

    return bcostars::Error();
}

void FrontServiceServer::asyncSendMessageByNodeIDs(tars::Int32 moduleID,
    const std::vector<std::vector<tars::Char>>& nodeIDs, const std::vector<tars::Char>& data,
    tars::TarsCurrentPtr current)
{
    std::vector<bcos::crypto::NodeIDPtr> bcosNodeIDs;
    bcosNodeIDs.reserve(nodeIDs.size());
    for (auto const& it : nodeIDs)
    {
        bcosNodeIDs.push_back(m_frontServiceInitializer->keyFactory()->createKey(
            bcos::bytesConstRef((bcos::byte*)it.data(), it.size())));
    }
    auto front = m_frontServiceInitializer->front();
    auto payloadData = std::make_shared<std::vector<tars::Char>>(data);
    // fan out one detached task::wait per destination (sharing the owned payload), so a slow or
    // unreachable destination does not head-of-line-block the others; each send is wrapped in
    // try/catch so an exception cannot leak out of a TARS/ASIO completion handler
    for (auto const& nodeID : bcosNodeIDs)
    {
        bcos::task::wait([](auto _front, auto _moduleID, auto _nodeID,
                             auto _payloadData) -> bcos::task::Task<void> {
            try
            {
                co_await _front->sendMessageByNodeID(_moduleID, _nodeID,
                    ::ranges::views::single(bcos::bytesConstRef(
                        (const bcos::byte*)_payloadData->data(), _payloadData->size())),
                    0);
            }
            catch (std::exception const& e)
            {
                FRONTSERVICE_LOG(WARNING)
                    << LOG_DESC("asyncSendMessageByNodeIDs send exception")
                    << LOG_KV("moduleID", _moduleID) << LOG_KV("nodeID", _nodeID->hex())
                    << LOG_KV("what", boost::diagnostic_information(e));
            }
        }(front, moduleID, nodeID, payloadData));
    }
}

bcostars::Error FrontServiceServer::asyncSendResponse(const std::string& id, tars::Int32 moduleID,
    const std::vector<tars::Char>& nodeID, const std::vector<tars::Char>& data, tars::TarsCurrentPtr current)
{
    FRONTSERVICE_LOG(TRACE) << LOG_DESC("asyncSendResponse server") << LOG_KV("id", id);
    current->setResponse(false);
    auto bcosNodeID = m_frontServiceInitializer->keyFactory()->createKey(
        bcos::bytesConstRef((bcos::byte*)nodeID.data(), nodeID.size()));
    // copy the payload into an owned coroutine parameter: the tars request buffer may not outlive
    // the detached coroutine
    auto payloadData = std::make_shared<std::vector<tars::Char>>(data);
    bcos::task::wait([](auto _front, auto _id, auto _moduleID, auto _bcosNodeID, auto _payloadData,
                         auto _current) -> bcos::task::Task<void> {
        try
        {
            auto error = co_await _front->sendResponse(_id, _moduleID, _bcosNodeID,
                bcos::bytesConstRef(
                    (const bcos::byte*)_payloadData->data(), _payloadData->size()));
            async_response_asyncSendResponse(_current, toTarsError(error));
        }
        catch (std::exception const& e)
        {
            FRONTSERVICE_LOG(WARNING) << LOG_DESC("asyncSendResponse exception")
                                      << LOG_KV("id", _id)
                                      << LOG_KV("what", boost::diagnostic_information(e));
            async_response_asyncSendResponse(_current,
                toTarsError(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e))));
        }
    }(m_frontServiceInitializer->front(), id, moduleID, bcosNodeID, payloadData, current));
    return bcostars::Error();
}

bcostars::Error FrontServiceServer::onReceiveBroadcastMessage(const std::string& groupID,
    const std::vector<tars::Char>& nodeID, const std::vector<tars::Char>& data, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    auto bcosNodeID = m_frontServiceInitializer->keyFactory()->createKey(
        bcos::bytesConstRef((bcos::byte*)nodeID.data(), nodeID.size()));
    // copy the tars request buffer into an owned coroutine parameter (it may not outlive the
    // detached coroutine)
    auto payloadData = std::make_shared<std::vector<tars::Char>>(data);
    bcos::task::wait([](auto _front, auto _groupID, auto _bcosNodeID, auto _payloadData,
                         auto _current) -> bcos::task::Task<void> {
        try
        {
            auto error = co_await _front->onReceiveBroadcastMessage(_groupID, _bcosNodeID,
                bcos::bytesConstRef(
                    (const bcos::byte*)_payloadData->data(), _payloadData->size()));
            async_response_onReceiveBroadcastMessage(_current, toTarsError(error));
        }
        catch (std::exception const& e)
        {
            FRONTSERVICE_LOG(WARNING) << LOG_DESC("onReceiveBroadcastMessage exception")
                                      << LOG_KV("groupID", _groupID)
                                      << LOG_KV("what", boost::diagnostic_information(e));
            async_response_onReceiveBroadcastMessage(_current,
                toTarsError(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e))));
        }
    }(m_frontServiceInitializer->front(), groupID, bcosNodeID, payloadData, current));

    return bcostars::Error();
}

bcostars::Error FrontServiceServer::onReceiveMessage(const std::string& groupID,
    const std::vector<tars::Char>& nodeID, const std::vector<tars::Char>& data, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    auto bcosNodeID = m_frontServiceInitializer->keyFactory()->createKey(
        bcos::bytesConstRef((bcos::byte*)nodeID.data(), nodeID.size()));
    // copy the tars request buffer into an owned coroutine parameter (it may not outlive the
    // detached coroutine)
    auto payloadData = std::make_shared<std::vector<tars::Char>>(data);
    bcos::task::wait([](auto _front, auto _groupID, auto _bcosNodeID, auto _payloadData,
                         auto _current) -> bcos::task::Task<void> {
        try
        {
            auto error = co_await _front->onReceiveMessage(_groupID, _bcosNodeID,
                bcos::bytesConstRef(
                    (const bcos::byte*)_payloadData->data(), _payloadData->size()));
            async_response_onReceiveMessage(_current, toTarsError(error));
        }
        catch (std::exception const& e)
        {
            FRONTSERVICE_LOG(WARNING) << LOG_DESC("onReceiveMessage exception")
                                      << LOG_KV("groupID", _groupID)
                                      << LOG_KV("what", boost::diagnostic_information(e));
            async_response_onReceiveMessage(_current,
                toTarsError(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e))));
        }
    }(m_frontServiceInitializer->front(), groupID, bcosNodeID, payloadData, current));

    return bcostars::Error();
}

bcostars::Error FrontServiceServer::onReceiveGroupNodeInfo(const std::string& groupID,
    const bcostars::GroupNodeInfo& _groupNodeInfo, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto bcosGroupNodeInfo = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>(
        [m_groupNodeInfo = _groupNodeInfo]() mutable { return &m_groupNodeInfo; });
    bcos::task::wait([](auto _front, auto _groupID, auto _bcosGroupNodeInfo,
                         auto _current) -> bcos::task::Task<void> {
        try
        {
            auto error = co_await _front->onReceiveGroupNodeInfo(_groupID, _bcosGroupNodeInfo);
            async_response_onReceiveGroupNodeInfo(_current, toTarsError(error));
        }
        catch (std::exception const& e)
        {
            FRONTSERVICE_LOG(WARNING) << LOG_DESC("onReceiveGroupNodeInfo exception")
                                      << LOG_KV("groupID", _groupID)
                                      << LOG_KV("what", boost::diagnostic_information(e));
            async_response_onReceiveGroupNodeInfo(_current,
                toTarsError(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e))));
        }
    }(m_frontServiceInitializer->front(), groupID, bcosGroupNodeInfo, current));
    return bcostars::Error();
}