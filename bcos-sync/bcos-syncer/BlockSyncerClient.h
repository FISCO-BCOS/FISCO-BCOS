#pragma once

#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/protocol/SyncBlockMessages.h>
#include <bcos-framework/sync/BlockSyncer.h>

namespace bcos::sync
{
template <RequestBlockHeadersMessage RequestType, ResponseBlockHeadersMessage ResponseType>
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

        for (auto& it : nodes->nodeIDList()) {
            RequestType request;
            request.currentBlockHeight = 0;
            request.requestBlockCount = 10;
        }
    }

private:
    bcos::front::FrontServiceInterface::Ptr m_front;

    bcos::gateway::GroupNodeInfo::Ptr m_nodes;
    std::mutex m_nodesMutex;
};
}  // namespace bcos::sync