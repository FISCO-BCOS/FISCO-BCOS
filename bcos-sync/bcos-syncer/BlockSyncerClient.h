#pragma once

#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/protocol/SyncBlockMessages.h>
#include <bcos-framework/sync/BlockSyncer.h>

namespace bcos::sync
{
template <RequestBlockHeaders RequestType, ResponseBlockHeaders ResponseType>
class BlockSyncerClient
{
public:
    void fetchNewBlocks()
    {
        decltype(m_nodes) nodes;
        {
            auto lock = std::unique_lock{m_nodesMutex};
            nodes = m_nodes;
        }

        for (auto& it : nodes->nodeIDList())
        {
            RequestType request;
            request.currentBlockHeight = 0;

            auto buffer = request.encode();

            m_front->asyncSendMessageByNodeID(0, it, bytesConstRef{buffer}, 0,
                [](Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data, const std::string& _id,
                    front::ResponseFunc _respFunc) {
                });  // TODO: fixup
                     // moduleid
        }
    }

private:
    bcos::front::FrontServiceInterface::Ptr m_front;

    bcos::gateway::GroupNodeInfo::Ptr m_nodes;
    std::mutex m_nodesMutex;

    size_t m_requestBlockCount = 10;
};
}  // namespace bcos::sync