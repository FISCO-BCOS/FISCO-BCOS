/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file EngineServiceImpl.h
 * @brief Minimal Engine API service implementation
 */

#pragma once

#include "bcos-concepts/ByteBuffer.h"
#include "bcos-crypto/merkle/Merkle.h"
#include "bcos-framework/engine/EngineService.h"
#include "bcos-framework/engine/Types.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Bloom.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/FixedBytes.h"
// bcos-codec is already a (transitive, PUBLIC) dependency of the `engine` target via `ledger`
// (bcos-ledger/CMakeLists.txt:27 links `codec` PUBLIC), so this adds no new library edge -- and
// notably it is NOT a bcos-evm dependency: `EthBlockHeader` deliberately lives in bcos-codec
// (Task 3) precisely so that both the engine and the OP execution side can reach it.
#include <bcos-codec/rlp/EthBlockHeader.h>
#include <boost/lexical_cast.hpp>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <range/v3/algorithm/any_of.hpp>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::engine
{
DERIVE_BCOS_EXCEPTION(UnsupportedEngineApiVersion);
DERIVE_BCOS_EXCEPTION(GlobalStateStorageNotConfigured);
DERIVE_BCOS_EXCEPTION(UnknownForkchoiceHeadBlock);
DERIVE_BCOS_EXCEPTION(InvalidForkchoiceState);
DERIVE_BCOS_EXCEPTION(UnknownPayload);
DERIVE_BCOS_EXCEPTION(IncompatiblePayloadVersion);

// ---- OP-mode exceptions (task-5b, design §6.1/§6.2/§6.3) ----
// These carry the JSON-RPC error codes the Engine API assigns to these conditions. The mapping
// exception-type -> code is not implemented anywhere yet on purpose: `engine_newPayloadV4` /
// `engine_getPayloadV4` RPC endpoint registration is out of scope for this cycle (spec §6.3/§6.4
// 欠账台账, 裁定 A6), so the OP branch is exercised by direct `EngineServiceImpl` calls and the
// code is documentation-of-intent for whoever wires `bcos-rpc`'s EngineEndpoint later. The
// existing generic exceptions above are in exactly the same position (no code mapping in-repo).

/// JSON-RPC -38005 "Unsupported fork": the payload timestamp's fork and the called method version
/// disagree (design §6.1 step 1) -- Isthmus+ payloads may only arrive on V4, pre-Isthmus
/// timestamps may not use V4.
DERIVE_BCOS_EXCEPTION(UnsupportedFork);

/// JSON-RPC -38003 "Invalid payload attributes": an OP-mode forkchoiceUpdated carried payload
/// attributes. Attribute-driven block building is not supported this cycle; per design §6.2 the
/// forkchoice state update itself still takes effect (head advances), only the build is refused.
DERIVE_BCOS_EXCEPTION(UnsupportedOpPayloadAttributes);

/// `getPayload` in OP mode: block *building* is not OP-ized this cycle (design §6.3), so there is
/// no OP payload to return and the failure is reported explicitly rather than as `UnknownPayload`
/// (which would falsely suggest an expired/unknown id).
DERIVE_BCOS_EXCEPTION(OpPayloadBuildingUnsupported);

/// JSON-RPC -32603 "Internal error": an OP block execution failure the error-classification table
/// (design §4.3/§6.1 step 4) attributes to the storage layer rather than to the block. Must never
/// be reported as INVALID -- a storage fault is not a consensus verdict on the payload.
DERIVE_BCOS_EXCEPTION(OpExecutionInternalError);

namespace detail
{
std::string encodePayloadSequence(std::uint64_t value);

bcos::h256 syntheticHash(std::string_view seed);

std::vector<std::string> supportedCapabilities();

/// OP-mode capability list (task-5a, spec §6.3): `supportedCapabilities()` plus the V4
/// entries. Selected via `if constexpr` on `EngineServiceImpl::c_opMode` in
/// `exchangeCapabilities` below -- never reached by the generic composition root, so the
/// generic path's capability list stays byte-for-byte the pre-existing 10 entries.
std::vector<std::string> supportedOpCapabilities();

bool isGetPayloadVersionCompatible(EngineApiVersion requestVersion, std::uint32_t payloadVersion);

std::optional<std::string> validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version);

std::optional<std::string> validateExecutionPayload(
    const ExecutionPayload& executionPayload, std::uint32_t version);

// ---- OP-mode helpers (task-5b, design §6.1 step 2) ----

/// Bounds-checked u256 -> uint64 narrowing (nullopt when out of range). Explicit rather than a
/// bare `convert_to`/`static_cast`: this repo has a documented silent-truncation incident with
/// unchecked wide-integer narrowing (MEMORY costofprecompiled-int64-overflow), and
/// `OpSchedulerImpl.h`'s `narrowU256ToU64` applies the same discipline on the execution side.
std::optional<std::uint64_t> narrowU256ToU64(const u256& value);

/// `Bloom` (std::array<byte,256>, the ExecutionPayload representation) -> `h2048` (the
/// EthBlockHeader representation).
bcos::h2048 toEthLogsBloom(const Bloom& logsBloom);

/// OP static validation (design §6.1 step 2, everything except the blockHash comparison, which
/// needs the caller-derived transactionsRoot). Returns a field-naming validationError string on
/// rejection. `jovianActive` selects the fork-dependent `blobGasUsed` rule (§5.1).
std::optional<std::string> validateOpNewPayloadRequest(
    const NewPayloadRequest& request, bool jovianActive);

/// Reconstructs the 21-field ETH/OP header from the payload (design §6.1 step 2 / §5.1); its
/// `hash()` is what the payload's `blockHash` is checked against, and its `encode()` is what the
/// block registration stores. Precondition: `validateOpNewPayloadRequest` accepted the request.
bcos::codec::rlp::EthBlockHeader rebuildOpEthHeader(const ExecutionPayload& payload,
    const h256& transactionsRoot, const h256& parentBeaconBlockRoot);
}  // namespace detail

template <class MemPoolType, class GlobalStateStorageType, class ExecutorType, class SchedulerType>
    requires executor_v1::TransactionExecutor<ExecutorType,
                 typename GlobalStateStorageType::ViewType> &&
             scheduler_v1::TransactionScheduler<SchedulerType,
                 typename GlobalStateStorageType::ViewType, ExecutorType,
                 std::vector<protocol::Transaction::Ptr>>
class EngineServiceImpl
{
public:
    using ViewType = typename GlobalStateStorageType::ViewType;

    /// opMode compile-time judgment (spec §6.3, 裁定 B1; task-5a review fix I2): probes for
    /// `SchedulerType::executeOpBlock` via an unevaluated `requires`-expression -- no runtime
    /// bool, matching this class's existing all-template style. Only `OpSchedulerImpl` (task 4)
    /// defines a member function with this name; every SchedulerType used by the generic
    /// composition root today (StubScheduler/BloomScheduler in EngineServiceTest.cpp,
    /// SchedulerSerialImpl in production) does not, so `c_opMode` is false for all of them and
    /// the generic path is byte-for-byte unaffected (task-5a zero-drift constraint).
    ///
    /// Takes the *address* of the member function template rather than calling it, pinning an
    /// explicit template argument only for the sole `auto`-deduced parameter (`executeOpBlock`'s
    /// third, `::ranges::input_range auto const& rawTxBytes`); the second parameter
    /// (`bcos::evm::engine::OpBlockEnv const&` on the real OP scheduler) is a fixed,
    /// non-dependent part of the declaration itself and needs no argument *value* to resolve --
    /// unlike a call expression, `&SchedulerType::template executeOpBlock<...>` never needs
    /// engine/bcos-engine to name `OpBlockEnv`, so this stays free of any bcos-evm dependency
    /// (task-5a "库纯净" constraint) without an extra sink type. `std::vector<bcos::bytes>` is
    /// the type `OpSchedulerImpl::executeOpBlock`'s `rawTxBytes` parameter is actually
    /// instantiated with elsewhere (matches `::ranges::input_range`). Address-of is an
    /// unevaluated operand inside a requires-expression ([expr.prim.req.simple]), so this does
    /// not odr-use (and therefore does not force instantiation of) the function body -- only its
    /// declaration needs to typecheck, same non-triggering-instantiation guarantee `sizeof`/
    /// `decltype`/`std::declval` rely on elsewhere in this codebase.
    ///
    /// This does not separately verify that `executeOpBlock`'s first parameter (`Storage&`) is
    /// exactly this class's `ViewType` -- redundant by construction: the enclosing class
    /// template's own `requires` clause already requires
    /// `scheduler_v1::TransactionScheduler<SchedulerType, ViewType, ExecutorType, ...>`, which
    /// for `OpSchedulerImpl<Storage>` can only be satisfied (via its dummy `executeBlock`'s
    /// `Storage&` parameter) when `Storage == ViewType` -- so any `SchedulerType` reaching this
    /// point already has that identity pinned by the class's own instantiation constraint.
    static constexpr bool c_opMode =
        requires { &SchedulerType::template executeOpBlock<std::vector<bcos::bytes>>; };

    EngineServiceImpl(MemPoolType& memPool, GlobalStateStorageType& globalStateStorage,
        ExecutorType& executor, SchedulerType& scheduler,
        bcos::protocol::BlockFactory::Ptr blockFactory,
        int64_t blockTxCountLimit = bcos::engine::c_defaultBlockTxCountLimit,
        std::uint32_t maxEngineVersion = static_cast<std::uint32_t>(EngineApiVersion::V3))
      : m_memPool(std::ref(memPool)),
        m_globalStateStorage(std::ref(globalStateStorage)),
        m_blockTxCountLimit(blockTxCountLimit),
        m_executor(std::ref(executor)),
        m_scheduler(std::ref(scheduler)),
        m_blockFactory(std::move(blockFactory)),
        m_maxEngineVersion(maxEngineVersion)
    {
        if (!m_blockFactory)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"blockFactory must not be null"});
        }
    }
    ~EngineServiceImpl() = default;
    EngineServiceImpl(const EngineServiceImpl&) = delete;
    EngineServiceImpl(EngineServiceImpl&&) = delete;
    EngineServiceImpl& operator=(const EngineServiceImpl&) = delete;
    EngineServiceImpl& operator=(EngineServiceImpl&&) = delete;

    bcos::task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        (void)remoteCapabilities;
        // opMode compile-time branch (task-5a, spec §6.3): selects the capability list at
        // compile time via `if constexpr` on `c_opMode` -- not a runtime bool -- so the generic
        // composition root's codegen for this function is exactly what it was before this task
        // (the `else` branch, unconditionally).
        if constexpr (c_opMode)
        {
            co_return detail::supportedOpCapabilities();
        }
        else
        {
            co_return detail::supportedCapabilities();
        }
    }

    bcos::task::Task<ForkchoiceUpdatedResult> updateForkchoice(
        const ForkchoiceState& forkchoiceState, const PayloadAttributes* payloadAttributes,
        std::uint32_t version)
    {
        if (!isVersionSupported(version))
        {
            BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                                  << bcos::errinfo_comment{"Unsupported Engine API version"});
        }
        if (payloadAttributes != nullptr)
        {
            // OP mode skips this pre-check entirely (design §6.2): the OP answer to *any*
            // attributes object -- well-formed or not -- is -38003, and it must be given only
            // AFTER the forkchoice state has been updated ("forkchoiceState 更新不回滚,head 照常
            // 推进,仅不开启 build"). Returning an Invalid payloadStatus here would abort the
            // update instead. The refusal is raised further down, at the point where the generic
            // path would start building. `if constexpr` keeps the generic path's codegen
            // byte-for-byte unchanged.
            if constexpr (!c_opMode)
            {
                if (auto validationError =
                        detail::validatePayloadAttributes(*payloadAttributes, version);
                    validationError.has_value())
                {
                    ForkchoiceUpdatedResult result{
                        .payloadStatus = makeStatus(
                            PayloadValidationStatus::Invalid, std::nullopt, validationError),
                        .payloadId = std::nullopt,
                    };
                    co_return result;
                }
            }
        }

        auto view = m_globalStateStorage.get().fork();
        auto headBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, forkchoiceState.headBlockHash, bcos::ledger::fromStorage);
        auto safeBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, forkchoiceState.safeBlockHash, bcos::ledger::fromStorage);
        auto finalizedBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, forkchoiceState.finalizedBlockHash, bcos::ledger::fromStorage);

        if (!headBlockNumber.has_value() || !safeBlockNumber.has_value() ||
            !finalizedBlockNumber.has_value())
        {
            ForkchoiceUpdatedResult result{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt),
                .payloadId = std::nullopt,
            };
            co_return result;
        }
        if (*safeBlockNumber > *headBlockNumber)
        {
            BOOST_THROW_EXCEPTION(
                InvalidForkchoiceState{} << bcos::errinfo_comment{
                    "Forkchoice safe block number must not exceed head block number"});
        }
        if (*finalizedBlockNumber > *headBlockNumber)
        {
            BOOST_THROW_EXCEPTION(
                InvalidForkchoiceState{} << bcos::errinfo_comment{
                    "Forkchoice finalized block number must not exceed head block number"});
        }
        if (*finalizedBlockNumber > *safeBlockNumber)
        {
            BOOST_THROW_EXCEPTION(
                InvalidForkchoiceState{} << bcos::errinfo_comment{
                    "Forkchoice finalized block number must not exceed safe block number"});
        }

        // Phase 1: Validate and update tracked block state under lock.
        // The lock is released before any co_await to avoid holding a mutex
        // across a coroutine suspension point (which is UB under POSIX).
        //
        // Scope note (task-5b review M5): this rule is this function's, not the class's. The OP
        // `newPayload` branch (`handleOpNewPayload` below) deliberately DOES hold `x_state` across
        // `co_await`s — that is required by the op-validator-loop design §4.4 coroutine contract
        // (safety premise 2, "engine 执行段被 x_state 锁串行"), which grants the permission
        // conditionally: every storage2 backend in play completes synchronously in-thread, so no
        // coroutine ever resumes on another thread. §4.4's invalidation criterion explicitly
        // covers both that usage and the pre-existing lock-across-`co_await` TODO in
        // `handleNewPayload`'s generic path. The two are consistent, not contradictory: this
        // comment states the default, §4.4 states the audited exception.
        {
            std::unique_lock lock(x_state);
            if (m_trackedHeadBlock.has_value())
            {
                auto const& trackedHeadBlock = *m_trackedHeadBlock;
                if (*headBlockNumber < trackedHeadBlock.blockNumber)
                {
                    ForkchoiceUpdatedResult result{
                        .payloadStatus = makeStatus(PayloadValidationStatus::Valid,
                            forkchoiceState.headBlockHash, std::nullopt),
                        .payloadId = std::nullopt,
                    };
                    co_return result;
                }
                if (*headBlockNumber == trackedHeadBlock.blockNumber)
                {
                    if (forkchoiceState.headBlockHash != trackedHeadBlock.hash)
                    {
                        BOOST_THROW_EXCEPTION(
                            InvalidForkchoiceState{} << bcos::errinfo_comment{
                                "Forkchoice head block hash conflicts with tracked block number"});
                    }
                }
                else if (*headBlockNumber != trackedHeadBlock.blockNumber + 1)
                {
                    BOOST_THROW_EXCEPTION(
                        InvalidForkchoiceState{} << bcos::errinfo_comment{
                            "Forkchoice head block number must increase by exactly 1"});
                }
            }

            m_forkchoiceState = forkchoiceState;
            m_trackedHeadBlock = TrackedHeadBlock{
                .hash = forkchoiceState.headBlockHash,
                .blockNumber = *headBlockNumber,
            };
            updateTrackedBlockNumbers(safeBlockNumber, finalizedBlockNumber);
        }  // Lock released here — safe to co_await below.

        ForkchoiceUpdatedResult result{
            .payloadStatus = makeStatus(
                PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
            .payloadId = std::nullopt,
        };
        if (payloadAttributes == nullptr)
        {
            co_return result;
        }
        if constexpr (c_opMode)
        {
            // Design §6.2: an OP validator's L1-derivation path *does* send forkchoiceUpdated
            // with OP-extended attributes (noTxPool=true + derived transactions) whenever
            // consolidation fails or there is no unsafe block -- "验证者不发 attributes" is not
            // true. Attribute-driven building is not supported this cycle (it is the same road as
            // OP-izing getPayload, both on the §6.4 op-node 实连前置欠账台账), so the answer is
            // -38003.
            //
            // Placement is the whole point: everything above -- the storage lookups, the
            // monotonicity checks, and the tracked-head/safe/finalized update under `x_state` --
            // has already run and is *kept*. Per spec the forkchoice update is not rolled back;
            // only the build is refused.
            BOOST_THROW_EXCEPTION(
                UnsupportedOpPayloadAttributes{} << bcos::errinfo_comment{
                    "Payload attributes are not supported in OP mode (block building is not "
                    "OP-ized; JSON-RPC -38003)"});
        }
        else
        {
            // Mempool operations run without lock — they only depend on the view, not on x_state.
            // Step 1: Remove stale/tainted transactions from mempool (cleans tx with nonce < state
            // nonce)
            m_memPool.get().remove(view);

            // Step 2: Create mutable storage layer on the view for transaction sealing
            view.newMutable();

            // Step 3: Pull valid transactions from mempool (in nonce order, with nonce
            // verification)
            std::vector<protocol::Transaction::Ptr> sealedTxs;
            m_memPool.get().seal(m_blockTxCountLimit, view, std::back_inserter(sealedTxs));

            auto payloadId = nextPayloadID();
            auto nextBlockNumber = *headBlockNumber + 1;
            auto payload = co_await buildPayload(forkchoiceState, *payloadAttributes, payloadId,
                version, nextBlockNumber, std::move(sealedTxs), view);
            PayloadEntry entry{
                .version = version,
                .executionPayload = std::move(payload),
                .blockValue = 0,
                .blobsBundle = std::nullopt,
                .shouldOverrideBuilder = false,
                .view = std::make_shared<ViewType>(std::move(view)),
            };
            if (version == static_cast<std::uint32_t>(EngineApiVersion::V3))
            {
                entry.blobsBundle = BlobsBundleV1{};
            }

            // Re-acquire lock to publish the built payload to the cache.
            {
                std::unique_lock lock(x_state);
                m_blockHashToPayloadId[entry.executionPayload.blockHash] = payloadId;
                m_payloadCache[payloadId] = entry;
            }
            result.payloadId = payloadId;
            co_return result;
        }
    }

    bcos::task::Task<GetPayloadResult> getPayload(const PayloadID& payloadId, std::uint32_t version)
    {
        co_return handleGetPayload(payloadId, version);
    }

    bcos::task::Task<PayloadStatus> newPayload(
        const NewPayloadRequest& request, std::uint32_t version)
    {
        co_return co_await handleNewPayload(request, version);
    }

    std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const
    {
        std::shared_lock lock(x_state);
        return m_safeBlockNumber;
    }

    std::optional<bcos::protocol::BlockNumber> getFinalizedBlockNumber() const
    {
        std::shared_lock lock(x_state);
        return m_finalizedBlockNumber;
    }

private:
    struct TrackedHeadBlock
    {
        h256 hash;
        bcos::protocol::BlockNumber blockNumber = 0;
    };

    struct PayloadEntry
    {
        std::uint32_t version = 0;
        ExecutionPayload executionPayload;
        u256 blockValue = 0;
        std::optional<BlobsBundleV1> blobsBundle;
        bool shouldOverrideBuilder = false;
        std::shared_ptr<ViewType> view;
    };

    /// Version-gate upper bound is member state, not a compile-time/static constant (task-5a,
    /// spec §6.3, decision table "V4 放宽门控"): the generic composition root leaves
    /// `m_maxEngineVersion` at its default (V3, identical to the pre-existing `static` bound --
    /// zero drift); only the OP composition root passes `maxEngineVersion = 4` at construction.
    /// The lower bound (V1) stays a compile-time constant -- only the upper bound is a runtime
    /// (per-instance, constructor-time-fixed) gate, matching the decision's literal wording
    /// ("版本上界成员化").
    bool isVersionSupported(std::uint32_t version) const
    {
        return version >= static_cast<std::uint32_t>(EngineApiVersion::V1) &&
               version <= m_maxEngineVersion;
    }

    static PayloadStatus makeStatus(PayloadValidationStatus status,
        std::optional<h256> latestValidHash = std::nullopt,
        std::optional<std::string> validationError = std::nullopt)
    {
        return PayloadStatus{
            .status = status,
            .latestValidHash = latestValidHash,
            .validationError = std::move(validationError),
        };
    }

    GetPayloadResult handleGetPayload(const PayloadID& payloadId, std::uint32_t version) const
    {
        if (!isVersionSupported(version))
        {
            BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                                  << bcos::errinfo_comment{"Unsupported Engine API version"});
        }
        if constexpr (c_opMode)
        {
            // Design §6.3 (裁定 A6): `getPayloadV4`'s stated behaviour is "OP 模式下返回明确错误
            // (出块未 OP 化)". The refusal is unconditional in OP mode rather than V4-only,
            // because the *cause* is version-independent: OP mode never builds a payload at all
            // (the attributes path above refuses with -38003), so the cache is always empty and
            // every version would otherwise answer the misleading `UnknownPayload`. Full V4
            // response shaping (executionRequests etc.) ships with attributes-driven building on
            // the §6.4 欠账台账.
            BOOST_THROW_EXCEPTION(
                OpPayloadBuildingUnsupported{} << bcos::errinfo_comment{
                    "getPayload is not supported in OP mode (block building is not OP-ized)"});
        }
        else
        {
            std::shared_lock lock(x_state);
            auto it = m_payloadCache.find(payloadId);
            if (it == m_payloadCache.end())
            {
                BOOST_THROW_EXCEPTION(UnknownPayload{} << bcos::errinfo_comment{"Unknown payload"});
            }
            if (!detail::isGetPayloadVersionCompatible(
                    static_cast<EngineApiVersion>(version), it->second.version))
            {
                BOOST_THROW_EXCEPTION(
                    IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                        "Payload version is incompatible with requested method version"});
            }

            return GetPayloadResult{
                .executionPayload = it->second.executionPayload,
                .blockValue = it->second.blockValue,
                .blobsBundle = it->second.blobsBundle,
                .shouldOverrideBuilder = it->second.shouldOverrideBuilder,
            };
        }
    }

    bcos::task::Task<PayloadStatus> handleNewPayload(
        const NewPayloadRequest& request, std::uint32_t version)
    {
        if (!isVersionSupported(version))
        {
            BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                                  << bcos::errinfo_comment{"Unsupported Engine API version"});
        }
        // OP branch (task-5b, design §6.1). Compile-time dispatch on `c_opMode` (task-5a): the
        // generic composition root never instantiates `handleOpNewPayload`, and its own path
        // below is the unconditional `else`, i.e. byte-for-byte the pre-existing body.
        if constexpr (c_opMode)
        {
            co_return co_await handleOpNewPayload(request, version);
        }
        else
        {
            if (auto validationError =
                    detail::validateExecutionPayload(request.executionPayload, version);
                validationError.has_value())
            {
                auto status = validationError->find("blockHash") != std::string::npos ?
                                  PayloadValidationStatus::InvalidBlockHash :
                                  PayloadValidationStatus::Invalid;
                co_return makeStatus(status, std::nullopt, validationError);
            }
            if (version <= 2 && request.parentBeaconBlockRoot.has_value())
            {
                co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                    std::string("parentBeaconBlockRoot is only valid for newPayloadV3"));
            }
            if (version == 3)
            {
                if (!request.parentBeaconBlockRoot.has_value())
                {
                    co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                        std::string(
                            "parentBeaconBlockRoot must be a 32-byte hash for newPayloadV3"));
                }
                if (request.expectedBlobVersionedHashes.empty() &&
                    !request.executionPayload.transactions.empty())
                {
                    co_return makeStatus(
                        PayloadValidationStatus::Accepted, std::nullopt, std::nullopt);
                }
            }

            std::unique_lock lock(x_state);
            auto parentKnown =
                request.executionPayload.parentHash == m_forkchoiceState.headBlockHash ||
                m_blockHashToPayloadId.contains(request.executionPayload.parentHash);
            if (!parentKnown)
            {
                co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
            }

            auto payloadIdIt = m_blockHashToPayloadId.find(request.executionPayload.blockHash);
            PayloadID payloadId;
            if (payloadIdIt == m_blockHashToPayloadId.end())
            {
                payloadId = nextPayloadID();
                m_blockHashToPayloadId.emplace(request.executionPayload.blockHash, payloadId);
            }
            else
            {
                payloadId = payloadIdIt->second;
            }

            PayloadEntry entry{
                .version = version,
                .executionPayload = request.executionPayload,
                .blockValue = 0,
                .blobsBundle = std::nullopt,
                .shouldOverrideBuilder = false,
                .view = nullptr,
            };
            if (version == static_cast<std::uint32_t>(EngineApiVersion::V3))
            {
                entry.blobsBundle = BlobsBundleV1{};
            }

            // If this payload was built locally (via updateForkchoice), commit the view's
            // state changes to storage. Externally received payloads have no view to commit.
            // TODO: merge pushView + mergeBackStorage into a single atomic mergeView()
            // operation. This will eliminate the risk of leaking a mutable layer if
            // mergeBackStorage throws, and avoid holding x_state across a co_await.
            auto it = m_payloadCache.find(payloadId);
            if (it != m_payloadCache.end() && it->second.view)
            {
                m_globalStateStorage.get().pushView(std::move(*it->second.view));
                co_await m_globalStateStorage.get().mergeBackStorage();
            }

            m_payloadCache[payloadId] = std::move(entry);

            co_return makeStatus(
                PayloadValidationStatus::Valid, request.executionPayload.blockHash, std::nullopt);
        }
    }

    /// newPayload OP branch (design §6.1, task-5b). Only ever instantiated when `c_opMode` is
    /// true -- i.e. when `SchedulerType` is the OP scheduler -- which is what makes every
    /// `SchedulerType::`-qualified name below legal without this library depending on bcos-evm:
    /// they are dependent names, resolved at instantiation in a translation unit that included
    /// the OP scheduler header (see `bcos-evm/bcos-evm/engine/OpEngineSeam.h`'s file comment for
    /// the seam's full rationale, and `c_opMode` above for the purity constraint).
    ///
    /// The six numbered steps below are design §6.1's six steps, in order.
    bcos::task::Task<PayloadStatus> handleOpNewPayload(
        const NewPayloadRequest& request, std::uint32_t version)
    {
        auto const& payload = request.executionPayload;

        // ---- Step 1: timestamp x version gate (-38005) ----
        // Isthmus+ payloads may arrive only on V4; a pre-Isthmus timestamp may not use V4.
        // `isIsthmusActiveAt` lives on the scheduler so the threshold comparison stays on the OP
        // side next to `configAt` (design §4.2 "不重复实现阈值比较"). It is deliberately not
        // `configAt` itself: that function resolves sub-`isthmusTime` timestamps to the Isthmus
        // config too (OpForkSchedule.h's own note; task-4 review item M4), so it cannot answer
        // "is this payload pre-Isthmus?" -- the exact question this gate asks.
        const bool isthmusActive = m_scheduler.get().isIsthmusActiveAt(payload.timestamp);
        constexpr std::uint32_t c_opIsthmusPayloadVersion = 4;
        if (isthmusActive != (version == c_opIsthmusPayloadVersion))
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    isthmusActive ?
                        "Isthmus+ payloads require engine_newPayloadV4 (JSON-RPC -38005)" :
                        "engine_newPayloadV4 requires an Isthmus+ payload timestamp (JSON-RPC "
                        "-38005)"});
        }

        // ---- Step 2: static validation + blockHash ----
        // All rejections here carry latestValidHash = null: they happen before parentKnown, so no
        // ancestor has been established as valid yet (design §6.1 step 5, "blockHash 失配桶恒
        // null"). Note the status is `Invalid`, never `InvalidBlockHash` -- that Engine API status
        // has been deprecated since Shanghai and the OP branch does not use it (the enumerator
        // stays in Types.h for the generic path; deviation ledger entry).
        if (auto validationError = detail::validateOpNewPayloadRequest(
                request, m_scheduler.get().isJovianActiveAt(payload.timestamp));
            validationError.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError);
        }
        // `ExecutionPayload` carries no transactionsRoot, so it is derived from the raw envelopes
        // here -- the same derivation `executeOpBlock` performs internally (one shared function,
        // `OpEngineSeam.h`'s `computeOpTxRoot`), which is what lets the blockHash check stay
        // genuinely static (before parentKnown and before execution, as §6.1 orders it).
        const auto transactionsRoot = SchedulerType::computeTxRoot(*payload.rawTransactions);
        const auto ethHeader =
            detail::rebuildOpEthHeader(payload, transactionsRoot, *request.parentBeaconBlockRoot);
        if (ethHeader.hash() != payload.blockHash)
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string("blockHash does not match the reconstructed block header"));
        }

        // From here on the storage/execution segment runs under `x_state`, which is safety
        // premise 2 of the design's coroutine contract (§4.4: "engine 执行段被 x_state 锁串行").
        // The lock is held across `co_await`s -- sanctioned by that same section, whose premise 1
        // is that every storage2 backend in play completes synchronously in-thread (memory
        // MultiLayerStorage / blocking RocksDB reads), so the nested awaits degenerate into plain
        // stack recursion and no coroutine ever resumes on another thread. §4.4's invalidation
        // criterion explicitly covers this lock-across-co_await usage as well: if any backend
        // ever completes asynchronously, this must be redesigned.
        std::unique_lock lock(x_state);

        // ---- Step 3: parentKnown, via storage (not the in-memory map) ----
        // OP semantics (design §6.1 step 3, rev.2): op-node relies on SYNCING to drive its own
        // sync, and the answer must reflect what is actually persisted, so the query goes to
        // `SYS_HASH_2_NUMBER` through the ledger accessor rather than to `m_blockHashToPayloadId`
        // (which the generic path uses and which the OP branch never populates). Nothing is
        // written on this path.
        auto view = m_globalStateStorage.get().fork();
        auto parentBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, payload.parentHash, bcos::ledger::fromStorage);
        if (!parentBlockNumber.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
        // Past this point the parent IS a verified ancestor -- that is precisely the operational
        // definition design §6.1 step 5 (裁定 C2) gives "parent 已验证": present in
        // `SYS_HASH_2_NUMBER`, a table only the VALID branch below writes. So every INVALID from
        // here on reports `latestValidHash = parentHash`.
        const auto latestValidHash = std::make_optional(payload.parentHash);

        // Parent/child number continuity (task-5b review I2). Not spelled out among §6.1's six
        // steps, but load-bearing here: `parentBlockNumber` is already in hand, and both
        // registration indices written in step 6 (`SYS_NUMBER_2_HASH` and the ETH header table)
        // are keyed BY NUMBER. Without this check a payload naming a verified parent but carrying
        // an arbitrary `blockNumber` would silently overwrite an existing height's index entries
        // -- a corrupted chain index, not merely a rejected block. Classified with steps 4/5
        // (INVALID + latestValidHash = parent) because the parent is established valid.
        if (payload.blockNumber != *parentBlockNumber + 1)
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("blockNumber must be exactly one greater than the parent's"));
        }

        // ---- Step 4: execute ----
        view.newMutable();
        auto fiscoHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader();
        fiscoHeader->setNumber(payload.blockNumber);
        fiscoHeader->setTimestamp(static_cast<int64_t>(payload.timestamp));
        // Aggregate init of a dependent type: `BlockEnv`'s member names are resolved at
        // instantiation. `fiscoHeader` outlives the call below (it is bound by reference).
        // Both narrowings are total here -- step 2's validation rejected out-of-range values.
        typename SchedulerType::BlockEnv blockEnv{
            .fiscoHeader = *fiscoHeader,
            .parentHash = payload.parentHash,
            .prevRandao = payload.prevRandao,
            .baseFeePerGas = payload.baseFeePerGas,
            .feeRecipient = payload.feeRecipient,
            .parentBeaconBlockRoot = *request.parentBeaconBlockRoot,
            .gasLimit = detail::narrowU256ToU64(payload.gasLimit).value(),
            .extraData = payload.extraData,
            .blobGasUsed = detail::narrowU256ToU64(*payload.blobGasUsed).value(),
        };

        std::optional<typename SchedulerType::ExecuteResult> executeResult;
        try
        {
            executeResult.emplace(co_await m_scheduler.get().executeOpBlock(
                view, blockEnv, *payload.rawTransactions));
        }
        // Error classification table (design §4.3/§6.1 step 4). The two catch bodies contain no
        // `co_await` -- an await-expression may not appear inside a handler ([expr.await]/2).
        catch (const typename SchedulerType::ConsensusError& e)
        {
            // A consensus-level rejection IS a verdict on the block: INVALID.
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("OP block execution rejected the payload: ") + e.what());
        }
        catch (const typename SchedulerType::StorageError& e)
        {
            // A storage fault is NOT a verdict on the block: -32603, never INVALID. Reporting it
            // as INVALID would make this node vote against a block it merely failed to read.
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    std::string("OP block execution hit a storage failure: ") + e.what()});
        }

        // ---- Step 5: the six-way comparison surface ----
        // Exactly six (design §4.1/§6.1 "六项比对面"): receiptsRoot / logsBloom / withdrawalsRoot
        // from the seal, stateRoot / gasUsed / txRoot from the execution result. The two extra
        // comparisons after them are design §5.1's, not part of the six -- see
        // `OpBlockCommitments`. `validationError` names the offending field (design §6.1 step 4
        // "validationError 点名字段").
        const auto commitments = SchedulerType::commitmentsOf(*executeResult);
        std::optional<std::string> mismatchedField;
        if (commitments.receiptsRoot != payload.receiptsRoot)
        {
            mismatchedField = "receiptsRoot";
        }
        else if (commitments.logsBloom != detail::toEthLogsBloom(payload.logsBloom))
        {
            mismatchedField = "logsBloom";
        }
        else if (commitments.withdrawalsRoot != *payload.withdrawalsRoot)
        {
            mismatchedField = "withdrawalsRoot";
        }
        else if (commitments.stateRoot != payload.stateRoot)
        {
            mismatchedField = "stateRoot";
        }
        else if (commitments.gasUsed != payload.gasUsed)
        {
            mismatchedField = "gasUsed";
        }
        else if (commitments.txRoot != transactionsRoot)
        {
            // Structurally equal by construction today (both come from `computeOpTxRoot` over the
            // same `rawTransactions`), and kept anyway: it is the guard that would fire the day
            // execution starts deriving txRoot from its own *parsed* interpretation of the
            // transactions instead of from the wire bytes.
            mismatchedField = "transactionsRoot";
        }
        else if (commitments.blobGasUsed.has_value() &&
                 u256(*commitments.blobGasUsed) != *payload.blobGasUsed)
        {
            // Jovian+ only (engaged iff the fork repurposes the slot as the DA footprint --
            // design §5.1 "由 seal 比对承接"). Pre-Jovian the slot is pinned to 0 by step 2.
            mismatchedField = "blobGasUsed";
        }
        else if (commitments.requestsHash.has_value() &&
                 *commitments.requestsHash != ethHeader.requestsHash)
        {
            // Catches drift between this library's copy of the OP empty-requests constant (used
            // to reconstruct the header above) and the OP side's own `OP_EMPTY_REQUESTS_HASH`.
            mismatchedField = "requestsHash";
        }
        if (mismatchedField.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("execution result does not match payload field: ") + *mismatchedField);
        }

        // ---- Step 6: block registration, then publish the view ----
        co_await registerOpBlock(view, payload, ethHeader, *executeResult);
        // `pushView` alone is enough for the forkchoice path to see this block: it reads through
        // `fork()`, and MultiLayerStorage makes pushed layers visible to subsequent forks without
        // a `mergeBackStorage()` (design §6.1 step 6 "merge 时机"). No chain-head progress table
        // is written this cycle (裁定 A4): FCU stays read-only + in-memory, and
        // `SYS_CURRENT_STATE`'s current number is on the §6.4 欠账台账.
        //
        // Known consequence, deliberately not solved here (task-5b review I3, for the §6.4
        // ledger): this branch NEVER calls `mergeBackStorage()`, so every accepted block leaves
        // one more immutable layer on the MultiLayerStorage stack -- unbounded growth, and read
        // amplification linear in the number of blocks accepted since process start. Deciding
        // when to merge (and how it interacts with reorg windows) belongs to the orchestration
        // layer that also owns `SYS_CURRENT_STATE`'s head progression; both land together when
        // the loop is wired into a real node, and both are on the same §6.4 欠账台账. The minimal
        // loop's block counts are small enough that this is a scaling issue, not a correctness
        // one.
        m_globalStateStorage.get().pushView(std::move(view));
        co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
    }

    /// Block registration (design §6.1 step 6, table-level manifest). Everything lands in the one
    /// mutable layer the caller opened, so the caller's single `pushView` publishes it atomically.
    ///
    /// Key/value encodings for the two ledger tables are copied byte-for-byte from the production
    /// precedent `transaction-scheduler/bcos-transaction-scheduler/BaselineScheduler.h:207-220`
    /// (`SYS_NUMBER_2_HASH`: key = number as a decimal string, value = the hash's raw 32 bytes;
    /// `SYS_HASH_2_NUMBER`: key = the hash's raw 32 bytes, value = number as a decimal string) --
    /// which is also what makes step 3's `getBlockNumber(..., fromStorage)` lookup find them.
    bcos::task::Task<void> registerOpBlock(ViewType& view, const ExecutionPayload& payload,
        const bcos::codec::rlp::EthBlockHeader& ethHeader,
        const typename SchedulerType::ExecuteResult& executeResult)
    {
        const auto blockNumberStr = boost::lexical_cast<std::string>(payload.blockNumber);

        storage::Entry numberToHashEntry;
        numberToHashEntry.set(payload.blockHash.asBytes());
        co_await storage2::writeOne(view,
            executor_v1::StateKey{ledger::SYS_NUMBER_2_HASH, blockNumberStr},
            std::move(numberToHashEntry));

        storage::Entry hashToNumberEntry;
        hashToNumberEntry.set(blockNumberStr);
        co_await storage2::writeOne(view,
            executor_v1::StateKey{
                ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(payload.blockHash)},
            std::move(hashToNumberEntry));

        // ETH/OP header RLP, in the OP-only table whose name the OP seam publishes (裁定 B5: the
        // constant lives in bcos-evm/bcos-evm/engine/, not in bcos-framework's LedgerTypeDef.h).
        // Key convention = SYS_NUMBER_2_HASH's, value = the 21-field RLP whose keccak is the
        // block hash just verified in step 2.
        storage::Entry headerEntry;
        headerEntry.set(ethHeader.encode());
        co_await storage2::writeOne(view,
            executor_v1::StateKey{SchedulerType::c_ethBlockHeaderTable, blockNumberStr},
            std::move(headerEntry));

        // Receipts through the existing receipt channel: same table, same key (tx hash) and same
        // value (`TransactionReceipt::encode`) as `bcos-ledger/LedgerMethods.h:106-119`'s
        // `prewriteBlockToBuffer`. The tx hash is keccak over the raw EIP-2718 envelope -- the ETH
        // transaction-hash definition, and the only one available here: the OP path carries raw
        // bytes, not `bcos::protocol::Transaction` objects. (This is also why OP mode requires a
        // keccak256 `hashImpl` in its BlockFactory's crypto suite.)
        auto const& rawTransactions = *payload.rawTransactions;
        auto& hashImpl = *m_blockFactory->cryptoSuite()->hashImpl();
        // `processOpBlock` produces exactly one receipt per transaction. A divergence is a broken
        // invariant in the execution layer, not a condition to paper over: truncating to the
        // shorter of the two (task-5b review M2) would silently drop receipts from the registry
        // while still reporting the block VALID. Fail loudly instead -- and as an internal error,
        // since it is this node's bug, not a verdict on the payload.
        if (rawTransactions.size() != executeResult.receipts.size())
        {
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    "OP block execution returned a receipt count differing from the transaction "
                    "count"});
        }
        for (std::size_t index = 0; index < rawTransactions.size(); ++index)
        {
            auto const& receipt = executeResult.receipts[index];
            if (!receipt)
            {
                BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                          "OP block execution returned a null receipt"});
            }
            bcos::bytes encodedReceipt;
            receipt->encode(encodedReceipt);
            const auto txHash = hashImpl.hash(rawTransactions[index]);

            storage::Entry receiptEntry;
            receiptEntry.set(std::move(encodedReceipt));
            co_await storage2::writeOne(view,
                executor_v1::StateKey{
                    ledger::SYS_HASH_2_RECEIPT, bcos::concepts::bytebuffer::toView(txHash)},
                std::move(receiptEntry));
        }
    }

    PayloadID nextPayloadID() { return detail::encodePayloadSequence(m_nextPayloadSequence++); }

    bcos::task::Task<ExecutionPayload> buildPayload(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, const PayloadID& payloadId,
        std::uint32_t version, bcos::protocol::BlockNumber nextBlockNumber,
        std::vector<protocol::Transaction::Ptr> sealedTxs, ViewType& view) const
    {
        ExecutionPayload executionPayload{
            .parentHash = forkchoiceState.headBlockHash,
            .feeRecipient = payloadAttributes.suggestedFeeRecipient,
            .stateRoot = detail::syntheticHash(std::string("state") + payloadId),
            .receiptsRoot = detail::syntheticHash(std::string("receipts") + payloadId),
            .logsBloom = Bloom{},
            .prevRandao = payloadAttributes.prevRandao,
            .blockNumber = nextBlockNumber,
            .gasLimit = 0,
            .gasUsed = 0,
            .timestamp = payloadAttributes.timestamp,
            .extraData = {},
            .baseFeePerGas = 0,
            .blockHash = detail::syntheticHash(payloadId),
            .transactions = std::move(sealedTxs),
            .withdrawals = std::nullopt,
            .blobGasUsed = std::nullopt,
            .excessBlobGas = std::nullopt,
        };

        if (version >= static_cast<std::uint32_t>(EngineApiVersion::V2))
        {
            executionPayload.withdrawals =
                payloadAttributes.withdrawals.value_or(std::vector<WithdrawalV1>{});
        }
        if (version >= static_cast<std::uint32_t>(EngineApiVersion::V3))
        {
            executionPayload.blobGasUsed = u256(0);
            executionPayload.excessBlobGas = u256(0);
        }

        // Step 2a: Get LedgerConfig via storage-based LedgerMethods
        // Uses the parent block number since system configs are effective up to the parent
        ledger::LedgerConfig ledgerConfig;
        co_await ledger::getLedgerConfig(view, ledgerConfig, nextBlockNumber - 1, *m_blockFactory);
        auto blockVersion = ledgerConfig.compatibilityVersion();

        // Fill gasLimit from ledger config (FISCO-BCOS does not use EIP-1559 baseFeePerGas,
        // and logsBloom is not part of BlockHeader hash computation in FISCO-BCOS).
        executionPayload.gasLimit = std::get<0>(ledgerConfig.gasLimit());

        // Real EVM execution: execute transactions and compute real hashes.
        if (executionPayload.transactions.empty())
        {
            auto emptyHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader();
            std::vector<bcos::protocol::ParentInfo> parentInfos{
                {.blockNumber = nextBlockNumber - 1, .blockHash = forkchoiceState.headBlockHash}};
            emptyHeader->setParentInfo(parentInfos);
            emptyHeader->setNumber(nextBlockNumber);
            emptyHeader->setVersion(blockVersion);
            emptyHeader->setTimestamp(static_cast<int64_t>(payloadAttributes.timestamp));
            emptyHeader->setStateRoot(co_await calculateStateRoot(view, emptyHeader->version()));
            emptyHeader->setReceiptsRoot(h256{});
            emptyHeader->setTxsRoot(h256{});
            emptyHeader->setGasUsed(0);
            emptyHeader->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());
            executionPayload.stateRoot = emptyHeader->stateRoot();
            executionPayload.receiptsRoot = h256{};
            executionPayload.gasUsed = 0;
            executionPayload.blockHash = emptyHeader->hash();
            co_return executionPayload;
        }

        // Step 2b: Create BlockHeader for the new block
        auto blockHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader();
        std::vector<bcos::protocol::ParentInfo> parentInfos{
            {.blockNumber = nextBlockNumber - 1, .blockHash = forkchoiceState.headBlockHash}};
        blockHeader->setParentInfo(parentInfos);
        blockHeader->setNumber(nextBlockNumber);
        blockHeader->setVersion(blockVersion);
        blockHeader->setTimestamp(static_cast<int64_t>(payloadAttributes.timestamp));

        // Step 2c: Execute transactions via the scheduler
        // Use views::indirect to dereference shared_ptr<Transaction> -> const Transaction&
        auto receipts = co_await m_scheduler.get().executeBlock(view, m_executor.get(),
            *blockHeader, executionPayload.transactions | ::ranges::views::indirect, ledgerConfig);

        // Step 2d: Compute transaction root (Merkle over tx hashes)
        // TODO: Use scheduler_v1::calculateTransactionRoot from BaselineScheduler.h
        // once MPTStorage is available. The current tx->hash() call lacks exception
        // handling for malformed transactions.
        h256 txRoot;
        {
            auto& hashImpl = *m_blockFactory->cryptoSuite()->hashImpl();
            auto hasher = hashImpl.hasher();
            crypto::merkle::Merkle<std::remove_reference_t<decltype(hasher)>> merkle(
                hasher.clone());
            if (!executionPayload.transactions.empty())
            {
                auto txHashes = executionPayload.transactions |
                                ::ranges::views::transform([](auto& tx) { return tx->hash(); });
                std::vector<h256> merkleTrie;
                merkle.generateMerkle(txHashes, merkleTrie);
                if (!merkleTrie.empty())
                {
                    txRoot = merkleTrie.back();
                }
            }
        }

        // Step 2e: Compute receipt root (Merkle over receipt hashes)
        h256 receiptRoot;
        {
            // Validate receipts are non-null before computing hashes
            if (::ranges::any_of(receipts, [](auto& r) { return !r; }))
            {
                BOOST_THROW_EXCEPTION(std::runtime_error{"Null receipt returned by scheduler"});
            }
            auto& hashImpl = *m_blockFactory->cryptoSuite()->hashImpl();
            auto hasher = hashImpl.hasher();
            crypto::merkle::Merkle<std::remove_reference_t<decltype(hasher)>> merkle(
                hasher.clone());
            if (!receipts.empty())
            {
                auto receiptHashes =
                    receipts | ::ranges::views::transform([](auto& r) { return r->hash(); });
                std::vector<h256> merkleTrie;
                merkle.generateMerkle(receiptHashes, merkleTrie);
                if (!merkleTrie.empty())
                {
                    receiptRoot = merkleTrie.back();
                }
            }
        }

        // Step 2f: Compute gas used and block-level logsBloom from receipts
        u256 totalGasUsed;
        Bloom logsBloom{};
        for (auto& receipt : receipts)
        {
            if (!receipt)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error{"Null receipt returned by scheduler"});
            }
            totalGasUsed += receipt->gasUsed();
            orBloom(logsBloom, receipt->logsBloom());
        }

        // Step 2g: Compute state root (MPT over state storage)
        h256 stateRoot = co_await calculateStateRoot(view, blockHeader->version());

        // Step 2h: Set computed values in the block header and calculate block hash
        blockHeader->setStateRoot(stateRoot);
        blockHeader->setReceiptsRoot(receiptRoot);
        blockHeader->setTxsRoot(txRoot);
        blockHeader->setGasUsed(totalGasUsed);
        blockHeader->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());

        // Step 2i: Fill the execution payload with real values
        executionPayload.stateRoot = stateRoot;
        executionPayload.receiptsRoot = receiptRoot;
        executionPayload.gasUsed = totalGasUsed;
        executionPayload.blockHash = blockHeader->hash();
        executionPayload.gasLimit = std::get<0>(ledgerConfig.gasLimit());
        executionPayload.logsBloom = logsBloom;

        co_return executionPayload;
    }

    /// Compute state root by iterating over storage and XOR-ing entry hashes.
    /// This is a simplified MPT approximation; for full correctness use
    /// scheduler_v1::calculateStateRoot from BaselineScheduler.h.
    /// TODO: Replace with scheduler_v1::calculateStateRoot from BaselineScheduler.h
    /// once MPTStorage is available. The XOR approach is not collision-resistant
    /// and is a consensus risk for production use.
    task::Task<h256> calculateStateRoot(ViewType& view, uint32_t blockVersion) const
    {
        auto range = co_await storage2::range(view);
        h256 totalHash;
        while (auto keyValue = co_await range.next())
        {
            auto& [key, value] = *keyValue;
            executor_v1::StateKeyView viewKey(key);
            auto [tableName, keyName] = viewKey.get();

            storage::Entry entry;
            if (auto* e = std::get_if<storage::Entry>(std::addressof(value)))
            {
                entry = *e;
            }
            else
            {
                entry.setStatus(storage::Entry::DELETED);
            }
            totalHash ^= entry.hash(
                tableName, keyName, *m_blockFactory->cryptoSuite()->hashImpl(), blockVersion);
        }
        co_return totalHash;
    }


    std::optional<bcos::protocol::BlockNumber> lookupBlockNumberByHash(const h256& blockHash) const
    {
        auto it = m_blockHashToPayloadId.find(blockHash);
        if (it == m_blockHashToPayloadId.end())
        {
            return std::nullopt;
        }

        auto payloadIt = m_payloadCache.find(it->second);
        if (payloadIt == m_payloadCache.end())
        {
            return std::nullopt;
        }
        return payloadIt->second.executionPayload.blockNumber;
    }

    void updateTrackedBlockNumbers(std::optional<bcos::protocol::BlockNumber> safeBlockNumber,
        std::optional<bcos::protocol::BlockNumber> finalizedBlockNumber)
    {
        m_safeBlockNumber = safeBlockNumber;
        m_finalizedBlockNumber = finalizedBlockNumber;
    }

    mutable std::shared_mutex x_state;
    std::reference_wrapper<MemPoolType> m_memPool;
    std::reference_wrapper<GlobalStateStorageType> m_globalStateStorage;
    int64_t m_blockTxCountLimit;
    std::reference_wrapper<ExecutorType> m_executor;
    std::reference_wrapper<SchedulerType> m_scheduler;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    std::uint32_t m_maxEngineVersion;
    ForkchoiceState m_forkchoiceState;
    std::optional<TrackedHeadBlock> m_trackedHeadBlock;
    std::optional<bcos::protocol::BlockNumber> m_safeBlockNumber;
    std::optional<bcos::protocol::BlockNumber> m_finalizedBlockNumber;
    std::unordered_map<PayloadID, PayloadEntry> m_payloadCache;
    std::unordered_map<h256, PayloadID> m_blockHashToPayloadId;
    std::uint64_t m_nextPayloadSequence = 1;
};

}  // namespace bcos::engine
