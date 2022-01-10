#include "FrontServiceServer.h"

using namespace bcostars;

bcostars::Error FrontServiceServer::asyncGetNodeIDs(
    vector<vector<tars::Char>>& nodeIDs, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    m_frontServiceInitializer->front()->asyncGetNodeIDs(
        [current](bcos::Error::Ptr _error, std::shared_ptr<const bcos::crypto::NodeIDs> _nodeIDs) {
            // Note: the nodeIDs maybe null if no connections
            std::vector<std::vector<char>> tarsNodeIDs;
            if (!_nodeIDs)
            {
                async_response_asyncGetNodeIDs(current, toTarsError(_error), tarsNodeIDs);
                return;
            }
            tarsNodeIDs.reserve(_nodeIDs->size());
            for (auto const& it : *_nodeIDs)
            {
                auto nodeIDData = it->data();
                tarsNodeIDs.emplace_back(nodeIDData.begin(), nodeIDData.end());
            }
            async_response_asyncGetNodeIDs(current, toTarsError(_error), tarsNodeIDs);
        });

    return bcostars::Error();
}

void FrontServiceServer::asyncSendBroadcastMessage(tars::Int32 _nodeType, tars::Int32 moduleID,
    const vector<tars::Char>& data, tars::TarsCurrentPtr)
{
    m_frontServiceInitializer->front()->asyncSendBroadcastMessage(
        _nodeType, moduleID, bcos::bytesConstRef((bcos::byte*)data.data(), data.size()));
}

bcostars::Error FrontServiceServer::asyncSendMessageByNodeID(tars::Int32 moduleID,
    const vector<tars::Char>& nodeID, const vector<tars::Char>& data, tars::UInt32 timeout,
    tars::Bool requireRespCallback, vector<tars::Char>& responseNodeID,
    vector<tars::Char>& responseData, std::string& seq, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    auto bcosNodeID = m_frontServiceInitializer->keyFactory()->createKey(
        bcos::bytesConstRef((bcos::byte*)nodeID.data(), nodeID.size()));
    if (requireRespCallback)
    {
        m_frontServiceInitializer->front()->asyncSendMessageByNodeID(moduleID, bcosNodeID,
            bcos::bytesConstRef((bcos::byte*)data.data(), data.size()), timeout,
            [current](bcos::Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
                bcos::bytesConstRef _data, const std::string& _id,
                bcos::front::ResponseFunc _respFunc) {
                boost::ignore_unused(_respFunc);
                auto encodedNodeID = *_nodeID->encode();
                async_response_asyncSendMessageByNodeID(current, toTarsError(_error),
                    std::vector<char>(encodedNodeID.begin(), encodedNodeID.end()),
                    std::vector<char>(_data.begin(), _data.end()), _id);
            });
    }
    else
    {
        m_frontServiceInitializer->front()->asyncSendMessageByNodeID(moduleID, bcosNodeID,
            bcos::bytesConstRef((bcos::byte*)data.data(), data.size()), timeout, nullptr);

        // response directly
        bcos::bytesConstRef respData;
        async_response_asyncSendMessageByNodeID(current, toTarsError(nullptr),
            std::vector<char>(nodeID.begin(), nodeID.end()),
            std::vector<char>(respData.begin(), respData.end()), seq);
    }

    return bcostars::Error();
}

void FrontServiceServer::asyncSendMessageByNodeIDs(tars::Int32 moduleID,
    const vector<vector<tars::Char>>& nodeIDs, const vector<tars::Char>& data,
    tars::TarsCurrentPtr current)
{
    std::vector<bcos::crypto::NodeIDPtr> bcosNodeIDs;
    bcosNodeIDs.reserve(nodeIDs.size());
    for (auto const& it : nodeIDs)
    {
        bcosNodeIDs.push_back(m_frontServiceInitializer->keyFactory()->createKey(
            bcos::bytesConstRef((bcos::byte*)it.data(), it.size())));
    }

    m_frontServiceInitializer->front()->asyncSendMessageByNodeIDs(
        moduleID, bcosNodeIDs, bcos::bytesConstRef((bcos::byte*)data.data(), data.size()));
}

bcostars::Error FrontServiceServer::asyncSendResponse(const std::string& id, tars::Int32 moduleID,
    const vector<tars::Char>& nodeID, const vector<tars::Char>& data, tars::TarsCurrentPtr current)
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
    const vector<tars::Char>& nodeID, const vector<tars::Char>& data, tars::TarsCurrentPtr current)
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
    const vector<tars::Char>& nodeID, const vector<tars::Char>& data, tars::TarsCurrentPtr current)
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

bcostars::Error FrontServiceServer::onReceivedNodeIDs(const std::string& groupID,
    const vector<vector<tars::Char>>& nodeIDs, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    auto bcosNodeIDs = std::make_shared<std::vector<bcos::crypto::NodeIDPtr>>();
    bcosNodeIDs->reserve(nodeIDs.size());

    for (auto const& it : nodeIDs)
    {
        bcosNodeIDs->push_back(m_frontServiceInitializer->keyFactory()->createKey(
            bcos::bytesConstRef((bcos::byte*)it.data(), it.size())));
    }

    m_frontServiceInitializer->front()->onReceiveNodeIDs(
        groupID, bcosNodeIDs, [current](bcos::Error::Ptr error) {
            async_response_onReceivedNodeIDs(current, toTarsError(error));
        });

    return bcostars::Error();
}