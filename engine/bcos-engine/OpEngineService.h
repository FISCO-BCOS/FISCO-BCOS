/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "EngineServiceCommon.h"
#include "EngineTracker.h"

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
#include <mutex>
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

namespace engine_common::op
{
std::vector<std::string> supportedOpCapabilities();
std::optional<std::uint64_t> narrowU256ToU64(const u256& value);
bcos::h2048 toEthLogsBloom(const Bloom& logsBloom);
std::optional<std::string> validateOpPayloadAttributes(
    const PayloadAttributes& payloadAttributes, bool jovianActive);
/// op-geth miner.BuildPayload uses attrs.Transactions as-is and never synthesizes
/// an L1-attributes deposit. Synthesis is test-only (`allowSynthesizedL1Attributes`);
/// production op_engine_rpc must receive the real deposit from op-node.
inline std::optional<std::string> requireL1AttributesDeposit(
    const PayloadAttributes& payloadAttributes, bool allowSynthesized)
{
    bool const missing =
        !payloadAttributes.transactions.has_value() || payloadAttributes.transactions->empty();
    if (missing && !allowSynthesized)
    {
        return std::string(
            "payloadAttributes.transactions must include the L1 attributes deposit "
            "(op-geth does not synthesize one)");
    }
    return std::nullopt;
}
std::optional<std::string> validateOpNewPayloadRequest(
    const NewPayloadRequest& request, bool jovianActive);
void applyOpHeaderConstants(bcos::protocol::BlockHeader& header);
bcos::protocol::BlockHeader::Ptr rebuildOpEthHeader(
    const bcos::protocol::BlockHeaderFactory::Ptr& factory, const ExecutionPayload& payload,
    const h256& transactionsRoot, const h256& parentBeaconBlockRoot);
std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash);
}  // namespace engine_common::op

namespace op_detail
{
template <class ArtifactsMap>
bcos::protocol::BlockHeader::Ptr findBuiltHeader(
    EngineTracker::SharedAccess& shared, ArtifactsMap const& artifacts, h256 const& blockHash)
{
    if (auto payloadId = shared.payloadIdForHash(blockHash))
    {
        if (auto artifactIt = artifacts.find(*payloadId); artifactIt != artifacts.end())
        {
            return artifactIt->second.canonicalHeader;
        }
    }
    return nullptr;
}
}  // namespace op_detail

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
class OpEngineService
{
public:
    using ViewType = typename GlobalStateStorageType::ViewType;

    OpEngineService(MemPoolType& memPool, GlobalStateStorageType& globalStateStorage,
        SchedulerType& scheduler, bcos::protocol::BlockFactory::Ptr blockFactory,
        bcos::ledger::LedgerInterface::Ptr ledger = nullptr,
        int64_t blockTxCountLimit = c_defaultBlockTxCountLimit,
        std::uint32_t maxEngineVersion = static_cast<std::uint32_t>(ApiVersion::V3),
        bcos::scheduler::SchedulerInterface::Ptr delegate = nullptr,
        std::shared_ptr<DACaps> daCaps = nullptr, bool allowSynthesizedL1Attributes = false)
      : m_memPool(memPool),
        m_globalStateStorage(globalStateStorage),
        m_scheduler(scheduler),
        m_blockFactory(std::move(blockFactory)),
        m_ledger(std::move(ledger)),
        m_blockTxCountLimit(blockTxCountLimit),
        m_maxEngineVersion(maxEngineVersion),
        m_delegate(std::move(delegate)),
        m_daCaps(std::move(daCaps)),
        m_allowSynthesizedL1Attributes(allowSynthesizedL1Attributes)
    {
        if (!m_blockFactory)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"blockFactory must not be null"});
        }
    }
    ~OpEngineService() = default;
    OpEngineService(const OpEngineService&) = delete;
    OpEngineService(OpEngineService&&) = delete;
    OpEngineService& operator=(const OpEngineService&) = delete;
    OpEngineService& operator=(OpEngineService&&) = delete;

    task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        (void)remoteCapabilities;
        co_return engine_common::op::supportedOpCapabilities();
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

    /// Header returned by the last successful newPayload execute/commit (not the
    /// request-rebuilt announcement). Null if this call did not run or persist execution.
    bcos::protocol::BlockHeader::Ptr lastExecutedHeader() const
    {
        std::lock_guard lock(m_lastExecutedHeaderMutex);
        return m_lastExecutedHeader;
    }

private:
    static PayloadStatus makeStatus(PayloadValidationStatus status,
        std::optional<h256> latestValidHash = std::nullopt,
        std::optional<std::string> validationError = std::nullopt)
    {
        return engine_common::makeStatus(status, latestValidHash, validationError);
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

    task::Task<ForkchoiceUpdatedResult> buildOpPayload(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, std::uint32_t version,
        bcos::protocol::BlockNumber nextBlockNumber);

    task::Task<PayloadStatus> handleOpNewPayload(
        const NewPayloadRequest& request, std::uint32_t version);

    task::Task<PayloadStatus> runOpNewPayloadSteps(const NewPayloadRequest& request);

    bcos::protocol::Block::Ptr buildOpBlock(
        const ExecutionPayload& payload, bcos::protocol::BlockHeader::Ptr header);

    void requireDelegate() const
    {
        if (!m_delegate)
        {
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    "OP engine requires an m_delegate (OpScheduler); the composition "
                    "root did not wire one"});
        }
    }

    EngineTracker m_tracker;
    std::unordered_map<PayloadID, OpPayloadArtifacts> m_artifacts;
    MemPoolType& m_memPool;
    GlobalStateStorageType& m_globalStateStorage;
    SchedulerType& m_scheduler;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    int64_t m_blockTxCountLimit;
    std::uint32_t m_maxEngineVersion;
    bcos::scheduler::SchedulerInterface::Ptr m_delegate;
    std::shared_ptr<DACaps> m_daCaps;
    bool m_allowSynthesizedL1Attributes;
    /// Guards m_lastExecutedHeader: newPayload requests can run concurrently on RPC
    /// threads (no serial executor), so the shared_ptr write/read must be synchronized.
    mutable std::mutex m_lastExecutedHeaderMutex;
    bcos::protocol::BlockHeader::Ptr m_lastExecutedHeader;
};

}  // namespace bcos::engine

#include "OpEngineService.inl"
