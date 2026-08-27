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

    m_frontServiceInitializer->front()->asyncGetGroupNodeInfo(
        [current](bcos::Error::Ptr _error, bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo) {
            // Note: the nodeIDs maybe null if no connections
            std::vector<std::vector<char>> tarsNodeIDs;
            if (!_groupNodeInfo)
            {
                async_response_asyncGetGroupNodeInfo(
                    current, toTarsError(_error), bcostars::GroupNodeInfo());
                return;
            }
            auto groupInfoImpl =
                std::dynamic_pointer_cast<bcostars::protocol::GroupNodeInfoImpl>(_groupNodeInfo);
            async_response_asyncGetGroupNodeInfo(
                current, toTarsError(_error), groupInfoImpl->inner());
        });

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
    m_frontServiceInitializer->front()->asyncSendResponse(id, moduleID,
        m_frontServiceInitializer->keyFactory()->createKey(
            bcos::bytesConstRef((bcos::byte*)nodeID.data(), nodeID.size())),
        bcos::bytesConstRef((bcos::byte*)data.data(), data.size()),
        [current](bcos::Error::Ptr error) {
            async_response_asyncSendResponse(current, toTarsError(error));
        });
    return bcostars::Error();
}

bcostars::Error FrontServiceServer::onReceiveBroadcastMessage(const std::string& groupID,
    const std::vector<tars::Char>& nodeID, const std::vector<tars::Char>& data, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    m_frontServiceInitializer->front()->onReceiveBroadcastMessage(groupID,
        m_frontServiceInitializer->keyFactory()->createKey(
            bcos::bytesConstRef((bcos::byte*)nodeID.data(), nodeID.size())),
        bcos::bytesConstRef((bcos::byte*)data.data(), data.size()),
        [current](bcos::Error::Ptr error) {
            async_response_onReceiveBroadcastMessage(current, toTarsError(error));
        });

    return bcostars::Error();
}

bcostars::Error FrontServiceServer::onReceiveMessage(const std::string& groupID,
    const std::vector<tars::Char>& nodeID, const std::vector<tars::Char>& data, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    m_frontServiceInitializer->front()->onReceiveMessage(groupID,
        m_frontServiceInitializer->keyFactory()->createKey(
            bcos::bytesConstRef((bcos::byte*)nodeID.data(), nodeID.size())),
        bcos::bytesConstRef((bcos::byte*)data.data(), data.size()),
        [current](bcos::Error::Ptr error) {
            async_response_onReceiveMessage(current, toTarsError(error));
        });

    return bcostars::Error();
}

bcostars::Error FrontServiceServer::onReceiveGroupNodeInfo(const std::string& groupID,
    const bcostars::GroupNodeInfo& _groupNodeInfo, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto bcosGroupNodeInfo = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>(
        [m_groupNodeInfo = _groupNodeInfo]() mutable { return &m_groupNodeInfo; });
    m_frontServiceInitializer->front()->onReceiveGroupNodeInfo(
        groupID, bcosGroupNodeInfo, [current](bcos::Error::Ptr error) {
            async_response_onReceiveGroupNodeInfo(current, toTarsError(error));
        });
    return bcostars::Error();
}