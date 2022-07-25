#pragma once

#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include <bcos-concepts/Serialize.h>
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/sync/BlockSyncerClient.h>
#include <bcos-concepts/sync/SyncBlockMessages.h>
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/front/FrontServiceInterface.h>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <exception>
#include <future>
#include <iterator>


namespace bcos::sync
{
template <bcos::concepts::ledger::Ledger LedgerType, bcos::concepts::sync::RequestBlock RequestType,
    bcos::concepts::sync::ResponseBlock ResponseType>
class BlockSyncerClientImpl : public bcos::concepts::sync::BlockSyncerClientBase<
                                  BlockSyncerClientImpl<LedgerType, RequestType, ResponseType>>
{
public:
    BlockSyncerClientImpl(LedgerType ledger, bcos::front::FrontServiceInterface::Ptr front,
        bcos::crypto::KeyFactory::Ptr keyFactory)
      : bcos::concepts::sync::BlockSyncerClientBase<
            BlockSyncerClientImpl<LedgerType, RequestType, ResponseType>>(),
        m_ledger(std::move(ledger)),
        m_front(std::move(front)),
        m_keyFactory(std::move(keyFactory))
    {}

    void impl_fetchAndStoreNewBlocks()
    {
        std::promise<std::tuple<Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr>> nodeInfofPromise;
        m_front->asyncGetGroupNodeInfo([&nodeInfofPromise](Error::Ptr _error,
                                           bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo) {
            nodeInfofPromise.set_value(std::tuple{std::move(_error), std::move(_groupNodeInfo)});
        });
        auto nodeInfoFuture = nodeInfofPromise.get_future();

        auto [error, nodeInfo] = nodeInfoFuture.get();
        if (error)
            BOOST_THROW_EXCEPTION(*error);

        for (auto& it : nodeInfo->nodeIDList())
        {
            auto count = ledger().getTotalTransactionCount();
            auto blockHeight = count.blockNumber;
            auto nodeID =
                m_keyFactory->createKey(bcos::bytesConstRef((bcos::byte*)it.data(), it.size()));

            RequestType request;
            request.beginBlockNumber = blockHeight;
            request.endBlockNumber = blockHeight + m_requestBlockCount;
            request.onlyHeader = true;

            std::vector<bcos::byte> buffer;
            bcos::concepts::serialize::encode(request, buffer);

            std::promise<std::tuple<std::exception_ptr, std::optional<ResponseType>>> promise;
            m_front->asyncSendMessageByNodeID(0, std::move(nodeID),
                bcos::bytesConstRef(buffer.data(), buffer.size()), 0,
                [&promise](Error::Ptr _error, [[maybe_unused]] bcos::crypto::NodeIDPtr _nodeID,
                    bytesConstRef _data, [[maybe_unused]] const std::string& _id,
                    [[maybe_unused]] front::ResponseFunc _respFunc) {
                    try
                    {
                        if (_error)
                        {
                            BOOST_THROW_EXCEPTION(*_error);
                            return;
                        }

                        std::optional<ResponseType> response{ResponseType()};
                        bcos::concepts::serialize::decode(_data, *response);

                        promise.set_value(std::tuple{nullptr, std::move(response)});
                    }
                    catch (...)
                    {
                        auto exception = std::current_exception();
                        promise.set_value({std::move(exception), {}});
                    }
                });

            auto future = promise.get_future();
            auto result = future.get();
            auto& [error, response] = result;
            if (error)
                std::rethrow_exception(error);

            if (!response->blocks.empty())
            {
                // TODO: Verify the signer
                for (auto& block : response->blocks)
                {
                    ledger().setBlock(std::move(block));
                }

                // If got block, break current query
                break;
            }
        }
    }

private:
    constexpr auto& ledger()
    {
        if constexpr (bcos::concepts::PointerLike<LedgerType>)
        {
            return *m_ledger;
        }
        else
        {
            return m_ledger;
        }
    }

    LedgerType m_ledger;
    bcos::front::FrontServiceInterface::Ptr m_front;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;

    constexpr static auto m_requestBlockCount = 10u;
};
}  // namespace bcos::sync