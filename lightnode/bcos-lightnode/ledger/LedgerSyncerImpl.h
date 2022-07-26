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
template <bcos::concepts::ledger::Ledger FromLedgerType,
    bcos::concepts::ledger::Ledger ToLedgerType>
class LedgerSyncerImpl
  : public bcos::concepts::sync::BlockSyncerBase<LedgerSyncerImpl<FromLedgerType, ToLedgerType>>
{
public:
    LedgerSyncerImpl(FromLedgerType fromLedger, ToLedgerType toLedger)
      : bcos::concepts::sync::BlockSyncerBase<LedgerSyncerImpl<FromLedgerType, ToLedgerType>>(),
        m_fromLedger(std::move(fromLedger)),
        m_toLedger(std::move(toLedger))
    {}

    template <bcos::concepts::block::Block BlockType>
    void impl_sync()
    {
        auto& toLedger = bcos::concepts::getRef(m_toLedger);
        auto& fromLedger = bcos::concepts::getRef(m_fromLedger);

        auto toStatus = toLedger.getStatus();
        auto fromStatus = fromLedger.getStatus();

        for (auto blockNumber = toStatus.blockNumber; blockNumber < fromStatus.blockNumber;
             ++blockNumber)
        {
            BlockType block;
            fromLedger.getBlock(blockNumber, block);
            toLedger.setBlock(std::move(block));
        }

        // std::promise<std::tuple<Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr>> nodeInfofPromise;
        // m_front->asyncGetGroupNodeInfo([&nodeInfofPromise](Error::Ptr _error,
        //                                    bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo) {
        //     nodeInfofPromise.set_value(std::tuple{std::move(_error), std::move(_groupNodeInfo)});
        // });
        // auto nodeInfoFuture = nodeInfofPromise.get_future();

        // auto [error, nodeInfo] = nodeInfoFuture.get();
        // if (error)
        //     BOOST_THROW_EXCEPTION(*error);

        // for (auto& it : nodeInfo->nodeIDList())
        // {
        //     auto count = ledger().getStatus();
        //     auto blockHeight = count.blockNumber;
        //     // auto nodeID =
        //     m_keyFactory->createKey(bcos::bytesConstRef((bcos::byte*)it.data(), it.size()));

        // RequestType request;
        // request.beginBlockNumber = blockHeight;
        // request.endBlockNumber = blockHeight + m_requestBlockCount;
        // request.onlyHeader = true;

        //     if (!response->blocks.empty())
        //     {
        //         // TODO: Verify the signer
        //         for (auto& block : response->blocks)
        //         {
        //             ledger().setBlock(std::move(block));
        //         }

        //         // If got block, break current query
        //         break;
        //     }
        // }
    }

private:
    FromLedgerType m_fromLedger;
    ToLedgerType m_toLedger;

    constexpr static auto m_requestBlockCount = 10u;
};
}  // namespace bcos::sync