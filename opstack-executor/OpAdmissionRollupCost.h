// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

/// @file OpAdmissionRollupCost.h
/// @brief The OP lane's rollup cost for pool admission: the callable TxValidator's
///        Check::L1Cost row asks (TxValidator.h RollupCostFn), priced exactly as
///        OpPolicy::additionalMaxCost prices execution -- opTotalRollupCost over the same
///        L1Block attributes and the same fork spec.

#pragma once

#include <bcos-framework/ledger/AccountTableName.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/ledger/OpForkSchedule.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-task/Task.h>
#include <bcos-transaction-scheduler/HistoricalCallStorage.h>
#include <bcos-utilities/Common.h>
#include <fmt/format.h>
#include <opstack-executor/OpEthReceipt.h>
#include <opstack-executor/OpFeeParams.h>
#include <opstack-executor/OpForkSpec.h>
#include <opstack-executor/OpRollupCost.h>
#include <boost/throw_exception.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace bcos::executor_v1::opstack
{

/// The L1Block fee parameters as of @p head, read through the head header's state root
/// (HistoricalStateBackend, the view coCallAtBlock serves historical eth_call from), not from
/// the live committed plane: a commit merges that plane bucket by bucket with no point-in-time
/// snapshot, so six point reads on it can combine the old head's slots with the new head's. A
/// root-pinned read cannot tear: MPT nodes are content-addressed, a commit only adds nodes under
/// the new root, and the pruner never removes the nodes of a head at most one block behind the
/// ledger tip, which is as old as a snapshot gets (republish follows commit). It is consistent
/// with the head by construction: the header at head.blockNumber() is accepted only
/// if its canonicalBlockHash is head.hash(), the value the OP commit path stores under
/// SYS_NUMBER_2_HASH and the ledger config republish reads back. A header that is missing or
/// carries another hash (the snapshot outran the ledger, or was reorged out under it) is an
/// infrastructure fault: thrown, never priced from another head's state, so it cannot
/// masquerade as a rejected transaction.
template <class MultiLayerStorage>
task::Task<OpFeeParams> loadOpFeeParamsAtHead(MultiLayerStorage& storage,
    protocol::BlockFactory& blockFactory, bcos::ledger::LedgerConfig const& head)
{
    using ViewType = typename MultiLayerStorage::ViewType;
    using HistoricalBackend = bcos::scheduler_v1::HistoricalStateBackend<ViewType>;
    using HeadView =
        storage2::View<typename MultiLayerStorage::MutableStorage, void, HistoricalBackend>;

    auto latestView = storage.forkCommitted();
    // The header row itself (as getLedgerConfig reads it): a missing number is the same fault
    // as a foreign hash, not the NotFoundBlockHeader getBlockData would throw for it.
    protocol::BlockHeader::Ptr header;
    if (auto entry = co_await storage2::readOne(
            latestView, executor_v1::StateKeyView{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER,
                            std::to_string(head.blockNumber())}))
    {
        auto field = entry->get();
        header = blockFactory.blockHeaderFactory()->createBlockHeader(
            bytesConstRef(reinterpret_cast<byte const*>(field.data()), field.size()));
    }
    if (header == nullptr || protocol::canonicalBlockHash(*header) != head.hash())
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(
            fmt::format("admission: no committed header for head {} with hash {}",
                head.blockNumber(), head.hash().hex())));
    }
    // One backend per read: HistoricalStateBackend is single-coroutine by design.
    HistoricalBackend backend(latestView, header->stateRoot(),
        bcos::ledger::account::nodeAddressTableMode(), /*ethLaneNaming=*/true);
    HeadView headView(std::addressof(backend));
    co_return co_await loadOpFeeParamsAsync(headView);
}

/// Builds the admission callable over @p storageOwner->storage(), the node's state
/// MultiLayerStorage (held by shared_ptr: the callable outlives the initializer's members).
/// As op-geth's legacypool resetRollupCostFn: the L1Block slots are those of the snapshot's
/// head block (loadOpFeeParamsAtHead) and the fork is opForkSpecAt(head timestamp), cached per
/// head (number, hash) so a transaction costs no storage read while the head stands.
template <class StorageOwner>
std::function<task::Task<std::optional<u512>>(
    bytesConstRef signedEnvelope, uint64_t gasLimit, bcos::ledger::LedgerConfig const& head)>
makeOpAdmissionRollupCost(std::shared_ptr<StorageOwner> storageOwner,
    bcos::ledger::OpForkSchedule schedule, protocol::BlockFactory::Ptr blockFactory)
{
    struct HeadParams
    {
        std::mutex mutex;
        std::optional<std::pair<protocol::BlockNumber, crypto::HashType>> key;
        OpFeeParams fee{};
        OpForkSpec spec = OP_BEDROCK_SPEC;
    };
    auto cache = std::make_shared<HeadParams>();
    auto price = [storageOwner = std::move(storageOwner), schedule,
                     blockFactory = std::move(blockFactory),
                     cache](bytesConstRef signedEnvelope, uint64_t gasLimit,
                     bcos::ledger::LedgerConfig const& head) -> task::Task<std::optional<u512>> {
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
            fee = co_await loadOpFeeParamsAtHead(storageOwner->storage(), *blockFactory, head);
            spec = opForkSpecAt(schedule, opForkTimestampSec(head.timestamp()));
            std::lock_guard<std::mutex> lock(cache->mutex);
            cache->key = key;
            cache->fee = fee;
            cache->spec = spec;
        }
        // 512-bit, unsaturated: the row compares the true total (TxValidator.h RollupCostFn).
        co_return intxToBcosU512(opTotalRollupCost(
            fee, evmc::bytes_view{signedEnvelope.data(), signedEnvelope.size()}, gasLimit, spec));
    };
    return price;
}

}  // namespace bcos::executor_v1::opstack
