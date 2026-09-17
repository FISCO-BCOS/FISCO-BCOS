/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file OpEngineService.h
 * @brief Side-by-side OP Engine API service (template on mempool / global state / scheduler)
 */

#pragma once

#include "EngineServiceCommon.h"
#include "EngineTracker.h"
#include "ImportedStore.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/engine/DACaps.h>
#include <bcos-framework/engine/EngineService.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-framework/engine/OpForkId.h>
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

#include <algorithm>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::engine
{

// OpPayloadUndecodable lives in bcos-framework/engine/Errors.h beside its sibling
// error_info tags; both engine lanes resolve it from there.

struct OpPayloadArtifacts
{
    bcos::protocol::BlockHeader::Ptr canonicalHeader;
};

namespace engine_common::op
{
std::vector<std::string> supportedOpCapabilities();
std::optional<std::uint64_t> tryNarrowU256ToU64(const u256& value);
bcos::h2048 toEthLogsBloom(const Bloom& logsBloom);
/// OP-only attrs rules, keyed on the fork the attributes timestamp selects: the fork
/// determines which 1559 fields the block may carry (see OpBaseFee's extraData
/// layouts). The Eth-generic shape rules by method version live in
/// engine_common::validatePayloadAttributes.
std::optional<std::string> validateOpPayloadAttributes(
    const PayloadAttributes& payloadAttributes, OpForkId forkId);
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
/// OP-only newPayload shape rules for the (method version, fork) pair the timestamp
/// selects. The Ethereum-side fields follow the method's window (V2 = pre-Ecotone,
/// V3 = Ecotone..Isthmus, V4 = Isthmus+); the OP extras follow the fork (withdrawals
/// absent before Canyon, blobGasUsed zero before Jovian, extraData layout).
std::optional<std::string> validateOpNewPayloadRequest(
    const NewPayloadRequest& request, OpForkId forkId, std::uint32_t version);
void applyOpHeaderConstants(bcos::protocol::BlockHeader& header);
/// Rebuild the Eth-shaped OP header from a payload. The fork decides which header
/// fields exist at all (pre-Canyon has no withdrawals hash, pre-Ecotone no blob pair
/// or beacon root, pre-Isthmus no requests hash), so @p forkId is passed in rather
/// than inferred from whichever fields the payload happens to carry.
bcos::protocol::BlockHeader::Ptr rebuildOpEthHeader(
    const bcos::protocol::BlockHeaderFactory::Ptr& factory, const ExecutionPayload& payload,
    const h256& transactionsRoot, std::optional<h256> const& parentBeaconBlockRoot,
    OpForkId forkId);
// opEnvelopeToTars moved to EngineServiceCommon.h (engine_common::op) — the merged
// signature carries the lane-policy `allowDeposit` flag (OP build: true, Eth build: false).
}  // namespace engine_common::op

namespace detail
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
}  // namespace detail

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
class OpEngineService
{
public:
    using ViewType = typename GlobalStateStorageType::ViewType;

    OpEngineService(MemPoolType& memPool, GlobalStateStorageType& globalStateStorage,
        SchedulerType& scheduler, bcos::protocol::BlockFactory::Ptr blockFactory,
        int64_t blockTxCountLimit = c_defaultBlockTxCountLimit,
        bcos::scheduler::SchedulerInterface::Ptr delegate = nullptr,
        std::shared_ptr<DACaps> daCaps = nullptr, bool allowSynthesizedL1Attributes = false,
        OpEip1559Params eip1559 = kLegacyOpEip1559Params)
      : m_memPool(memPool),
        m_globalStateStorage(globalStateStorage),
        m_scheduler(scheduler),
        m_blockFactory(std::move(blockFactory)),
        m_blockTxCountLimit(blockTxCountLimit),
        m_delegate(std::move(delegate)),
        m_daCaps(std::move(daCaps)),
        m_allowSynthesizedL1Attributes(allowSynthesizedL1Attributes),
        m_eip1559(eip1559)
    {
        if (!m_blockFactory)
        {
            BOOST_THROW_EXCEPTION(
                InvalidEngineConfig{} << bcos::errinfo_comment{"blockFactory must not be null"});
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

    /// Profile-gated: payload timestamp (internal ms → Unix seconds) selects
    /// Jovian V4 / Karst V5. Never keys on head. Does not use EngineTracker::getPayload
    /// (that applies the Eth static V1–V5 window first).
    /// execution-apis prague.md / osaka.md: V4 serves payloads inside the pre-Karst time
    /// frame and V5 serves Karst ones; asking for the wrong one is -38005 Unsupported fork.
    /// The fork comes from the built payload's OWN timestamp (op-node's GetPayloadVersion).
    task::Task<GetPayloadResult> getPayload(const PayloadID& payloadId, std::uint32_t version);


    task::Task<PayloadStatus> newPayload(const NewPayloadRequest& request, std::uint32_t version);

    std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const
    {
        return m_tracker.safeBlockNumber();
    }

    /// Diagnostic/test surface: the tracker's current head number (the FCU-followed
    /// tip; distinct from lastExecutedHeader and from ledger SYS_CURRENT_STATE).
    std::optional<bcos::protocol::BlockNumber> trackedHeadNumber() const
    {
        if (auto head = m_tracker.trackedHead(); head.has_value())
        {
            return head->blockNumber;
        }
        return std::nullopt;
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

    /// S5: true when @p blockHash landed in the ImportedStore (imported by newPayload,
    /// not yet canonical). Read-only diagnostic/test surface; never consults the
    /// canonical tables.
    bool hasImportedBlock(const h256& blockHash) const
    {
        return m_importedStore.hasBlock(blockHash);
    }

private:
    static PayloadStatus makeStatus(PayloadValidationStatus status,
        std::optional<h256> latestValidHash = std::nullopt,
        std::optional<std::string> validationError = std::nullopt)
    {
        return engine_common::makeStatus(status, latestValidHash, validationError);
    }

    /// The load-bearing error router between this service and its scheduler delegate: ONLY
    /// OpConsensusRejected may answer as a consensus INVALID (with latestValidHash);
    /// everything else is rethrown as OpExecutionInternalError so the RPC surfaces -32603
    /// — never a consensus INVALID for a valid payload. Both routes are pinned by
    /// OpEngineServiceParityTest::op_commit_error_routing_unknown_error_is_never_invalid.
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

    /// FCU method-version window for the OP lane: V1-V3 exactly (Isthmus/Jovian —
    /// upstream has no FCU V4 on this fork; the caps list advertises exactly this
    /// window and V4 answers -38005). newPayload is V2..V4: V2 from Bedrock, V3 at
    /// Ecotone, V4 at Isthmus+ (see isNewPayloadVersionSupported). Method windows
    /// need not intersect; stored shape is payloadShapeVersion (V3/V4 → PayloadV3).
    static bool isForkchoiceVersionSupported(std::uint32_t version)
    {
        return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
               version <= static_cast<std::uint32_t>(ApiVersion::V3);
    }

    /// OP newPayload window: V2 from Bedrock, V3 at Ecotone, V4 at Isthmus — and V4 STAYS
    /// the window top through Karst. That asymmetry is upstream's own, not an oversight:
    /// op-node's NewPayloadVersion(ts) (op-node/rollup/types.go) has a single Isthmus branch
    /// returning NewPayloadV4 and no Karst branch, while GetPayloadVersion(ts) does rise to
    /// GetPayloadV5 on Karst — which is why getPayload gates V4/V5 on the payload's fork and
    /// this window does not move. exchangeCapabilities therefore advertises
    /// engine_getPayloadV5 but no engine_newPayloadV5. Which one is live comes from the
    /// payload timestamp (engineApiFor); V1 stays out because op-node's first fork is
    /// Bedrock, whose newPayload is V2. Not the Eth V1..V4 window.

    static bool isNewPayloadVersionSupported(std::uint32_t version)
    {
        return version >= static_cast<std::uint32_t>(ApiVersion::V2) &&
               version <= static_cast<std::uint32_t>(ApiVersion::V4);
    }

    /// `timestampSeconds` is Unix seconds (callers convert internal ms first).
    EngineForkContext requireOpEngineForkAt(uint64_t timestampSeconds) const;

    /// The next block's baseFee clock, derived in exactly one place: both the FCU build
    /// and the newPayload comparison must price a block identically, so they must not
    /// each assemble the flags (the parent's fork decides the 1559 source, the new
    /// block's fork the Canyon denominator — op-geth CalcBaseFee).
    [[nodiscard]] OpBaseFeeClock baseFeeClockFor(
        bcos::protocol::BlockHeader const& parentHeader, OpForkId newForkId) const;

    task::Task<ForkchoiceUpdatedResult> buildOpPayload(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, std::uint32_t version,
        bcos::protocol::BlockNumber nextBlockNumber, std::vector<bcos::bytes> decodedForcedTxs);

    task::Task<PayloadStatus> handleOpNewPayload(
        const NewPayloadRequest& request, std::uint32_t version);

    /// @p ctx and @p version come from the caller's resolved fork context: the pair
    /// gate already matched them, so the steps reuse it instead of re-resolving.
    task::Task<PayloadStatus> runOpNewPayloadSteps(
        const NewPayloadRequest& request, const EngineForkContext& ctx, std::uint32_t version);

    /// S6 SetCanonical, forward case: merge the imported chain rooting at the
    /// canonical tip up to @p headHash — per block, the block's own delta carries
    /// its canonical keys (HASH_2_NUMBER / NUMBER_2_HASH / NUMBER_2_BLOCK_HEADER)
    /// and merges once (一块一配, design §4.2/§4.4.4); SYS_CURRENT_STATE lands with
    /// the head's merge. Any failure restores the backend to its pre-call rows
    /// (design §4.2 atomicity: 失败则全部回到调用前) before rethrowing — the FCU
    /// caller must not answer VALID on a half-written plane.
    task::Task<void> canonicalizeImportedHead(const h256& headHash);

    /// One backend row's original value for the canonicalize undo journal.
    struct CanonicalizeUndoRow
    {
        executor_v1::StateKey key;
        std::optional<bcos::storage::Entry> prior;  // nullopt == key was absent
        // Cache-layer prior (review F3). The production composition has a cache layer
        // that mergeToBackends writes alongside the backend and fork()/forkCommitted()
        // read FIRST, so restoring only the backend would leave a mid-chain failure's
        // partial canonical state visible in the cache. nullopt == key was absent there.
        std::optional<bcos::storage::Entry> cachePrior;
    };

    /// Record @p key's current backend AND cache value (first occurrence only — the
    /// earliest value is the one a rollback must restore) before canonicalize mutates it.
    template <class BackendType>
    task::Task<void> recordCanonicalizeUndo(BackendType& backend,
        std::vector<CanonicalizeUndoRow>& undo, std::unordered_set<executor_v1::StateKey>& seen,
        executor_v1::StateKeyView key);

    /// Restore every journaled row: write the prior value back (or remove the key when it
    /// did not exist before the call) into BOTH the backend and the cache layer.
    template <class BackendType>
    task::Task<void> rollbackCanonicalize(
        BackendType& backend, std::vector<CanonicalizeUndoRow> const& undo);

    /// Release the materialized flats of blocks at/below the finalized marker (review F4).
    void pruneFlatsAtOrBelowFinalized();

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
    int64_t m_blockTxCountLimit;
    /// Block-commit delegate. CONTRACT: executeBlock/commitBlock must invoke the
    /// completion callback synchronously, before the call returns — this service
    /// reads the captured error immediately after the call and answers VALID on a
    /// null error (answering before the durable write would mask a failed commit).
    /// BaselineScheduler satisfies this (task::wait runs the coroutine to
    /// completion, callback inside it); the legacy chain's SchedulerImpl does NOT
    /// (its commitBlock returns while blockExecutive->asyncCommit is still in
    /// flight) — do not wire it here.
    bcos::scheduler::SchedulerInterface::Ptr m_delegate;
    std::shared_ptr<DACaps> m_daCaps;
    bool m_allowSynthesizedL1Attributes;
    /// The chain's EIP-1559 triple (config.genesis [op_eip1559]; kLegacyOpEip1559Params when
    /// undeclared). Injected at boot and never mutated: it is a genesis-frozen chain property,
    /// so a value that changed mid-flight could not be reconciled with blocks already produced.
    OpEip1559Params m_eip1559{kLegacyOpEip1559Params};
    /// S5/S6 imported-tree lock (design §4.2): guards the ImportedStore decision
    /// sequence (occupancy check -> put) and the canonicalize gate — NOT the
    /// importExecute execution and NOT any storage co_await. A POSIX mutex must never
    /// be held across a suspension point: task::syncWait can complete the coroutine on
    /// another thread (libtask/bcos-task/Wait.h), so the unlock would cross threads;
    /// canonicalizeImportedHead takes this lock only for its sync entry and exit
    /// sections, and m_canonicalizeInFlight (guarded by it) carries the exclusion
    /// across the awaited body.
    mutable std::mutex m_importedTreeMutex;
    /// True while canonicalizeImportedHead is between its entry and exit critical
    /// sections. Guarded by m_importedTreeMutex. A concurrent newPayload that observes
    /// it fails closed with SYNCING rather than interleaving with the batch.
    bool m_canonicalizeInFlight = false;
    /// Guards m_lastExecutedHeader: newPayload requests can run concurrently on RPC
    /// threads (no serial executor), so the shared_ptr write/read must be synchronized.
    ///
    /// The m_delegate (an OpScheduler) sequences (reset → executeBlock, executeBlock →
    /// commitBlock) need no extra serialization of their own — the reasoning, precisely:
    /// OpScheduler::executeBlock try-locks m_executeMutex AND m_commitMutex ("Another
    /// block is executing/committing!" — a concurrent second caller fails closed), and
    /// executeBlock/commitBlock drive their task via task::syncWait, so a sequence's calls
    /// are ordered within the calling thread. OpScheduler::reset is NOT a no-op: it takes
    /// all three mutexes (scoped_lock, so it cannot interleave with an in-flight execute
    /// or commit), then drops any uncommitted pending block (popping its verified storage
    /// layer) and restores the continuity watermark to the committed tip. A reset landing
    /// between another caller's executeBlock and commitBlock therefore makes that caller's
    /// commitBlock fail CLOSED ("Unexpected empty results!" — the pending it needs was
    /// dropped/replaced), which the OP service reports as an error and the sequencer
    /// retries — convergent, never corrupt. Out-of-order parents cannot interleave: the
    /// sequencer advances height n+1 only after height n's canonical status, and the
    /// delegate's continuity check rejects anything else.
    mutable std::mutex m_lastExecutedHeaderMutex;
    bcos::protocol::BlockHeader::Ptr m_lastExecutedHeader;
    /// S5: payloads imported by newPayload (InsertBlockWithoutSetHead), keyed by the
    /// CL-announced hash. latest / SYS_CURRENT_STATE / tracker never read this.
    ImportedStore m_importedStore;
};

}  // namespace bcos::engine
