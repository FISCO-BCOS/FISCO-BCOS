#pragma once

#include "bcos-utilities/FixedBytes.h"
#include <bcos-framework/concepts/Serialize.h>
#include <bcos-framework/concepts/ledger/Ledger.h>
#include <bcos-framework/concepts/sync/SyncBlockMessages.h>
#include <bcos-framework/front/FrontServiceInterface.h>
#include <boost/throw_exception.hpp>
#include <exception>
#include <future>

namespace bcos::sync
{
template <bcos::concepts::ledger::Ledger LedgerType, bcos::concepts::sync::RequestBlock RequestType,
    bcos::concepts::sync::ResponseBlock ResponseType>
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
            auto count = m_ledger.getTotalTransactionCount();
            auto blockHeight = count.blockNumber;

            RequestType request;
            request.currentBlockHeight = blockHeight;
            request.requestBlockCount = m_requestBlockCount;

            auto buffer = request.encode();

            std::promise<std::tuple<std::exception_ptr, std::optional<ResponseType>>> promise;
            auto future = promise.get_future();

            m_front->asyncSendMessageByNodeID(0, it, bytesConstRef{buffer}, 0,
                [&future](Error::Ptr _error, [[maybe_unused]] bcos::crypto::NodeIDPtr _nodeID,
                    bytesConstRef _data, [[maybe_unused]] const std::string& _id,
                    [[maybe_unused]] front::ResponseFunc _respFunc) {
                    try
                    {
                        if (_error)
                        {
                            BOOST_THROW_EXCEPTION(*_error);
                            return;
                        }

                        std::optional<ResponseType> response;
                        bcos::concepts::serialize::decode(*response, _data);

                        future.set_value({std::move(_error), std::move(response)});
                    }
                    catch (...)
                    {
                        auto exception = std::current_exception();
                        future.set_value({std::move(exception), {}});
                    }
                });

            auto result = future.get();
            auto& [error, response] = result;
            if (error)
                std::rethrow_exception(error);

            // TODO: Verify the signer
            for (auto& block : response.blocks)
            {
                m_ledger.setBlock(block);
            }
        }
    }

private:
    bcos::front::FrontServiceInterface::Ptr m_front;
    LedgerType m_ledger;

    bcos::gateway::GroupNodeInfo::Ptr m_nodes;
    std::mutex m_nodesMutex;

    constexpr static auto m_requestBlockCount = 10u;
};
}  // namespace bcos::sync