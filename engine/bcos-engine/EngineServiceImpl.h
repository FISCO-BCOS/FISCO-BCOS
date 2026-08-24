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

#include "bcos-crypto/hash/Keccak256.h"
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
#include "bcos-ledger/mpt/AccountStorageRoot.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Bloom.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/FixedBytes.h"
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/indirect.hpp>
#include <range/v3/view/transform.hpp>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::engine
{
// UnsupportedEngineApiVersion / UnknownPayload / IncompatiblePayloadVersion moved to
// bcos-framework/engine/Types.h so the RPC endpoint can map them to Engine error codes.
/// The L2ToL1MessagePasser predeploy. Isthmus defines the block header's withdrawalsRoot as
/// this account's storage root. Second spelling of the same consensus-critical address as
/// bcos::evm::opstack::OP_L2_TO_L1_MESSAGE_PASSER (OpPredeploys.h), duplicated to keep
/// bcos-engine off the bcos-evm include graph — that header would be includable (it is
/// header-only over evmc), the separation is a deliberate choice, not a build constraint.
/// The two must not drift; #5468 wires the validation side that compares roots derived
/// through both, which is where a cross-assertion belongs.
inline const bcos::Address c_l2ToL1MessagePasser{
    std::string_view{"4200000000000000000000000000000000000016"}, bcos::Address::FromHex};

DERIVE_BCOS_EXCEPTION(GlobalStateStorageNotConfigured);
DERIVE_BCOS_EXCEPTION(UnknownForkchoiceHeadBlock);
DERIVE_BCOS_EXCEPTION(InvalidForkchoiceState);

namespace detail
{
std::string encodePayloadSequence(std::uint64_t value);

bcos::h256 syntheticHash(std::string_view seed);

std::vector<std::string> supportedCapabilities();

bool isGetPayloadVersionCompatible(ApiVersion requestVersion, std::uint32_t payloadVersion);

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

    EngineServiceImpl(MemPoolType& memPool, GlobalStateStorageType& globalStateStorage,
        ExecutorType& executor, SchedulerType& scheduler,
        bcos::protocol::BlockFactory::Ptr blockFactory,
        bcos::ledger::LedgerInterface::Ptr ledger = nullptr,
        int64_t blockTxCountLimit = bcos::engine::c_defaultBlockTxCountLimit)
      : m_memPool(std::ref(memPool)),
        m_globalStateStorage(std::ref(globalStateStorage)),
        m_blockTxCountLimit(blockTxCountLimit),
        m_executor(std::ref(executor)),
        m_scheduler(std::ref(scheduler)),
        m_blockFactory(std::move(blockFactory)),
        m_ledger(std::move(ledger))
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
        co_return detail::supportedCapabilities();
    }

    bcos::task::Task<ForkchoiceUpdatedResult> updateForkchoice(
        const ForkchoiceState& forkchoiceState, const PayloadAttributes* payloadAttributes,
        std::uint32_t version)
    {
        if (!isForkchoiceVersionSupported(version))
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

        // Mempool operations run without lock — they only depend on the state, not on x_state.
        // Step 1: Remove stale/tainted transactions from mempool (cleans tx with nonce < state
        // nonce).
        // Step 2: Seal valid transactions (in nonce order, with nonce verification) directly on
        // the executed view. Both MemPoolImpl::remove() and MemPoolImpl::seal() are read-only
        // with respect to the given view: remove() only erases the mempool's own container, and
        // seal() only reads the sender's current nonce to pick the gapless executable prefix. It
        // deliberately does NOT advance the nonce in the view — the authoritative nonce advance
        // happens during execution itself (evmone), matching how geth's legacypool and reth's
        // best_transactions() select block transactions without touching state. Sealing on the
        // executed view is therefore safe: evmone validates tx.nonce >= state.nonce, and the
        // state nonce here is still the committed one.
        std::vector<protocol::Transaction::Ptr> sealedTxs;
        view.newMutable();
        // noTxPool=true (OP Stack): the payload must not take any transaction from the
        // pool — it contains exactly the forced transaction list (possibly none). Skip
        // both pool hygiene and sealing; forced transactions are prepended in
        // buildPayload.
        if (!payloadAttributes->noTxPool.value_or(false))
        {
            m_memPool.get().remove(view);
            m_memPool.get().seal(m_blockTxCountLimit, view, std::back_inserter(sealedTxs));
        }

        // Step 3: Build the payload on the executed view (already mutable above).
        auto payloadId = nextPayloadID();
        auto nextBlockNumber = *headBlockNumber + 1;
        auto built = co_await buildPayload(forkchoiceState, *payloadAttributes, payloadId, version,
            nextBlockNumber, std::move(sealedTxs), view);
        PayloadEntry entry{
            .version = version,
            .executionPayload = std::move(built.executionPayload),
            .blockValue = 0,
            .blobsBundle = std::nullopt,
            .shouldOverrideBuilder = false,
            .parentBeaconBlockRoot = payloadAttributes->parentBeaconBlockRoot,
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
                std::erase_if(
                    m_blockHashToPayloadId, [&](auto const& kv) { return kv.second == evictedId; });
            }
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
        /// Engine API version of the call that last wrote this entry: the forkchoiceUpdated
        /// version for a build, the newPayload version for a commit. getPayload's version
        /// window (detail::isGetPayloadVersionCompatible) is checked against it, so
        /// re-querying a payloadId AFTER committing it through newPayloadV4 answers -38005
        /// rather than replaying the payload. op-node never does that — it fetches a build
        /// exactly once — and op-geth's build cache is likewise not meant to outlive the
        /// commit.
        std::uint32_t version = 0;
        ExecutionPayload executionPayload;
        u256 blockValue = 0;
        std::optional<BlobsBundleV1> blobsBundle;
        bool shouldOverrideBuilder = false;
        /// Beacon root the payload was built with (from PayloadAttributes) or received
        /// with (from NewPayloadRequest); echoed in the getPayload response (OP Stack).
        std::optional<h256> parentBeaconBlockRoot;
        std::shared_ptr<ViewType> view;
        /// Built-block artifacts kept so newPayload() can persist the ledger block tables
        /// (SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER /
        /// SYS_CURRENT_STATE / SYS_NUMBER_2_TXS / SYS_BLOCK_NUMBER_2_NONCES /
        /// SYS_HASH_2_RECEIPT / SYS_HASH_2_TX) via ledger::prewriteBlockToBuffer. Externally
        /// received payloads leave these null/empty.
        bcos::protocol::BlockHeader::Ptr header;
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
    };

    /// Per-method Engine API version windows. forkchoiceUpdated tops out at V3 (the
    /// version Karst payload building runs on), newPayload at V4 (Isthmus payload with
    /// executionRequests), getPayload at V5 (Osaka response shape). Every version from V1
    /// up is served: adapting to Karst does not make the older versions incompatible, and
    /// the V1-V3 callers (the unsafe_allow_v1_executor harness, the integration suites)
    /// keep working.
    static bool isForkchoiceVersionSupported(std::uint32_t version)
    {
        return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
               version <= static_cast<std::uint32_t>(ApiVersion::V3);
    }

    static bool isNewPayloadVersionSupported(std::uint32_t version)
    {
        return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
               version <= static_cast<std::uint32_t>(ApiVersion::V4);
    }

    static bool isGetPayloadVersionSupported(std::uint32_t version)
    {
        return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
               version <= static_cast<std::uint32_t>(ApiVersion::V5);
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

    GetPayloadResult handleGetPayload(const PayloadID& payloadId, std::uint32_t version) const
    {
        if (!isGetPayloadVersionSupported(version))
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
                static_cast<ApiVersion>(version), it->second.version))
        {
            BOOST_THROW_EXCEPTION(
                IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                    "Payload version is incompatible with requested method version"});
        }
        // The version window alone is not enough once the entry may have been REWRITTEN by
        // a commit: newPayload replaces the cached payload with the request's, and a V3
        // request carries no withdrawalsRoot (it is a V4+/Isthmus field). Such an entry is
        // still tagged version 3, so it passes the V4/V5 window above, and
        // serializeExecutionPayload would then throw InternalError on the missing field.
        // Answer the version error it really is instead of -32603.
        if (version >= static_cast<std::uint32_t>(ApiVersion::V4) &&
            !it->second.executionPayload.withdrawalsRoot.has_value())
        {
            BOOST_THROW_EXCEPTION(IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                                      "Payload does not carry the V4+ response shape"});
        }

        return std::make_unique<GetPayloadData>(GetPayloadData{
            .executionPayload = it->second.executionPayload,
            .blockValue = it->second.blockValue,
            .blobsBundle = it->second.blobsBundle,
            .shouldOverrideBuilder = it->second.shouldOverrideBuilder,
            // getPayloadV4/V5 responses must carry executionRequests; Karst never has
            // any, so the value is a present-but-empty list (serialized as []).
            .executionRequests = version >= static_cast<std::uint32_t>(ApiVersion::V4) ?
                                     std::optional<std::vector<bytes>>{std::in_place} :
                                     std::nullopt,
            .parentBeaconBlockRoot = it->second.parentBeaconBlockRoot,
        });
    }

    bcos::task::Task<PayloadStatus> handleNewPayload(
        const NewPayloadRequest& request, std::uint32_t version)
    {
        if (!isNewPayloadVersionSupported(version))
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
                std::string("parentBeaconBlockRoot is only valid for newPayloadV3 and later"));
        }
        if (version >= 3 && !request.parentBeaconBlockRoot.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string(
                    "parentBeaconBlockRoot must be a 32-byte hash for newPayloadV3 and later"));
        }
        if (version >= 3 && !request.expectedBlobVersionedHashes.empty())
        {
            // op-geth checks expectedBlobVersionedHashes against the blob hashes carried by
            // the payload's OWN transactions and answers INVALID on any length or element
            // mismatch (beacon/engine/types.go:311-322, whose error reaches
            // eth/catalyst/api.go:867 api.invalid -> Status: engine.INVALID). No blob
            // sidecar lookup is involved, so "the EL cannot verify these" is not a state
            // this check can be in. L2 (Ecotone onwards) forbids blob transactions
            // entirely, so the payload side is always empty and any non-empty list is a
            // mismatch.
            //
            // There is deliberately no ACCEPTED escape. op-geth returns ACCEPTED from
            // exactly one place — "State not available, ignoring new payload",
            // eth/catalyst/api.go:904-907, the parent-block-known-but-state-missing case —
            // which on this stack is the parentKnown -> SYNCING branch below. Answering
            // ACCEPTED here (as the V3 path used to, whenever a payload carried
            // transactions but no blob hashes) neither validated nor stored the payload,
            // so the CL took the block as accepted while no later forkchoiceUpdated could
            // ever make it head.
            co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string("expectedBlobVersionedHashes must be empty (L2 forbids blob "
                            "transactions)"));
        }
        if (version >= 4)
        {
            // Present-but-empty, not "absent or empty": op-geth's NewPayloadV4 rejects a
            // nil executionRequests outright ("nil executionRequests post-prague",
            // eth/catalyst/api.go:755) and only then requires the list to be empty for
            // Isthmus. The RPC layer already enforces the fourth parameter, so accepting
            // nullopt here would give in-process callers a laxer contract than the wire.
            if (!request.executionRequests.has_value() || !request.executionRequests->empty())
            {
                co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                    std::string("executionRequests must be a present-but-empty list on this "
                                "chain"));
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
            .parentBeaconBlockRoot = request.parentBeaconBlockRoot,
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
        // NOTE: mergeView() exists (MultiLayerStorage.h) but is intentionally
        // non-atomic (pushView and mergeBackStorage are independent critical
        // sections). Using bare pushView here keeps the state change immediate;
        // mergeBackStorage follows in the co_await block below.
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
                // eth_getTransactionReceipt and to ledger::getBlockHash / getCurrentBlockNumber.
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
                // Raw-only entries (forced transactions) carry no decoded form and are
                // not modeled as ledger transactions until execution wiring lands; the
                // persisted transaction list therefore matches the receipts list, which
                // also only covers the executed (decoded) subset.
                for (auto const& tx : it->second.executionPayload.transactions)
                {
                    if (tx.decoded)
                    {
                        block->appendTransaction(tx.decoded);
                    }
                }
                for (auto const& receipt : it->second.receipts)
                {
                    block->appendReceipt(receipt);
                }
                auto blockTxs = std::make_shared<protocol::ConstTransactions>(
                    it->second.executionPayload.transactions |
                    ::ranges::views::filter([](auto const& tx) { return tx.decoded != nullptr; }) |
                    ::ranges::views::transform([](auto const& tx) {
                        return protocol::Transaction::ConstPtr(tx.decoded);
                    }) |
                    ::ranges::to<std::vector>());
                co_await ledger::prewriteBlockToBuffer(*m_ledger, blockTxs, block, prewriteStorage);
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
        // Dual carrier: every sealed transaction is stored with both its raw EIP-2718
        // bytes (the wire form getPayload returns) and the decoded executable form (used
        // for execution and ledger persistence). Web3 transactions reassemble their
        // exact signed raw bytes from the signing payload + signature — the same splice
        // that produces the canonical txHash.
        //
        // Only transactions with a genuine EIP-2718 wire form enter the OP payload. A
        // native Tars transaction has no such form — any bytes emitted for it would be
        // rejected by this service's own newPayload dispatch, so sealing one would make
        // the service build a payload it then judges INVALID (an FCU -> getPayload ->
        // newPayload livelock). Such transactions are excluded from the payload with a
        // warning and remain in the mempool. In production the mempool's sole ingress is
        // eth_sendRawTransaction, which admits Web3 transactions exclusively, so the
        // exclusion only ever triggers for in-process callers.
        std::vector<EngineTransaction> engineTransactions;
        engineTransactions.reserve(
            payloadAttributes.transactions.value_or(std::vector<std::string>{}).size() +
            sealedTxs.size());
        // Forced transactions (OP attributes.transactions) come FIRST, in the order the
        // CL gave them — this is the only OP-sanctioned path for deposits. Their raw
        // EIP-2718 bytes are carried byte-for-byte (hex validity and dispatch
        // admissibility were already enforced by validatePayloadAttributes, which runs
        // before buildPayload). They carry no decoded executable form yet: 0x7E deposit
        // execution (runDeposit) and raw->executable decoding for typed/legacy forced
        // transactions belong to the execution-lane wiring, so raw-only entries are
        // placed in the payload, participate in the transactions root via their
        // canonical keccak256(raw) hash, but are not executed and do not advance state.
        if (payloadAttributes.transactions.has_value())
        {
            for (auto const& forcedHex : *payloadAttributes.transactions)
            {
                engineTransactions.push_back(EngineTransaction{
                    .raw = fromHex(forcedHex),
                    .decoded = nullptr,
                });
            }
        }
        for (auto& sealedTx : sealedTxs)
        {
            if (sealedTx->type() !=
                static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
            {
                BCOS_LOG(WARNING) << LOG_BADGE("EngineService")
                                  << LOG_DESC(
                                         "buildPayload: excluding transaction without an EIP-2718 "
                                         "wire form from the OP payload")
                                  << LOG_KV("hash", sealedTx->hash().hex())
                                  << LOG_KV("type", static_cast<int>(sealedTx->type()));
                continue;
            }
            engineTransactions.push_back(EngineTransaction{
                .raw = bcostars::protocol::reassembleWeb3RawTransaction(
                    sealedTx->extraTransactionBytes(), sealedTx->signatureData()),
                .decoded = std::move(sealedTx),
            });
        }

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
            .transactions = std::move(engineTransactions),
            .extraData = {},
            .feeRecipient = payloadAttributes.suggestedFeeRecipient,
            .timestamp = payloadAttributes.timestamp,
            .blockNumber = nextBlockNumber,
            .withdrawals = std::nullopt,
            .blobGasUsed = std::nullopt,
            .excessBlobGas = std::nullopt,
            .withdrawalsRoot = std::nullopt,
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
            // Isthmus payload shape (V4/V5, fed by forkchoiceUpdatedV3 on Karst): the field
            // must be present so getPayloadV5 -> newPayloadV4 round-trips. Presence is all
            // that is decided here — fillWithdrawalsRoot() computes the value once the
            // block's transactions have run, on either return path below.
            executionPayload.withdrawalsRoot = h256{};
        }

        // Step 2a: Get LedgerConfig via storage-based LedgerMethods
        // Uses the parent block number since system configs are effective up to the parent
        ledger::LedgerConfig ledgerConfig;
        co_await ledger::getLedgerConfig(view, ledgerConfig, nextBlockNumber - 1, *m_blockFactory);
        auto blockVersion = ledgerConfig.compatibilityVersion();

        // OP Stack: attributes.gasLimit is the block gas limit the L1 SystemConfig dictates
        // through the CL. op-geth writes it verbatim into header.GasLimit
        // (miner/worker.go:362-363). Overriding ledgerConfig here — rather than only the
        // header field — is what makes it the EXECUTION budget as well: the block gas limit
        // reaches evmone through std::get<0>(ledgerConfig.gasLimit()) at
        // EthereumExecutor.cpp:23 (BlockInfo::gas_limit) and HostContext::blockGasLimit(),
        // both fed by the ledgerConfig passed to executeBlock below. The header and payload
        // fields read the same tuple, so this single assignment carries all three.
        //
        // Absent = keep the chain's own SystemConfig value. op-geth's OP path rejects a nil
        // gasLimit with -38003 (eth/catalyst/api_optimism.go:41-43), but that check guards
        // attributes that are OP attributes by construction. Here the same PayloadAttributes
        // struct also serves the vanilla V1/V2/V3 callers that never send the field — the
        // built-in single-node CL (SingleNodeConsensus::produceBlock) and the V2/V3 cases in
        // tools/engine_integration_test.sh — so rejecting on absence would stop block
        // production on this node's own driver. Making it mandatory belongs with the
        // OP-only attributes flavour, not with this shared struct. An out-of-range value IS
        // rejected, one layer up in detail::validatePayloadAttributes — the executor narrows
        // this budget to int64.
        //
        // Consequence to carry forward: once the CL dictates the gas limit, the value no
        // longer lives on the chain's SystemConfig. The built header records it
        // (BlockHeader::gasLimit), but ledger::getLedgerConfig — where every existing caller
        // reads a block's gas budget from — still answers from SystemConfig. Any future path
        // that RE-EXECUTES a block (verification, sync, the OpScheduler rewrite in #5468)
        // must take the budget from that block's own header rather than from
        // getLedgerConfig, or the replay runs on a different budget than the build did and
        // can land on a different stateRoot. No such path exists today: newPayload only
        // caches an externally submitted payload, it never re-executes one.
        if (payloadAttributes.gasLimit.has_value())
        {
            ledgerConfig.setGasLimit({*payloadAttributes.gasLimit, nextBlockNumber});
        }

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
            // Before calculateHash: the root is a header field the block hash commits to.
            co_await fillWithdrawalsRoot(executionPayload, *emptyHeader, view);
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

        // Step 2c: Execute transactions via the scheduler, over the decoded executable
        // forms. Raw-only entries (forced transactions from the OP attributes list) have
        // no executable form yet and are skipped — see the forced-transaction comment
        // above. Materialized into a vector because scheduler implementations require a
        // sized range (a lazy filter view is not sized).
        auto executableTransactions =
            executionPayload.transactions | ::ranges::views::filter([](auto const& transaction) {
                return transaction.decoded != nullptr;
            }) |
            ::ranges::views::transform(
                [](auto const& transaction) { return transaction.decoded; }) |
            ::ranges::to<std::vector>();
        auto receipts = co_await m_scheduler.get().executeBlock(view, m_executor.get(),
            *blockHeader, executableTransactions | ::ranges::views::indirect, ledgerConfig);

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
                auto txHashes =
                    executionPayload.transactions | ::ranges::views::transform([](auto& tx) {
                        // Canonical txHash: decoded transactions expose it directly;
                        // raw-only (forced) entries hash their EIP-2718 bytes, which is
                        // the canonical hash for every raw transaction kind.
                        return tx.decoded ? tx.decoded->hash() :
                                            bcos::crypto::keccak256Hash(bcos::ref(tx.raw));
                    });
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
        // Before calculateHash: the root is a header field the block hash commits to.
        co_await fillWithdrawalsRoot(executionPayload, *blockHeader, view);
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

    /// Isthmus (OP Stack) withdrawalsRoot: the storage root of the L2ToL1MessagePasser
    /// predeploy, which is what L1 withdrawal proofs are checked against (op-geth
    /// params/protocol_params.go:31, block_validator.go:190-198). Runs after the block's
    /// transactions so a withdrawal initiated in THIS block is already in the root; @p view
    /// is the executed fork view, so committed slots and this block's writes are both seen.
    ///
    /// Writes the value into BOTH carriers, and both writes are load-bearing:
    ///  - @p executionPayload is what getPayload serves to the CL;
    ///  - @p header is what commits to the value. BlockHeaderImpl::setWithdrawalsRoot fills
    ///    the tars field the NON_ETH calculateHash() digests, so this MUST run before
    ///    calculateHash() or the root would be a field the blockHash does not commit to —
    ///    and it is also the only carrier newPayload persists (handleNewPayload writes
    ///    PayloadEntry::header, not the ExecutionPayload), so the eth_getBlockBy* answer
    ///    comes from here too.
    ///
    /// No-op for a payload whose shape has no such field (V1/V2 builds leave it nullopt),
    /// which is what keeps this one call usable on both return paths of buildPayload.
    ///
    /// Only the BUILD side is covered. handleNewPayload still accepts an externally
    /// submitted withdrawalsRoot on presence alone — validateExecutionPayload cannot
    /// re-derive it, because an external payload is never executed here — so a value this
    /// node did not build carries no guarantee, and no L1 withdrawal proof may be taken
    /// against one. Re-deriving it belongs with the block re-execution path (#5468).
    task::Task<void> fillWithdrawalsRoot(ExecutionPayload& executionPayload,
        bcos::protocol::BlockHeader& header, ViewType& view) const
    {
        if (!executionPayload.withdrawalsRoot.has_value())
        {
            co_return;
        }
        auto const root =
            co_await ledger::mpt::accountStorageRootFromFlat(view, c_l2ToL1MessagePasser);
        executionPayload.withdrawalsRoot = root;
        header.setWithdrawalsRoot(root);
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
    /// Optional ledger used to persist the ledger block tables when a locally built payload
    /// is committed via newPayload(). Null in unit tests / for payloads without block
    /// persistence.
    bcos::ledger::LedgerInterface::Ptr m_ledger;
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
