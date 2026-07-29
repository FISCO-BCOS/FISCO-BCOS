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

#include "bcos-crypto/merkle/Merkle.h"
#include "bcos-framework/engine/EngineService.h"
#include "bcos-framework/engine/Types.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Bloom.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/FixedBytes.h"
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
            if (auto validationError =
                    detail::validatePayloadAttributes(*payloadAttributes, version);
                validationError.has_value())
            {
                ForkchoiceUpdatedResult result{
                    .payloadStatus =
                        makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError),
                    .payloadId = std::nullopt,
                };
                co_return result;
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

        // Mempool operations run without lock — they only depend on the view, not on x_state.
        // Step 1: Remove stale/tainted transactions from mempool (cleans tx with nonce < state
        // nonce)
        m_memPool.get().remove(view);

        // Step 2: Create mutable storage layer on the view for transaction sealing
        view.newMutable();

        // Step 3: Pull valid transactions from mempool (in nonce order, with nonce verification)
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

    bcos::task::Task<PayloadStatus> handleNewPayload(
        const NewPayloadRequest& request, std::uint32_t version)
    {
        if (!isVersionSupported(version))
        {
            BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                                  << bcos::errinfo_comment{"Unsupported Engine API version"});
        }

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
                    std::string("parentBeaconBlockRoot must be a 32-byte hash for newPayloadV3"));
            }
            if (request.expectedBlobVersionedHashes.empty() &&
                !request.executionPayload.transactions.empty())
            {
                co_return makeStatus(PayloadValidationStatus::Accepted, std::nullopt, std::nullopt);
            }
        }

        std::unique_lock lock(x_state);
        auto parentKnown = request.executionPayload.parentHash == m_forkchoiceState.headBlockHash ||
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
