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
#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/engine/EngineService.h"
#include "bcos-framework/engine/Errors.h"
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
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Bloom.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/FixedBytes.h"
#include <bcos-framework/protocol/BlockHeader.h>
#include <boost/lexical_cast.hpp>
#include <cstdint>
#include <deque>
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

// ---- OP-mode exceptions ----
// These carry the JSON-RPC error codes the Engine API assigns to these conditions. The mapping
// exception-type -> code is intentionally not implemented yet: `engine_newPayloadV4` /
// `engine_getPayloadV4` RPC endpoint registration is out of scope for this cycle, so the OP
// branch is exercised by direct `EngineServiceImpl` calls and the code is
// documentation-of-intent for whoever wires `bcos-rpc`'s EngineEndpoint later. The existing
// generic exceptions above are in exactly the same position (no code mapping in-repo).

/// JSON-RPC -38005 "Unsupported fork": the payload timestamp's fork and the called method version
/// disagree -- Isthmus+ payloads may only arrive on V4, pre-Isthmus timestamps may not use V4.
DERIVE_BCOS_EXCEPTION(UnsupportedFork);

/// JSON-RPC -38003 "Invalid payload attributes": an OP-mode forkchoiceUpdated carried payload
/// attributes. Attribute-driven block building is not supported this cycle; the forkchoice state
/// update itself still takes effect (head advances), only the build is refused.
DERIVE_BCOS_EXCEPTION(UnsupportedOpPayloadAttributes);

/// `getPayload` in OP mode: block *building* is not OP-ized this cycle, so there is no OP payload
/// to return and the failure is reported explicitly rather than as `UnknownPayload` (which would
/// falsely suggest an expired/unknown id).
DERIVE_BCOS_EXCEPTION(OpPayloadBuildingUnsupported);

namespace detail
{
std::string encodePayloadSequence(std::uint64_t value);

bcos::h256 syntheticHash(std::string_view seed);

std::vector<std::string> supportedCapabilities();

/// OP-mode capability list: `supportedCapabilities()` plus the V4 entries. Selected via
/// `if constexpr` on `EngineServiceImpl::c_opMode` in `exchangeCapabilities` below -- never
/// reached by the generic composition root, so the generic path's capability list stays
/// byte-for-byte the pre-existing 10 entries.
std::vector<std::string> supportedOpCapabilities();

bool isGetPayloadVersionCompatible(ApiVersion requestVersion, std::uint32_t payloadVersion);

std::optional<std::string> validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version);

std::optional<std::string> validateExecutionPayload(
    const ExecutionPayload& executionPayload, std::uint32_t version);

// ---- OP-mode helpers ----

/// Bounds-checked u256 -> uint64 narrowing (nullopt when out of range). Explicit rather than a
/// bare `convert_to`/`static_cast`: this repo has a documented silent-truncation incident with
/// unchecked wide-integer narrowing (MEMORY costofprecompiled-int64-overflow), and
/// `OpSchedulerImpl.h`'s `narrowU256ToU64` applies the same discipline on the execution side.
std::optional<std::uint64_t> narrowU256ToU64(const u256& value);

/// `Bloom` (std::array<byte,256>, the ExecutionPayload representation) -> `h2048` (the
/// protocol::BlockHeader::logsBloom representation).
bcos::h2048 toEthLogsBloom(const Bloom& logsBloom);

/// OP static validation (everything except the blockHash comparison, which needs the
/// caller-derived transactionsRoot). Returns a field-naming validationError string on rejection.
/// `jovianActive` selects the fork-dependent `blobGasUsed` rule.
std::optional<std::string> validateOpNewPayloadRequest(
    const NewPayloadRequest& request, bool jovianActive);

/// Compute expected baseFeePerGas from the parent header.
/// Mirrors op-geth consensus/misc/eip1559/eip1559.go:CalcBaseFee, including Holocene extraData
/// elasticity/denominator, Jovian blobGasUsed substitution, and Jovian minBaseFee floor.
/// `parentIsJovian`/`parentIsIsthmus` derive from the parent's own timestamp — matching op-geth's
/// `config.IsJovian(parent.Time)` / `config.IsOptimismHolocene(parent.Time)`. Reads the parent
/// header's accessors: `extraData()` (Holocene/Jovian params), `gasLimit()`, `gasUsed()`,
/// `blobGasUsed()`, `baseFee()` (review fix: does not read timestamp; optional fields must use
/// `.value()`).
bcos::u256 calcOpBaseFee(
    const bcos::protocol::BlockHeader& parent, bool parentIsJovian, bool parentIsIsthmus);

/// The OP header's 3 post-merge constants (ommersHash/difficulty/nonce) — the values live in
/// EngineServiceImpl.cpp's anonymous namespace; this accessor lets the engine's step-2 blockHash
/// check and the test seal helpers share one source. `BlockHeader::OpHeaderConst` is the carrier
/// the protocol::BlockHeader OP capability takes.
bcos::protocol::BlockHeader::OpHeaderConst opHeaderConst();

/// Reconstructs the 21-field ETH/OP header from the payload as a FISCO `protocol::BlockHeader`
/// (tars) with 18 fields filled; the 3 post-merge constants are
/// supplied separately via `opHeaderConst()` when encoding/hashing. `opHeaderHash` (codec) is what
/// the payload's `blockHash` is checked against, and `header->encode()` (tars) is what the block
/// registration stores. Precondition: `validateOpNewPayloadRequest` accepted the request.
bcos::protocol::BlockHeader::Ptr rebuildOpEthHeader(
    const bcos::protocol::BlockHeaderFactory::Ptr& factory, const ExecutionPayload& payload,
    const h256& transactionsRoot, const h256& parentBeaconBlockRoot);

/// OP envelope -> tars Transaction conversion (implemented in EngineServiceImpl.cpp; returns
/// nullopt on failure, does not throw). Fills extraTransactionHash and sender. 0x04 (EIP-7702)
/// is decoded via Web3Transaction RLP (a first-class type since upstream #5411); only a malformed
/// or un-enumerated envelope yields nullopt. Forward-declared here with the implementation in the
/// .cpp because EngineServiceImpl.h is included by many consumers (engine/test, libinitializer,
/// bcos-evm opstack tests) and pulling in Web3Transaction.h would drag in jsoncpp + rpc Common +
/// RPC_LOG macro pollution. `bcostars::Transaction` is fully visible via LedgerMethods.h, so no
/// extra forward declaration is needed.
std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash);
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

    /// Compile-time OP-mode probe (no runtime bool, matching this class's all-template style):
    /// detects `SchedulerType::executeOpBlock` via an unevaluated `requires`-expression. Only the
    /// OP scheduler defines that member, so `c_opMode` is false for every scheduler used by the
    /// generic composition root (StubScheduler/BloomScheduler in EngineServiceTest.cpp,
    /// SchedulerSerialImpl in production) and the generic path is byte-for-byte unaffected.
    ///
    /// Takes the *address* of the member function template rather than calling it, pinning an
    /// explicit template argument only for the sole `auto`-deduced parameter (`executeOpBlock`'s
    /// third, `::ranges::input_range auto const& rawTxBytes`); the fixed `OpBlockEnv`-typed second
    /// parameter needs no argument *value* to resolve, so no bcos-evm type needs to be named here
    /// (library-purity constraint). `std::vector<bcos::bytes>` matches how the real scheduler is
    /// instantiated. Address-of is an unevaluated operand inside a requires-expression
    /// ([expr.prim.req.simple]), so it does not odr-use (and therefore does not force
    /// instantiation of) the function body.
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
        bcos::ledger::LedgerInterface::Ptr ledger = nullptr,
        int64_t blockTxCountLimit = bcos::engine::c_defaultBlockTxCountLimit,
        std::uint32_t maxEngineVersion = static_cast<std::uint32_t>(ApiVersion::V3),
        bcos::scheduler::SchedulerInterface::Ptr delegate = nullptr)
      : m_memPool(std::ref(memPool)),
        m_globalStateStorage(std::ref(globalStateStorage)),
        m_blockTxCountLimit(blockTxCountLimit),
        m_executor(std::ref(executor)),
        m_scheduler(std::ref(scheduler)),
        m_blockFactory(std::move(blockFactory)),
        m_maxEngineVersion(maxEngineVersion),
        m_ledger(std::move(ledger)),
        m_delegate(std::move(delegate))
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
        // opMode compile-time branch: selects the capability list at compile time via
        // `if constexpr` on `c_opMode` -- not a runtime bool -- so the generic composition
        // root's codegen for this function is exactly what it was before (the `else` branch,
        // unconditionally).
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
            // OP mode skips this pre-check entirely: the OP answer to *any* attributes object --
            // well-formed or not -- is -38003, and it must be given only AFTER the forkchoice
            // state has been updated (the forkchoice update is not rolled back, the head advances
            // normally, only block building is not started). Returning an Invalid payloadStatus
            // here would abort the update instead. The refusal is raised further down, where the
            // generic path would start building. `if constexpr` keeps the generic path's codegen
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
        // Scope note: this rule is this function's, not the class's. The OP `newPayload` branch
        // (`handleOpNewPayload` below) deliberately DOES hold `x_state` across `co_await`s --
        // required by the op-validator-loop coroutine contract (safety premise 2: the engine
        // execution segment is serialized by the x_state lock), which grants the permission
        // conditionally: every storage2 backend in play completes synchronously in-thread, so no
        // coroutine ever resumes on another thread. That contract's invalidation criterion
        // explicitly covers both that usage and the pre-existing lock-across-`co_await` TODO in
        // `handleNewPayload`'s generic path. The two are consistent: this comment states the
        // default, the coroutine contract states the audited exception.
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
            // An OP validator's L1-derivation path *does* send forkchoiceUpdated with
            // OP-extended attributes (noTxPool=true + derived transactions) whenever consolidation
            // fails or there is no unsafe block -- "the validator does not send attributes" is not
            // true. Attribute-driven building is not supported this cycle (the same road as
            // OP-izing getPayload), so the answer is -38003.
            //
            // Placement is the whole point: everything above -- the storage lookups, the
            // monotonicity checks, and the tracked-head/safe/finalized update under `x_state` --
            // has already run and is *kept*. The forkchoice update is not rolled back; only the
            // build is refused.
            BOOST_THROW_EXCEPTION(
                UnsupportedOpPayloadAttributes{} << bcos::errinfo_comment{
                    "Payload attributes are not supported in OP mode (block building is not "
                    "OP-ized; JSON-RPC -38003)"});
        }
        else
        {
            // Mempool operations run without lock — they only depend on the state, not on x_state.
            // Step 1: Remove stale/tainted transactions from mempool (cleans tx with nonce < state
            // nonce).
            // Step 2: Seal valid transactions (in nonce order, with nonce verification) directly on
            // the executed view. Both MemPoolImpl::remove() and MemPoolImpl::seal() are read-only
            // with respect to the given view: remove() only erases the mempool's own container, and
            // seal() only reads the sender's current nonce to pick the gapless executable prefix.
            // It deliberately does NOT advance the nonce in the view — the authoritative nonce
            // advance happens during execution itself (evmone), matching how geth's legacypool and
            // reth's best_transactions() select block transactions without touching state. Sealing
            // on the executed view is therefore safe: evmone validates tx.nonce >= state.nonce, and
            // the state nonce here is still the committed one.
            std::vector<protocol::Transaction::Ptr> sealedTxs;
            view.newMutable();
            m_memPool.get().remove(view);
            m_memPool.get().seal(m_blockTxCountLimit, view, std::back_inserter(sealedTxs));

            // Step 3: Build the payload on the executed view (already mutable above).
            auto payloadId = nextPayloadID();
            auto nextBlockNumber = *headBlockNumber + 1;
            auto built = co_await buildPayload(forkchoiceState, *payloadAttributes, payloadId,
                version, nextBlockNumber, std::move(sealedTxs), view);
            PayloadEntry entry{
                .version = version,
                .executionPayload = std::move(built.executionPayload),
                .blockValue = 0,
                .blobsBundle = std::nullopt,
                .shouldOverrideBuilder = false,
                .view = std::make_shared<ViewType>(std::move(view)),
                .header = std::move(built.header),
                .receipts = std::move(built.receipts),
            };
            if (version == static_cast<std::uint32_t>(ApiVersion::V3))
            {
                entry.blobsBundle = BlobsBundleV1{};
            }

            // Re-acquire lock to publish the built payload to the cache.
            {
                std::unique_lock lock(x_state);
                m_blockHashToPayloadId[entry.executionPayload.blockHash] = payloadId;
                m_payloadCache[payloadId] = std::move(entry);
                // Bound the payload cache at insert time. A payload is only read between
                // updateForkchoice / getPayload and newPayload; the single-node driver can skip
                // newPayload entirely (empty block with produce_empty_blocks=false — the EEST
                // configuration — or getPayload returning null), and an external CL may abandon
                // a requested payload. None of those reach handleNewPayload's eviction, so
                // without a bound here each skipped tick would retain a live storage fork (the
                // PayloadEntry::view) plus the block's transactions forever.
                m_payloadOrder.push_back(payloadId);
                while (m_payloadOrder.size() > c_maxPayloadEntries)
                {
                    auto const evictedId = m_payloadOrder.front();
                    m_payloadOrder.pop_front();
                    m_payloadCache.erase(evictedId);
                    std::erase_if(m_blockHashToPayloadId,
                        [&](auto const& kv) { return kv.second == evictedId; });
                }
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
        /// Built-block artifacts kept so newPayload() can persist the ledger block tables
        /// (SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER /
        /// SYS_CURRENT_STATE / SYS_NUMBER_2_TXS / SYS_BLOCK_NUMBER_2_NONCES /
        /// SYS_HASH_2_RECEIPT / SYS_HASH_2_TX) via ledger::prewriteBlockToBuffer. Externally
        /// received payloads leave these null/empty.
        bcos::protocol::BlockHeader::Ptr header;
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
    };

    /// Version-gate upper bound is member state, not a compile-time/static constant: the generic
    /// composition root leaves `m_maxEngineVersion` at its default (V3, identical to the
    /// pre-existing `static` bound -- zero drift); only the OP composition root passes
    /// `maxEngineVersion = 4` at construction. The lower bound (V1) stays a compile-time constant
    /// -- only the upper bound is a runtime (per-instance, constructor-time-fixed) gate ("version
    /// upper bound is member state").
    bool isVersionSupported(std::uint32_t version) const
    {
        return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
               version <= m_maxEngineVersion;
    }

    static PayloadStatus makeStatus(PayloadValidationStatus status,
        std::optional<h256> latestValidHash = std::nullopt,
        std::optional<std::string> validationError = std::nullopt)
    {
        return PayloadStatus{
            .latestValidHash = latestValidHash,
            .validationError = std::move(validationError),
            .status = status,
        };
    }

    /// Maps a delegate-reported SchedulerError code back to a classified outcome (v3 P1-6):
    /// OpConsensusRejected -> INVALID (with latestValidHash = the verified parent); OpStorageFault
    /// / UnknownError / any other code -> JSON-RPC -32603 (OpExecutionInternalError, never
    /// INVALID). The catch(...) fallback in handleOpNewPayload's barrier preserves the
    /// unclassified -> -32603 semantics for exceptions that escape the delegate path entirely.
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

    GetPayloadResult handleGetPayload(const PayloadID& payloadId, std::uint32_t version) const
    {
        if (!isVersionSupported(version))
        {
            BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                                  << bcos::errinfo_comment{"Unsupported Engine API version"});
        }
        if constexpr (c_opMode)
        {
            // `getPayloadV4`'s stated behaviour is "return an explicit error in OP mode (block
            // building is not OP-ized)". The refusal is unconditional in OP mode rather than
            // V4-only, because the *cause* is version-independent: OP mode never builds a payload
            // at all (the attributes path above refuses with -38003), so the cache is always
            // empty and every version would otherwise answer the misleading `UnknownPayload`.
            // Full V4 response shaping (executionRequests etc.) ships together with
            // attributes-driven building.
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
                    static_cast<ApiVersion>(version), it->second.version))
            {
                BOOST_THROW_EXCEPTION(
                    IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                        "Payload version is incompatible with requested method version"});
            }

            return std::make_unique<GetPayloadData>(GetPayloadData{
                .executionPayload = it->second.executionPayload,
                .blockValue = it->second.blockValue,
                .blobsBundle = it->second.blobsBundle,
                .shouldOverrideBuilder = it->second.shouldOverrideBuilder,
                .executionRequests = std::nullopt,
            });
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
        // ---- opMode silent-failure guardrail ----
        //
        // `c_opMode` is a SFINAE probe on `executeOpBlock`'s exact signature shape. If the probe
        // ever silently collapses (signature change), a V4 request would fall through to the
        // generic path, where an OP payload is accepted and answered VALID with its self-reported
        // blockHash without the block ever being executed -- a validator that rubber-stamps is
        // worse than one that refuses. Hence a non-OP build refuses V4 outright, independently of
        // `maxEngineVersion`.
        //
        // Zero behavioural drift for the generic composition root as configured today: it leaves
        // `maxEngineVersion` at 3, so `isVersionSupported(4)` already threw the same exception
        // before this statement was reachable. Defence in depth for the mis-configuration, not a
        // new rule for the existing one.
        if constexpr (!c_opMode)
        {
            if (version >= static_cast<std::uint32_t>(ApiVersion::V3) + 1)
            {
                BOOST_THROW_EXCEPTION(
                    UnsupportedEngineApiVersion{} << bcos::errinfo_comment{
                        "engine_newPayloadV4 requires an OP-mode scheduler; this build's "
                        "c_opMode probe did not detect one"});
            }
        }
        // OP branch. Compile-time dispatch on `c_opMode`: the generic composition root never
        // instantiates `handleOpNewPayload`, and its own path below is the unconditional `else`,
        // i.e. byte-for-byte the pre-existing body.
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
                .header = nullptr,
                .receipts = {},
            };
            if (version == static_cast<std::uint32_t>(ApiVersion::V3))
            {
                entry.blobsBundle = BlobsBundleV1{};
            }

            // If this payload was built locally (via updateForkchoice), commit the view's
            // state changes to storage. Externally received payloads have no view to commit.
            // mergeView() (commit 589d15dd8) removed the risk of leaking a mutable layer when
            // mergeBackStorage throws (the OP path at EngineServiceImpl.h:1165 uses it too).
            // Remaining TODO: avoid holding x_state across a co_await -- the OP path still holds
            // x_state across mergeView's internal co_await (acknowledged); this generic path
            // still does the two-step pushView+mergeBackStorage (semantically equivalent, can
            // switch to mergeView later).
            auto it = m_payloadCache.find(payloadId);
            if (it != m_payloadCache.end() && it->second.view)
            {
                m_globalStateStorage.get().pushView(std::move(*it->second.view));
                if (m_ledger && it->second.header)
                {
                    // Locally built payload: persist the ledger block tables atomically with
                    // the state merge, using the same FIB-104 prewriteBlockToBuffer pattern the
                    // BaselineScheduler commit path uses. Without these rows a produced block is
                    // invisible to eth_getBlockByNumber / eth_getBlockByHash /
                    // eth_getTransactionReceipt and to ledger::getBlockHash /
                    // getCurrentBlockNumber.
                    typename GlobalStateStorageType::MutableStorage prewriteStorage;
                    auto block = m_blockFactory->createBlock();
                    block->setBlockHeader(it->second.header);
                    // Persist the block-level logsBloom (computed in buildPayload from the
                    // per-receipt blooms). Without it every block produced by this driver
                    // answers eth_getBlockByNumber with 256 zero bytes — the legacy
                    // BaselineScheduler commit path sets the block bloom before commit, so
                    // this restores parity with that path (eth_getLogs uses it as a filter).
                    auto const& bloom = it->second.executionPayload.logsBloom;
                    block->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
                    for (auto const& tx : it->second.executionPayload.transactions)
                    {
                        block->appendTransaction(tx);
                    }
                    for (auto const& receipt : it->second.receipts)
                    {
                        block->appendReceipt(receipt);
                    }
                    auto blockTxs = std::make_shared<protocol::ConstTransactions>(
                        it->second.executionPayload.transactions |
                        ::ranges::views::transform(
                            [](auto const& tx) { return protocol::Transaction::ConstPtr(tx); }) |
                        ::ranges::to<std::vector>());
                    co_await ledger::prewriteBlockToBuffer(
                        *m_ledger, blockTxs, block, prewriteStorage);
                    co_await m_globalStateStorage.get().mergeBackStorage(prewriteStorage);
                }
                else
                {
                    co_await m_globalStateStorage.get().mergeBackStorage();
                }
            }

            m_payloadCache[payloadId] = std::move(entry);

            // Evict stale payload entries. A payload is only read between updateForkchoice /
            // getPayload and newPayload, so once a block is committed its payloadId and
            // blockHash are unreachable. The built-in single-node driver mints one new
            // payloadId per block_interval tick, so without eviction both maps grow by one
            // row per produced block and hold strong references to every transaction ever
            // executed (unbounded memory over time). Keep only the just-committed block; the
            // newPayload() parent check accepts the head hash directly, so dropping older
            // blockHash rows is safe.
            std::erase_if(m_blockHashToPayloadId,
                [&](auto const& kv) { return kv.first != request.executionPayload.blockHash; });
            std::erase_if(m_payloadCache, [&](auto const& kv) { return kv.first != payloadId; });

            co_return makeStatus(
                PayloadValidationStatus::Valid, request.executionPayload.blockHash, std::nullopt);
        }
    }

    /// newPayload OP branch. Only ever instantiated when `c_opMode` is true -- i.e. when
    /// `SchedulerType` is the OP scheduler -- which is what makes every `SchedulerType::`-qualified
    /// name below legal without this library depending on bcos-evm: they are dependent names,
    /// resolved at instantiation in a translation unit that included the OP scheduler header (see
    /// `bcos-evm/bcos-evm/engine/OpEngineSeam.h`'s file comment for the seam's full rationale, and
    /// `c_opMode` above for the purity constraint).
    ///
    /// This function is the version gate plus a **classification barrier**: it guarantees that
    /// every way out of the OP branch is one of the classified outcomes (a `PayloadStatus`,
    /// `UnsupportedFork`, or `OpExecutionInternalError`) and that nothing escapes unclassified.
    /// The steps themselves live in `runOpNewPayloadSteps` so that a single try/catch pair can
    /// cover all of them without wrapping the version gate too -- see the barrier below.
    bcos::task::Task<PayloadStatus> handleOpNewPayload(
        const NewPayloadRequest& request, std::uint32_t version)
    {
        auto const& payload = request.executionPayload;

        // ---- Step 1: timestamp x version gate (-38005) ----
        // The OP engine branch is scoped to Isthmus/Jovian only: a pre-Isthmus timestamp is
        // answered -38005 *unconditionally*, on any version. Isthmus+ payloads must then arrive
        // on V4 (Isthmus+ payloads are not allowed on V3).
        // `isIsthmusActiveAt` lives on the scheduler so the threshold comparison stays on the OP
        // side next to `configAt` (do not re-implement the threshold comparison). It is
        // deliberately not `configAt` itself: that function resolves sub-`isthmusTime` timestamps
        // to the Isthmus config too (see OpForkSchedule.h's own note), so it cannot answer "is
        // this payload pre-Isthmus?" -- the exact question this gate asks.
        //
        // Deliberately OUTSIDE the barrier below: `UnsupportedFork` (-38005) is itself a
        // classified outcome and must reach the caller unchanged, not be re-labelled.
        const bool isthmusActive = m_scheduler.get().isIsthmusActiveAt(payload.timestamp);
        constexpr std::uint32_t c_opIsthmusPayloadVersion = 4;
        if (!isthmusActive)
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "pre-Isthmus payloads are not supported by the OP engine branch (JSON-RPC "
                    "-38005)"});
        }
        if (version != c_opIsthmusPayloadVersion)
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "Isthmus+ payloads require engine_newPayloadV4 (JSON-RPC -38005)"});
        }

        // ---- Classification barrier ----
        //
        // The `catch (...)` added around `executeOpBlock` in the first pass closed only ONE
        // window. Everything else in the OP branch still ran outside any handler: step 2's
        // `computeTxRoot` / `rebuildOpEthHeader` / `hash()`, step 5's `commitmentsOf` and the
        // comparisons, and the whole of step 6's `registerOpBlock` -- whose `lexical_cast`,
        // `Entry::set`, `ethHeader.encode()`, `receipt->encode()`, `hashImpl.hash()` and four
        // `storage2::writeOne` calls can each raise something that is neither
        // `OpExecutionInternalError` nor an execution-classified error (`bad_alloc`, a tars
        // encoding error, ...). Such an escape would leave `handleOpNewPayload` entirely and
        // surface at the caller's `co_await` as neither INVALID nor -32603 -- an outcome the
        // error classification rules rule out. (The two
        // `BOOST_THROW_EXCEPTION(OpExecutionInternalError)` calls inside `registerOpBlock` were
        // never the problem: they arrive already classified, and the rethrow handler below
        // preserves them verbatim.)
        //
        // The barrier is a wrapper rather than an outer try around the existing body so that the
        // version gate above stays outside it, and so the already-classified paths keep their own
        // (more specific) messages: `catch (const OpExecutionInternalError&) { throw; }` passes
        // through the non-tip refusal, the receipt-count invariant, and the execution-phase
        // fallback untouched, while `catch (...)` labels everything else.
        try
        {
            co_return co_await runOpNewPayloadSteps(request);
        }
        catch (const OpExecutionInternalError&)
        {
            // Already classified (and carrying a more specific marker) -- pass through unchanged.
            throw;
        }
        catch (...)
        {
            // -32603 rather than INVALID, for the same reason as the execution-phase fallback: an
            // unknown local failure must not make this node vote against a block. The marker
            // distinguishes this barrier from that fallback, so a test can tell which window an
            // escape came through (a bare `catch (...)` otherwise collapses every refusal into
            // one indistinguishable type).
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    "OP newPayload threw an unclassified exception outside block execution "
                    "(validation, comparison or registration phase)"});
        }
    }

    /// Never called outside `handleOpNewPayload`'s classification barrier;
    /// the signature carries no OP-dependent name, so the declaration instantiates harmlessly for
    /// the generic composition root while the body (which is full of them) only instantiates in
    /// OP mode.
    bcos::task::Task<PayloadStatus> runOpNewPayloadSteps(const NewPayloadRequest& request)
    {
        auto const& payload = request.executionPayload;

        // ---- Step 2: static validation + blockHash ----
        // All rejections here carry latestValidHash = null: they happen before parentKnown, so no
        // ancestor has been established as valid yet (the blockHash-mismatch bucket is always
        // null). Note the status is `Invalid`, never `InvalidBlockHash` -- that Engine API status
        // has been deprecated since Shanghai and the OP branch does not use it (the enumerator
        // stays in Types.h for the generic path).
        if (auto validationError = detail::validateOpNewPayloadRequest(
                request, m_scheduler.get().isJovianActiveAt(payload.timestamp));
            validationError.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError);
        }
        // `ExecutionPayload` carries no transactionsRoot, so it is derived from the raw envelopes
        // here -- the same derivation `executeOpBlock` performs internally (one shared function,
        // `OpEngineSeam.h`'s `computeOpTxRoot`), which is what lets the blockHash check stay
        // genuinely static (before parentKnown and before execution).
        const auto transactionsRoot = SchedulerType::computeTxRoot(*payload.rawTransactions);
        const auto ethHeader = detail::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(),
            payload, transactionsRoot, *request.parentBeaconBlockRoot);
        // OP hash = keccak(RLP(21 fields)), via the protocol::BlockHeader OP capability
        // (opHeaderConst injects the 3 post-merge constants). Must not use BlockHeader::hash() --
        // an empty tars dataHash throws EmptyBlockHeaderHash, and if the factory back-fills it the
        // result would be the tars-order hash (a known pitfall).
        if (ethHeader->opHeaderHash(detail::opHeaderConst()) != payload.blockHash)
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string("blockHash does not match the reconstructed block header"));
        }

        // From here on the storage/execution segment runs under `x_state`, which is safety
        // premise 2 of the design's coroutine contract: the engine execution segment is serialized
        // by the x_state lock. The lock is held across `co_await`s -- sanctioned by that same
        // contract, whose premise 1 is that every storage2 backend in play completes synchronously
        // in-thread (memory MultiLayerStorage / blocking RocksDB reads), so the nested awaits
        // degenerate into plain stack recursion and no coroutine ever resumes on another thread.
        // The contract's invalidation criterion explicitly covers this lock-across-co_await usage
        // as well: if any backend ever completes asynchronously, this must be redesigned.
        std::unique_lock lock(x_state);

        // ---- Step 3: parentKnown, via storage (not the in-memory map) ----
        // OP semantics: op-node relies on SYNCING to drive its own sync, and the answer must
        // reflect what is actually persisted, so the query goes to `SYS_HASH_2_NUMBER` through the
        // ledger accessor rather than to `m_blockHashToPayloadId` (which the generic path uses and
        // which the OP branch never populates). Nothing is written on this path.
        auto view = m_globalStateStorage.get().fork();
        auto parentBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, payload.parentHash, bcos::ledger::fromStorage);
        if (!parentBlockNumber.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
        // Past this point the parent IS a verified ancestor -- that is precisely the operational
        // definition of "parent verified": present in `SYS_HASH_2_NUMBER`, a table only the VALID
        // branch below writes. So every INVALID from here on reports `latestValidHash =
        // parentHash`.
        const auto latestValidHash = std::make_optional(payload.parentHash);

        // Parent/child number continuity. Load-bearing here: `parentBlockNumber` is already in
        // hand, and both registration indices written in step 6 (`SYS_NUMBER_2_HASH` and the ETH
        // header table) are keyed BY NUMBER. Without this check a payload naming a verified parent
        // but carrying an arbitrary `blockNumber` would silently overwrite an existing height's
        // index entries -- a corrupted chain index, not merely a rejected block. Classified with
        // steps 4/5 (INVALID + latestValidHash = parent) because the parent is established valid.
        if (payload.blockNumber != *parentBlockNumber + 1)
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("blockNumber must be exactly one greater than the parent's"));
        }

        // ---- Step 3a: timestamp strictly increases ----
        //
        // op-geth rejects this twice over: `eth/catalyst/api.go:891-894`
        // (`block.Time() <= parent.Time()` -> invalid, latestValidHash = parent) and
        // `consensus/beacon/consensus.go:253-256` (`errInvalidTimestamp`).
        //
        // Here the timestamp is also the FORK SELECTOR: `OpSchedulerImpl` picks its
        // `OpForkConfig` with `configAt(fiscoHeader.timestamp(), ...)` and step 1's -38005 gate
        // reads the same field, so without monotonicity a payload could roll the active fork
        // backwards (Jovian -> Isthmus), changing DA accounting and baseFee semantics mid-chain.
        //
        // Keyed by NUMBER (op-geth keys by HASH, `api.go:887`); equivalent only while
        // number -> header is injective, which step 3c guarantees today (at most one block per
        // height). Whoever lifts step 3c must re-key this read by hash in the same change.
        //
        // Deliberately ordered BEFORE step 3b's already-known-block short-circuit, unlike op-geth
        // (known-block first at `api.go:872-876`, timestamp after) -- observationally equivalent,
        // since a re-delivered block necessarily passed this check when first accepted.
        //
        // Missing parent header => SKIP, deliberately (the genesis/first-block case): a trusted
        // starting point is established by seeding `SYS_HASH_2_NUMBER` alone, so the first block
        // after it has no stored parent header to compare against. Every block registered BY this
        // loop does store its header, so the check is live for every subsequent block.
        const auto parentNumberStr = boost::lexical_cast<std::string>(*parentBlockNumber);
        // The parent header is read from the standard s_number_2_header table as a tars
        // BlockHeader, replacing the retired s_eth_block_header RLP read path. Entry::get()
        // returns a string_view (Entry.h:332) and createBlockHeader has no string_view overload
        // -- copy explicitly into bytes.
        if (auto parentHeaderEntry = co_await storage2::readOne(view,
                executor_v1::StateKeyView{ledger::SYS_NUMBER_2_BLOCK_HEADER, parentNumberStr});
            parentHeaderEntry.has_value())
        {
            const auto storedHeader = parentHeaderEntry->get();  // string_view
            bcos::protocol::BlockHeader::Ptr parentHeader;
            try
            {
                bcos::bytes parentHeaderBytes(storedHeader.begin(), storedHeader.end());
                parentHeader =
                    m_blockFactory->blockHeaderFactory()->createBlockHeader(parentHeaderBytes);
            }
            catch (const std::exception& e)
            {
                // Our own stored bytes failed to parse: a local storage/consistency fault, not a
                // verdict on the incoming payload (the error-classification rule cuts both ways).
                BOOST_THROW_EXCEPTION(
                    OpExecutionInternalError{} << bcos::errinfo_comment{
                        std::string("stored parent block header is undecodable: ") + e.what()});
            }
            // tars
            // Tars decoding is lenient: a truncated stream may silently fill defaults. This table
            // is keyed by height and written only by this loop, so a height mismatch is a local
            // fault.
            if (parentHeader->number() != static_cast<int64_t>(*parentBlockNumber))
            {
                BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                          "stored parent block header height mismatch"});
            }
            // Note: createBlockHeader(bytesConstRef) auto back-fills dataHash with the tars-order
            // hasher when it is empty (BlockHeaderFactoryImpl.cpp:21-37) -- that value is NOT the
            // OP hash. This step only reads timestamp/baseFee/extraData/gasLimit/gasUsed/
            // blobGasUsed, never hash(), so it is unaffected; this is a known consequence of
            // deferred dataHash, and any future consumer of OP headers must not call hash().
            // Monotonicity: payload seconds -> milliseconds, compared in the same unit as the
            // parent header (milliseconds).
            if (static_cast<uint64_t>(payload.timestamp) * 1000 <=
                static_cast<uint64_t>(parentHeader->timestamp()))
            {
                co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                    std::string("timestamp must be strictly greater than the parent's"));
            }

            // Step 3a-2: baseFee consistency (Holocene+ EIP-1559 with Optimism
            // extensions). op-geth checks this in consensus/misc/eip1559/eip1559.go's
            // VerifyEIP1559Header → CalcBaseFee. The parent header is already in hand
            // from the timestamp check above, and fork membership is determined from
            // the parent's own timestamp — matching op-geth's
            // config.IsJovian(parent.Time) / config.IsOptimismHolocene(parent.Time).
            // Fork determination switches back to seconds (consistent with the payload seconds and
            // the config thresholds; the reverse direction of the milliseconds comparison above).
            {
                auto expectedBaseFee = detail::calcOpBaseFee(*parentHeader,
                    m_scheduler.get().isJovianActiveAt(
                        static_cast<uint64_t>(parentHeader->timestamp()) / 1000),
                    m_scheduler.get().isIsthmusActiveAt(
                        static_cast<uint64_t>(parentHeader->timestamp()) / 1000));
                if (payload.baseFeePerGas != expectedBaseFee)
                {
                    co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                        std::string("baseFeePerGas does not match the value computed "
                                    "from the parent"));
                }
            }
        }

        // ---- Step 3b: already-known block -> VALID short-circuit ----
        //
        // op-geth does exactly this (`eth/catalyst/api.go:872-876`): a payload whose block is
        // already in the chain returns VALID without re-executing. Re-delivery is a routine
        // path, not a reorg -- CL timeouts re-send, and op-node replays unsafe blocks after a
        // restart. Without this short-circuit the second delivery re-executes ON TOP OF the
        // state the first delivery already committed, which produces a guaranteed-wrong result
        // and answers INVALID: user-transaction blocks fail inside `processOpBlock` (the sender
        // nonce has already advanced), deposit-only blocks fail the receiptsRoot comparison (the
        // mint is credited twice). Answering INVALID there would make op-node mark a block it
        // itself just accepted as bad -- the one case in this implementation where a block
        // op-geth accepts would be rejected.
        //
        // Placement is load-bearing: AFTER step 2, so a malformed payload is still rejected on
        // its own merits rather than waved through on a hash match; AFTER parentKnown/continuity,
        // so the answer is only ever given for a block that sits where it claims to sit; and
        // BEFORE the non-tip check below, because a re-delivered block always has a child height
        // occupied (by itself) and would otherwise trip that check.
        //
        // `SYS_HASH_2_NUMBER` is written only by the VALID branch below, so presence there means
        // "this exact block was executed and accepted by this node" -- the same operational
        // definition step 5 uses for the parent.
        if (auto knownBlockNumber = co_await bcos::ledger::getBlockNumber(
                view, payload.blockHash, bcos::ledger::fromStorage);
            knownBlockNumber.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
        }

        // ---- Step 3c: the parent must be the current chain tip ----
        //
        // `fork()` above hands back the top of the layer stack -- the CURRENT tip's state -- not
        // the state as of `payload.parentHash`. Whenever those differ, execution would silently
        // run against the wrong base state and produce a result that cannot match the payload,
        // reported as INVALID: a wrong verdict dressed up as a consensus judgement.
        //
        // The criterion is one-directional: an occupied child height ALWAYS implies the parent is
        // not the tip (this is the direction used to refuse); an empty child height implies the
        // parent IS the tip only under the invariant that every hash in `SYS_HASH_2_NUMBER`
        // occupies its own height in `SYS_NUMBER_2_HASH`. `registerOpBlock` below writes both,
        // always together, as does the production precedent `BaselineScheduler.h:207-220` -- so on
        // a real ledger the criterion is exact; a store where the two disagree (notably a test
        // fixture that seeds only `SYS_HASH_2_NUMBER`, a documented exemption) can present an
        // empty child height for a parent that is not the tip and slip through. The parent/child
        // number continuity check above does not close that gap: a parent seeded at height 5 with
        // a payload at height 6 satisfies it.
        //
        // (A block already occupying the child height that IS this payload was short-circuited
        // immediately above, so reaching here with an occupied height means a genuine
        // sibling/side-chain delivery.)
        //
        // Answering `OpExecutionInternalError` (-32603) rather than INVALID is the honest
        // reply: this node cannot evaluate the block, which is a capability limit, not a verdict.
        // Serving arbitrary-parent base state needs a `blockHash -> MLS layer` mapping that does
        // not exist today; that is architecture work, parked (this cycle delivers the explicit
        // refusal, not the capability).
        const auto childNumberStr = boost::lexical_cast<std::string>(payload.blockNumber);
        if (auto occupiedHeight = co_await storage2::readOne(
                view, executor_v1::StateKeyView{ledger::SYS_NUMBER_2_HASH, childNumberStr});
            occupiedHeight.has_value())
        {
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    "non-tip parent not supported: a different block is already registered at "
                    "this height, so the forked view's base state is not the payload's parent"});
        }

        // ---- Step 4: delegate block execution + commit (wiring Task 5b) ----
        //
        // The OP block's executeOpBlock / six-way comparison / registerOpBlock are no longer
        // driven inline here: they are absorbed by the delegate (OpScheduler's execute /
        // verifyResult / commit hooks running on the shared SchedulerSkeleton). The engine keeps
        // the static validation above (parentKnown / continuity / 3a / 3a-2 / 3b / 3c) and the
        // classification barrier below, bridging the delegate's callback-async
        // executeBlock/commitBlock back into this coroutine's PayloadStatus (v3 P1-6).
        if (!m_delegate)
        {
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    "OP newPayload requires an m_delegate (OpScheduler) for block execution; "
                    "the composition root did not wire one"});
        }

        // Block assembly (SEV-8: extraTransactionBytes = the full EIP-2718 envelope, not the
        // signing preimage) — the delegate's execute hook re-derives rawTxBytes from it.
        auto block = buildOpBlock(payload, ethHeader);

        // SchedulerInterface executeBlock is synchronous from here (the skeleton runs task::wait
        // internally), so the callback fires before the call returns. Errors surface as
        // SchedulerError codes through the callback — never as typed OP exceptions. The header
        // returned is the delegate's executed header (finishExecute), which commitBlock consumes.
        bcos::Error::Ptr executeError;
        bcos::protocol::BlockHeader::Ptr executedHeader;
        m_delegate->executeBlock(block, /*verify=*/true,
            [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, bool) {
                executeError = std::move(error);
                executedHeader = std::move(header);
            });
        if (executeError)
        {
            co_return mapDelegateError(*executeError, latestValidHash);
        }

        bcos::Error::Ptr commitError;
        m_delegate->commitBlock(
            executedHeader, [&](bcos::Error::Ptr error, bcos::ledger::LedgerConfig::Ptr) {
                commitError = std::move(error);
            });
        if (commitError)
        {
            co_return mapDelegateError(*commitError, latestValidHash);
        }

        // The delegate's commit hook (opstackRegisterBlock) wrote the 7 ledger tables including
        // SYS_CURRENT_STATE, and the skeleton merged the execute view + commit storage atomically
        // (FIB-104). The head-advance monotonic guard semantics the inline path kept
        // (blockNumber > currentHead) are preserved by OpScheduler::commitContinuityCheck
        // (v3 P1-6), which refuses already-committed / discontinuous commits on the same view.
        co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
    }

    /// OP block assembly (wiring Task 5a, SEV-8). Build a `protocol::Block` carrying the
    /// payload's raw EIP-2718 envelopes as each transaction's `extraTransactionBytes` -- the
    /// FULL envelope, not the signing preimage (`takeToTarsTransaction` stores the preimage;
    /// the delegate's execute hook re-derives rawTxBytes from extraTransactionBytes, so the
    /// overwrite is load-bearing). Precedent: OpDualPathEquivalenceTest.cpp:566-568. Used only
    /// by the delegate path (Task 5b); the inline `executeOpBlock` path keeps its raw-byte
    /// carrier. `[[maybe_unused]]` in the 5a intermediate state, before the delegate path
    /// consumes it.
    [[maybe_unused]] bcos::protocol::Block::Ptr buildOpBlock(
        const ExecutionPayload& payload, bcos::protocol::BlockHeader::Ptr header)
    {
        auto block = m_blockFactory->createBlock();
        block->setBlockHeader(std::move(header));
        auto const& rawTransactions = *payload.rawTransactions;
        auto& hashImpl = *m_blockFactory->cryptoSuite()->hashImpl();
        for (auto const& env : rawTransactions)
        {
            const auto txHash = hashImpl.hash(env);
            auto tarsTx = detail::opEnvelopeToTars(env, txHash);
            if (!tarsTx)
            {
                // A conversion failure means the envelope is malformed or un-enumerated
                // (Web3Transaction RLP decode returned an error) -- a consensus-level rejection
                // of the block, classified INVALID by the execution layer's decodeOneRawTx
                // (OpTxDecode.h), never -32603. Step 2 (validateOpNewPayloadRequest) does NOT
                // decode envelopes, so reaching assembly does not imply every envelope is
                // canonical and enumerated. Carry the raw envelope in a minimal tars tx (only the
                // hash and wire bytes populated) so the delegate's execute hook re-derives it and
                // decodeOneRawTx issues the verdict -- the same envelope the old inline path
                // handed to executeOpBlock (which threw OpConsensusError -> INVALID).
                bcostars::Transaction fallback;
                fallback.extraTransactionHash.assign(txHash.begin(), txHash.end());
                tarsTx = std::move(fallback);
            }
            // SEV-8: takeToTarsTransaction stores the signing preimage; overwrite with the full
            // envelope so the delegate's execute hook decodes the exact wire bytes.
            tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
            auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
                [tars = std::move(*tarsTx)]() mutable { return &tars; });
            block->appendTransaction(std::move(tx));
        }
        return block;
    }


    PayloadID nextPayloadID() { return detail::encodePayloadSequence(m_nextPayloadSequence++); }

    /// Result of building a payload: the ExecutionPayload handed to the CL plus the
    /// built-block artifacts (header + receipts) needed to persist the ledger block tables
    /// when the payload is committed via newPayload().
    struct BuildPayloadResult
    {
        ExecutionPayload executionPayload;
        bcos::protocol::BlockHeader::Ptr header;
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
    };

    bcos::task::Task<BuildPayloadResult> buildPayload(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, const PayloadID& payloadId,
        std::uint32_t version, bcos::protocol::BlockNumber nextBlockNumber,
        std::vector<protocol::Transaction::Ptr> sealedTxs, ViewType& view) const
    {
        ExecutionPayload executionPayload{
            .logsBloom = Bloom{},
            .parentHash = forkchoiceState.headBlockHash,
            .stateRoot = detail::syntheticHash(std::string("state") + payloadId),
            .receiptsRoot = detail::syntheticHash(std::string("receipts") + payloadId),
            .prevRandao = payloadAttributes.prevRandao,
            .gasLimit = 0,
            .gasUsed = 0,
            .baseFeePerGas = 0,
            .blockHash = detail::syntheticHash(payloadId),
            .transactions = std::move(sealedTxs),
            .extraData = {},
            .feeRecipient = payloadAttributes.suggestedFeeRecipient,
            .timestamp = payloadAttributes.timestamp,
            .blockNumber = nextBlockNumber,
            .withdrawals = std::nullopt,
            .blobGasUsed = std::nullopt,
            .excessBlobGas = std::nullopt,
            .blockAccessList = std::nullopt,
            .slotNumber = std::nullopt,
        };

        if (version >= static_cast<std::uint32_t>(ApiVersion::V2))
        {
            executionPayload.withdrawals =
                payloadAttributes.withdrawals.value_or(std::vector<WithdrawalV1>{});
        }
        if (version >= static_cast<std::uint32_t>(ApiVersion::V3))
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
            bcos::protocol::ParentInfo parentInfo{
                .blockNumber = nextBlockNumber - 1, .blockHash = forkchoiceState.headBlockHash};
            emptyHeader->setParentInfo(parentInfo);
            emptyHeader->setNumber(nextBlockNumber);
            emptyHeader->setVersion(blockVersion);
            emptyHeader->setTimestamp(static_cast<int64_t>(payloadAttributes.timestamp));
            emptyHeader->setCoinbase(payloadAttributes.suggestedFeeRecipient);
            emptyHeader->setPrevRandao(payloadAttributes.prevRandao);
            emptyHeader->setGasLimit(u256(std::get<0>(ledgerConfig.gasLimit())));
            emptyHeader->setStateRoot(co_await calculateStateRoot(view, emptyHeader->version()));
            emptyHeader->setReceiptsRoot(h256{});
            emptyHeader->setTxsRoot(h256{});
            emptyHeader->setGasUsed(0);
            emptyHeader->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());
            executionPayload.stateRoot = emptyHeader->stateRoot();
            executionPayload.receiptsRoot = h256{};
            executionPayload.gasUsed = 0;
            executionPayload.blockHash = emptyHeader->hash();
            co_return BuildPayloadResult{.executionPayload = std::move(executionPayload),
                .header = std::move(emptyHeader),
                .receipts = {}};
        }

        // Step 2b: Create BlockHeader for the new block
        auto blockHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader();
        bcos::protocol::ParentInfo parentInfo{
            .blockNumber = nextBlockNumber - 1, .blockHash = forkchoiceState.headBlockHash};
        blockHeader->setParentInfo(parentInfo);
        blockHeader->setNumber(nextBlockNumber);
        blockHeader->setVersion(blockVersion);
        blockHeader->setTimestamp(static_cast<int64_t>(payloadAttributes.timestamp));
        blockHeader->setCoinbase(payloadAttributes.suggestedFeeRecipient);
        blockHeader->setPrevRandao(payloadAttributes.prevRandao);
        blockHeader->setGasLimit(u256(std::get<0>(ledgerConfig.gasLimit())));

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

        // Step 2f: Compute gas used and block-level logsBloom from receipts.
        u256 totalGasUsed;
        Bloom logsBloom{};
        for (auto& receipt : receipts)
        {
            if (!receipt)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error{"Null receipt returned by scheduler"});
            }
            totalGasUsed += receipt->gasUsed();
            // The v2 (pure-Ethereum) executor's receipts carry an empty logsBloom (a
            // documented limitation — evmoneReceiptToBcos does not compute it), so tolerate
            // empty blooms instead of indexing past their (zero) length.
            if (!receipt->logsBloom().empty())
            {
                orBloom(logsBloom, receipt->logsBloom());
            }
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

        co_return BuildPayloadResult{.executionPayload = std::move(executionPayload),
            .header = std::move(blockHeader),
            .receipts = std::move(receipts)};
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
    /// Optional ledger used to persist the ledger block tables when a locally built payload
    /// is committed via newPayload(). Null in unit tests / for payloads without block
    /// persistence.
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    /// OP block-execution delegate (wiring Task 5): when non-null and `c_opMode`, newPayload
    /// routes block execution + commit through `m_delegate->executeBlock/commitBlock`
    /// (SchedulerInterface face) instead of the inline `executeOpBlock` path. The delegate is
    /// the composition root's `OpScheduler` (slot 3). Null for the generic composition root and
    /// for OP fixtures that never drive block execution (ethereum instances).
    bcos::scheduler::SchedulerInterface::Ptr m_delegate;
    ForkchoiceState m_forkchoiceState;
    std::optional<TrackedHeadBlock> m_trackedHeadBlock;
    std::optional<bcos::protocol::BlockNumber> m_safeBlockNumber;
    std::optional<bcos::protocol::BlockNumber> m_finalizedBlockNumber;
    std::unordered_map<PayloadID, PayloadEntry> m_payloadCache;
    std::unordered_map<h256, PayloadID> m_blockHashToPayloadId;
    /// Insertion order of m_payloadCache payloadIds, used to bound the cache at insert time
    /// (updateForkchoice), independent of whether the caller ever reaches newPayload.
    std::deque<PayloadID> m_payloadOrder;
    /// Upper bound on retained payload entries (both m_payloadCache and m_blockHashToPayloadId
    /// rows). A payload is only needed between updateForkchoice / getPayload and newPayload.
    static constexpr size_t c_maxPayloadEntries = 64;
    std::uint64_t m_nextPayloadSequence = 1;
};

}  // namespace bcos::engine
