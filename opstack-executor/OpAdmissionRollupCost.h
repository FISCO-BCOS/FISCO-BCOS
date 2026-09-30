// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

/// @file OpAdmissionRollupCost.h
/// @brief The OP lane's rollup cost for pool admission: the callable TxValidator's
///        Check::L1Cost row asks (TxValidator.h RollupCostFn), priced exactly as
///        OpPolicy::additionalMaxCost prices execution -- opTotalRollupCost over the same
///        L1Block attributes and the same fork spec.

#pragma once

#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/OpForkSchedule.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <opstack-executor/OpEthReceipt.h>
#include <opstack-executor/OpFeeParams.h>
#include <opstack-executor/OpForkSpec.h>
#include <opstack-executor/OpRollupCost.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace bcos::executor_v1::opstack
{

/// Builds the admission callable over @p storageOwner->storage(), the node's state
/// MultiLayerStorage (held by shared_ptr: the callable outlives the initializer's members).
/// As op-geth's legacypool resetRollupCostFn: the L1Block slots are read from the COMMITTED
/// plane (never a sealed-but-uncommitted layer) and the fork is opForkSpecAt(head timestamp),
/// cached per snapshot (number, hash) so a transaction costs no storage read while the head
/// stands. Between a commit and the republish that follows it a miss caches the new head's
/// slots under the old key -- a price at most one block newer than the snapshot, never older.
template <class StorageOwner>
std::function<task::Task<std::optional<u256>>(
    bytesConstRef signedEnvelope, uint64_t gasLimit, bcos::ledger::LedgerConfig const& head)>
makeOpAdmissionRollupCost(
    std::shared_ptr<StorageOwner> storageOwner, bcos::ledger::OpForkSchedule schedule)
{
    struct HeadParams
    {
        std::mutex mutex;
        std::optional<std::pair<protocol::BlockNumber, crypto::HashType>> key;
        OpFeeParams fee{};
        OpForkSpec spec = OP_BEDROCK_SPEC;
    };
    auto cache = std::make_shared<HeadParams>();
    return [storageOwner = std::move(storageOwner), schedule, cache](bytesConstRef signedEnvelope,
               uint64_t gasLimit,
               bcos::ledger::LedgerConfig const& head) -> task::Task<std::optional<u256>> {
        auto const key = std::pair{head.blockNumber(), head.hash()};
        OpFeeParams fee;
        OpForkSpec spec = OP_BEDROCK_SPEC;
        bool hit = false;
        {
            std::lock_guard<std::mutex> lock(cache->mutex);
            if (cache->key == key)
            {
                fee = cache->fee;
                spec = cache->spec;
                hit = true;
            }
        }
        if (!hit)
        {
            auto view = storageOwner->storage().forkCommitted();
            fee = co_await loadOpFeeParamsAsync(view);
            spec = opForkSpecAt(schedule, opForkTimestampSec(head.timestamp()));
            std::lock_guard<std::mutex> lock(cache->mutex);
            cache->key = key;
            cache->fee = fee;
            cache->spec = spec;
        }
        auto const total = opTotalRollupCost(
            fee, evmc::bytes_view{signedEnvelope.data(), signedEnvelope.size()}, gasLimit, spec);
        // Past 2^256 exceeds any balance: saturate, the comparison still rejects.
        co_return intxToBcosU256(total > intx::uint512{~intx::uint256{0}} ?
                                     ~intx::uint256{0} :
                                     static_cast<intx::uint256>(total));
    };
}

}  // namespace bcos::executor_v1::opstack
