/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "EngineTracker.h"
#include "SplitEngineCommon.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-framework/engine/EngineService.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-framework/transaction-scheduler/TransactionScheduler.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::engine
{

namespace generic_detail
{
bcos::h256 syntheticHash(std::string_view seed);
std::optional<std::string> validateExecutionPayload(
    const ExecutionPayload& executionPayload, std::uint32_t version);

template <class ArtifactsMap, class ArtifactNode>
PayloadCache::PutResult publishBuiltPayload(EngineTracker::ExclusiveAccess& guard,
    ArtifactsMap& artifacts, PayloadID const& payloadId, h256 const& blockHash,
    CommonPayloadEntryPtr entry, ArtifactNode&& artifactNode)
{
    PayloadCache cacheRollback = guard.snapshotPayloadCache();
    ArtifactsMap artifactsRollback = artifacts;
    try
    {
        auto putResult = guard.putUnboundedPayload(payloadId, blockHash, std::move(entry));
        artifacts[payloadId] = std::forward<ArtifactNode>(artifactNode);
        for (auto const& evictedId : putResult.evicted)
        {
            artifacts.erase(evictedId);
        }
        return putResult;
    }
    catch (...)
    {
        guard.restorePayloadCache(std::move(cacheRollback));
        artifacts = std::move(artifactsRollback);
        throw;
    }
}
}  // namespace generic_detail

template <class ViewType>
struct GenericPayloadArtifacts
{
    std::shared_ptr<ViewType> view;
    bcos::protocol::BlockHeader::Ptr header;
    std::vector<protocol::TransactionReceipt::Ptr> receipts;
};

template <class MemPoolType, class GlobalStateStorageType, class ExecutorType, class SchedulerType>
    requires executor_v1::TransactionExecutor<ExecutorType,
                 typename GlobalStateStorageType::ViewType> &&
             scheduler_v1::TransactionScheduler<SchedulerType,
                 typename GlobalStateStorageType::ViewType, ExecutorType,
                 std::vector<protocol::Transaction::Ptr>>
class GenericEngineService
{
public:
    using ViewType = typename GlobalStateStorageType::ViewType;

    GenericEngineService(MemPoolType& memPool, GlobalStateStorageType& globalStateStorage,
        ExecutorType& executor, SchedulerType& scheduler,
        bcos::protocol::BlockFactory::Ptr blockFactory,
        bcos::ledger::LedgerInterface::Ptr ledger = nullptr,
        int64_t blockTxCountLimit = c_defaultBlockTxCountLimit,
        std::uint32_t maxEngineVersion = static_cast<std::uint32_t>(ApiVersion::V3))
      : m_memPool(memPool),
        m_globalStateStorage(globalStateStorage),
        m_executor(executor),
        m_scheduler(scheduler),
        m_blockFactory(std::move(blockFactory)),
        m_ledger(std::move(ledger)),
        m_blockTxCountLimit(blockTxCountLimit),
        m_maxEngineVersion(maxEngineVersion)
    {
        if (!m_blockFactory)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"blockFactory must not be null"});
        }
    }

    task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        (void)remoteCapabilities;
        co_return split_detail::supportedCapabilities();
    }

    task::Task<ForkchoiceUpdatedResult> updateForkchoice(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes* payloadAttributes, std::uint32_t version);

    task::Task<GetPayloadResult> getPayload(const PayloadID& payloadId, std::uint32_t version)
    {
        co_return m_tracker.getPayload(payloadId, version);
    }

    task::Task<PayloadStatus> newPayload(const NewPayloadRequest& request, std::uint32_t version);

    std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const
    {
        return m_tracker.safeBlockNumber();
    }

    std::optional<bcos::protocol::BlockNumber> getFinalizedBlockNumber() const
    {
        return m_tracker.finalizedBlockNumber();
    }

private:
    struct BuildPayloadResult
    {
        ExecutionPayload executionPayload;
        bcos::protocol::BlockHeader::Ptr header;
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
    };

    bool isForkchoiceVersionSupported(std::uint32_t version) const
    {
        return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
               version <= m_maxEngineVersion;
    }

    static bool isNewPayloadVersionSupported(std::uint32_t version)
    {
        return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
               version <= static_cast<std::uint32_t>(ApiVersion::V4);
    }

    task::Task<BuildPayloadResult> buildPayload(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, const PayloadID& payloadId,
        std::uint32_t version, bcos::protocol::BlockNumber nextBlockNumber,
        std::vector<protocol::Transaction::Ptr> sealedTxs, ViewType& view) const;

    task::Task<h256> calculateStateRoot(ViewType& view, uint32_t blockVersion) const;

    EngineTracker m_tracker;
    std::unordered_map<PayloadID, GenericPayloadArtifacts<ViewType>> m_artifacts;
    MemPoolType& m_memPool;
    GlobalStateStorageType& m_globalStateStorage;
    ExecutorType& m_executor;
    SchedulerType& m_scheduler;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    int64_t m_blockTxCountLimit;
    std::uint32_t m_maxEngineVersion;
};

}  // namespace bcos::engine

#include "GenericEngineService.inl"
