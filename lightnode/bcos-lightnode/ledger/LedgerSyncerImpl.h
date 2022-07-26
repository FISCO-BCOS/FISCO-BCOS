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
    void impl_sync(bool onlyHeader)
    {
        auto& toLedger = bcos::concepts::getRef(m_toLedger);
        auto& fromLedger = bcos::concepts::getRef(m_fromLedger);

        auto toStatus = toLedger.getStatus();
        auto fromStatus = fromLedger.getStatus();

        if (onlyHeader)
        {
            for (auto blockNumber = toStatus.blockNumber; blockNumber < fromStatus.blockNumber;
                 ++blockNumber)
            {
                BlockType block;
                fromLedger.template getBlock<bcos::concepts::ledger::HEADER>(blockNumber, block);
                toLedger.template setBlock<bcos::concepts::ledger::HEADER>(std::move(block));
            }
        }
        else
        {
            for (auto blockNumber = toStatus.blockNumber; blockNumber < fromStatus.blockNumber;
                 ++blockNumber)
            {
                BlockType block;
                fromLedger.template getBlock<bcos::concepts::ledger::ALL>(blockNumber, block);
                toLedger.template setBlock<bcos::concepts::ledger::ALL>(std::move(block));
            }
        }
    }

private:
    FromLedgerType m_fromLedger;
    ToLedgerType m_toLedger;
};
}  // namespace bcos::sync