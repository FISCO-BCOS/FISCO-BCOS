#pragma once

#include <bcos-concepts/Block.h>
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/sync/BlockSyncerServer.h>
#include <bcos-concepts/sync/SyncBlockMessages.h>
#include <boost/throw_exception.hpp>
#include <stdexcept>

namespace bcos::sync
{

template <bcos::concepts::ledger::Ledger LedgerType>
class BlockSyncerServerImpl
  : public bcos::concepts::sync::SyncBlockServerBase<BlockSyncerServerImpl<LedgerType>>
{
public:
    BlockSyncerServerImpl(LedgerType ledger) : m_ledger(std::move(ledger)) {}

    void impl_getBlock(bcos::concepts::sync::RequestBlock auto const& request,
        bcos::concepts::sync::ResponseBlock auto& response)
    {
        if (request.endBlockNumber <= request.beginBlockNumber)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Wrong block number range"});
        }

        auto transactionCount = ledger().getTotalTransactionCount();
        auto blockNumber = transactionCount.blockNumber;

        response.blockNumber = blockNumber;
        if (request.beginBlockNumber > blockNumber)
        {
            return;
        }
        auto count =
            std::min(blockNumber + 1, static_cast<decltype(blockNumber)>(request.endBlockNumber)) -
            request.beginBlockNumber;
        response.blocks.reserve(response.blocks.size() + count);

        constexpr static auto getHeader =
            [](decltype(ledger())& ledger, decltype(request.beginBlockNumber) blockNumber,
                RANGES::range_value_t<decltype(response.blocks)>& block) {
                ledger.template getBlock<concepts::ledger::HEADER>(blockNumber, block);
            };
        constexpr static auto getALL =
            [](decltype(ledger())& ledger, decltype(request.beginBlockNumber) blockNumber,
                RANGES::range_value_t<decltype(response.blocks)>& block) {
                ledger.template getBlock<concepts::ledger::ALL>(blockNumber, block);
            };
        std::function<void(decltype(ledger())& ledger, decltype(request.beginBlockNumber),
            RANGES::range_value_t<decltype(response.blocks)>&)>
            func = request.onlyHeader ? getHeader : getALL;

        for (auto currentBlockNumber = request.beginBlockNumber;
             currentBlockNumber < request.beginBlockNumber + count; ++currentBlockNumber)
        {
            RANGES::range_value_t<decltype(response.blocks)> block;
            func(ledger(), currentBlockNumber, block);
            response.blocks.emplace_back(std::move(block));
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
};
}  // namespace bcos::sync