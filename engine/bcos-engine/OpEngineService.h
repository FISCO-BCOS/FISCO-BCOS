/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "EngineTracker.h"
#include "SplitEngineCommon.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/engine/DACaps.h>
#include <bcos-framework/engine/EngineService.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/OpBaseFee.h>
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
#include <bcos-rlp-protocol/EthBlockHeader.h>
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
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::engine
{

struct OpPayloadArtifacts
{
    bcos::protocol::BlockHeader::Ptr canonicalHeader;
};

namespace split_detail::op
{
std::vector<std::string> supportedOpCapabilities();
std::optional<std::uint64_t> narrowU256ToU64(const u256& value);
bcos::h2048 toEthLogsBloom(const Bloom& logsBloom);
std::optional<std::string> validateOpPayloadAttributes(
    const PayloadAttributes& payloadAttributes, bool jovianActive);
std::optional<std::string> validateOpNewPayloadRequest(
    const NewPayloadRequest& request, bool jovianActive);
void applyOpHeaderConstants(bcos::protocol::BlockHeader& header);
bcos::protocol::BlockHeader::Ptr rebuildOpEthHeader(
    const bcos::protocol::BlockHeaderFactory::Ptr& factory, const ExecutionPayload& payload,
    const h256& transactionsRoot, const h256& parentBeaconBlockRoot);
std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash);
}  // namespace split_detail::op

namespace op_detail
{
template <class ArtifactsMap, class ArtifactNode>
PayloadCache::PutResult publishBuiltPayload(EngineTracker::ExclusiveAccess& guard,
    ArtifactsMap& artifacts, PayloadID const& payloadId, h256 const& blockHash,
    CommonPayloadEntryPtr entry, ArtifactNode&& artifactNode)
{
    PayloadCache cacheRollback = guard.snapshotPayloadCache();
    ArtifactsMap artifactsRollback = artifacts;
    try
    {
        auto putResult = guard.putPayload(payloadId, blockHash, std::move(entry));
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
}  // namespace op_detail

template <class MemPoolType, class GlobalStateStorageType, class ExecutorType, class SchedulerType>
    requires executor_v1::TransactionExecutor<ExecutorType,
                 typename GlobalStateStorageType::ViewType> &&
             scheduler_v1::TransactionScheduler<SchedulerType,
                 typename GlobalStateStorageType::ViewType, ExecutorType,
                 std::vector<protocol::Transaction::Ptr>>
class OpEngineService
{
public:
    using ViewType = typename GlobalStateStorageType::ViewType;

    OpEngineService(MemPoolType& memPool, GlobalStateStorageType& globalStateStorage,
        ExecutorType& executor, SchedulerType& scheduler,
        bcos::protocol::BlockFactory::Ptr blockFactory,
        bcos::ledger::LedgerInterface::Ptr ledger = nullptr,
        int64_t blockTxCountLimit = c_defaultBlockTxCountLimit,
        std::uint32_t maxEngineVersion = static_cast<std::uint32_t>(ApiVersion::V4),
        bcos::scheduler::SchedulerInterface::Ptr delegate = nullptr,
        std::shared_ptr<DACaps> daCaps = nullptr)
      : m_memPool(memPool),
        m_globalStateStorage(globalStateStorage),
        m_executor(executor),
        m_scheduler(scheduler),
        m_blockFactory(std::move(blockFactory)),
        m_ledger(std::move(ledger)),
        m_blockTxCountLimit(blockTxCountLimit),
        m_maxEngineVersion(maxEngineVersion),
        m_delegate(std::move(delegate)),
        m_daCaps(std::move(daCaps))
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
        co_return split_detail::op::supportedOpCapabilities();
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
    static PayloadStatus makeStatus(PayloadValidationStatus status,
        std::optional<h256> latestValidHash = std::nullopt,
        std::optional<std::string> validationError = std::nullopt)
    {
        return split_detail::makeStatus(status, latestValidHash, validationError);
    }

    static PayloadStatus mapDelegateError(
        bcos::Error const& error, std::optional<h256> latestValidHash)
    {
        if (static_cast<bcos::scheduler::SchedulerError>(error.errorCode()) ==
            bcos::scheduler::SchedulerError::OpConsensusRejected)
        {
            return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("OP block execution rejected the payload: ") + error.errorMessage());
        }
        BOOST_THROW_EXCEPTION(
            OpExecutionInternalError{} << bcos::errinfo_comment{
                std::string("OP block execution failed (SchedulerError ") +
                std::to_string(error.errorCode()) + "): " + error.errorMessage()});
    }

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

    task::Task<ForkchoiceUpdatedResult> buildOpPayload(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, std::uint32_t version,
        bcos::protocol::BlockNumber nextBlockNumber);

    task::Task<PayloadStatus> handleOpNewPayload(
        const NewPayloadRequest& request, std::uint32_t version);

    task::Task<PayloadStatus> runOpNewPayloadSteps(const NewPayloadRequest& request);

    bcos::protocol::Block::Ptr buildOpBlock(
        const ExecutionPayload& payload, bcos::protocol::BlockHeader::Ptr header);

    EngineTracker m_tracker;
    std::unordered_map<PayloadID, OpPayloadArtifacts> m_artifacts;
    MemPoolType& m_memPool;
    GlobalStateStorageType& m_globalStateStorage;
    ExecutorType& m_executor;
    SchedulerType& m_scheduler;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    int64_t m_blockTxCountLimit;
    std::uint32_t m_maxEngineVersion;
    bcos::scheduler::SchedulerInterface::Ptr m_delegate;
    std::shared_ptr<DACaps> m_daCaps;
};

}  // namespace bcos::engine

#include "OpEngineService.inl"
