#pragma once

#include <bcos-concepts/Block.h>
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/sync/BlockSyncerServer.h>
#include <bcos-concepts/sync/SyncBlockMessages.h>
#include <boost/throw_exception.hpp>
#include <stdexcept>

namespace bcos::sync
{

template <bcos::concepts::ledger::Ledger LedgerType, bcos::concepts::sync::RequestBlock RequestType,
    bcos::concepts::sync::ResponseBlock ResponseType>
class BlockSyncerServerImpl : public bcos::concepts::sync::SyncBlockServerBase<
                                  BlockSyncerServerImpl<LedgerType, RequestType, ResponseType>>
{
public:
    BlockSyncerServerImpl(LedgerType ledger) : m_ledger(std::move(ledger)) {}

    ResponseType impl_getBlock(RequestType const& request)
    {
        if (request.endBlockNumber <= request.beginBlockNumber)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Wrong block number range"});
        }

        auto transactionCount = ledger().getTotalTransactionCount();
        auto blockNumber = transactionCount.blockNumber;

        ResponseType response;
        response.blockNumber = blockNumber;
        if (request.beginBlockNumber > blockNumber)
        {
            return response;
        }
        auto count =
            std::min(blockNumber + 1, static_cast<decltype(blockNumber)>(request.endBlockNumber)) -
            request.beginBlockNumber;
        response.blocks.reserve(count);

        for (auto currentBlockNumber = request.beginBlockNumber;
             currentBlockNumber < request.beginBlockNumber + count; ++currentBlockNumber)
        {
            if (request.onlyHeader)
            {
                response.blocks.emplace_back(
                    ledger().template getBlock<concepts::ledger::HEADER>(currentBlockNumber));
            }
            else
            {
                response.blocks.emplace_back(
                    ledger().template getBlock<concepts::ledger::ALL>(currentBlockNumber));
            }
        }

        return response;
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