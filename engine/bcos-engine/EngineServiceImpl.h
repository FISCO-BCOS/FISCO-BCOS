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
#include "bcos-engine/PayloadId.h"
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
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/FixedBytes.h"
#include <bcos-framework/engine/DACaps.h>
#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <boost/exception/get_error_info.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/indirect.hpp>
#include <range/v3/view/transform.hpp>
#include <set>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::engine
{
// Engine exceptions live in bcos-framework/engine/Errors.h.

namespace detail
{
bcos::h256 syntheticHash(std::string_view seed);

std::vector<std::string> supportedCapabilities();

/// OP capability list. V4 get/newPayload are already in supportedCapabilities().
std::vector<std::string> supportedOpCapabilities();

bool isGetPayloadVersionCompatible(ApiVersion requestVersion, std::uint32_t payloadVersion);

std::optional<std::string> validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version);

/// OP FCU attrs: gasLimit, eip1559Params, withdrawals, minBaseFee.
std::optional<std::string> validateOpPayloadAttributes(
    const PayloadAttributes& payloadAttributes, bool jovianActive);

std::optional<std::string> validateExecutionPayload(
    const ExecutionPayload& executionPayload, std::uint32_t version);

/// Encodes the OP-Stack block-header extraData from the CL-supplied payload attributes.
bcos::bytes encodeOptimismExtraData(const PayloadAttributes& payloadAttributes);

/// Generic path: empty-trie root (geth DeriveSha([])). OP copies
/// executedHeader->withdrawalsRoot() in buildOpPayload.
inline bcos::h256 withdrawalsRootFor(const ExecutionPayload& /*payload*/)
{
    return bcos::ledger::mpt::emptyRootHash();
}

/// Compares a payload the CL submitted through newPayload against the one this node built.
/// Only extraData is compared (hash input after #5517). Transaction-list comparison is
/// deferred with the #5468 re-execution path.
std::optional<std::string> compareWithBuiltPayload(
    const ExecutionPayload& submitted, const ExecutionPayload& built);

/// Map the chain's EVM revision to the Ethereum header fork era used for RLP hashing.
bcos::protocol::EthBlockVersion ethBlockVersionFor(evmc_revision rev);

/// Fill Ethereum header fields, mark the header as Eth, and inject its RLP hash.
void finalizeEthBlockHeader(bcos::protocol::BlockHeader& header, const ExecutionPayload& payload,
    std::optional<bcos::h256> parentBeaconBlockRoot, bcos::protocol::EthBlockVersion forkVersion);

// ---- OP-mode helpers ----

/// u256 -> uint64; nullopt if out of range.
std::optional<std::uint64_t> narrowU256ToU64(const u256& value);

/// `Bloom` (std::array<byte,256>, the ExecutionPayload representation) -> `h2048` (the
/// protocol::BlockHeader::logsBloom representation).
bcos::h2048 toEthLogsBloom(const Bloom& logsBloom);

/// OP newPayload static checks (not blockHash). jovianActive selects the blobGasUsed rule.
std::optional<std::string> validateOpNewPayloadRequest(
    const NewPayloadRequest& request, bool jovianActive);

/// Set post-merge ommersHash / difficulty / nonce constants.
void applyOpHeaderConstants(bcos::protocol::BlockHeader& header);

/// Rebuild the OP header from the payload. Requires validateOpNewPayloadRequest to have passed.
bcos::protocol::BlockHeader::Ptr rebuildOpEthHeader(
    const bcos::protocol::BlockHeaderFactory::Ptr& factory, const ExecutionPayload& payload,
    const h256& transactionsRoot, const h256& parentBeaconBlockRoot);

/// Decode an OP envelope to a tars Transaction. Returns nullopt on failure (does not throw).
std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash);

/// Decode a CL attribute hex envelope. Undecodable hex is -38003, not -32603.
bcos::bytes decodeOpAttributeHex(std::string_view hex);

/// Decode `env` once (RLP + ecrecover) and pair it with the envelope. Malformed
/// input still carries a fallback tars tx so the scheduler can reject it.
EngineTransaction preparedOpTransaction(bytes env, crypto::HashType const& txHash);

/// Clone a sealed mempool tx and overwrite extraTransactionBytes with the
/// reassembled envelope. Does not mutate the pool object or re-ecrecover.
EngineTransaction preparedOpTransactionFromSealed(
    protocol::Transaction::Ptr const& sealedTx, bytes env);

/// Single carrier after #5537: OP envelopes live in `transactions[i].raw`.
inline std::vector<bytes> rawEnvelopesOf(ExecutionPayload const& payload)
{
    std::vector<bytes> out;
    out.reserve(payload.transactions.size());
    for (auto const& tx : payload.transactions)
    {
        out.push_back(tx.raw);
    }
    return out;
}

inline std::vector<EngineTransaction> transactionsFromEnvelopes(std::vector<bytes> envelopes)
{
    std::vector<EngineTransaction> out;
    out.reserve(envelopes.size());
    for (auto& raw : envelopes)
    {
        out.push_back(EngineTransaction{.raw = std::move(raw), .decoded = {}});
    }
    return out;
}

inline std::optional<h256> culpritHashOf(bcos::Error const& error)
{
    if (auto const* hash = boost::get_error_info<OpCulpritTxHash>(error))
    {
        return *hash;
    }
    return std::nullopt;
}
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

    /// True when SchedulerType has computeTxRoot (OpSchedulerSeam). Generic schedulers stay false.
    static constexpr bool c_opMode =
        requires { &SchedulerType::template computeTxRoot<std::vector<bcos::bytes>>; };

    EngineServiceImpl(MemPoolType& memPool, GlobalStateStorageType& globalStateStorage,
        ExecutorType& executor, SchedulerType& scheduler,
        bcos::protocol::BlockFactory::Ptr blockFactory,
        bcos::ledger::LedgerInterface::Ptr ledger = nullptr,
        int64_t blockTxCountLimit = bcos::engine::c_defaultBlockTxCountLimit,
        std::uint32_t maxEngineVersion = static_cast<std::uint32_t>(ApiVersion::V3),
        bcos::scheduler::SchedulerInterface::Ptr delegate = nullptr,
        std::shared_ptr<bcos::engine::DACaps> daCaps = nullptr)
      : m_memPool(std::ref(memPool)),
        m_globalStateStorage(std::ref(globalStateStorage)),
        m_blockTxCountLimit(blockTxCountLimit),
        m_executor(std::ref(executor)),
        m_scheduler(std::ref(scheduler)),
        m_blockFactory(std::move(blockFactory)),
        m_maxEngineVersion(maxEngineVersion),
        m_ledger(std::move(ledger)),
        m_delegate(std::move(delegate)),
        m_daCaps(std::move(daCaps))
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
        // c_opMode selects the capability list at compile time.
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
        if (!isForkchoiceVersionSupported(version))
        {
            BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                                  << bcos::errinfo_comment{"Unsupported Engine API version"});
        }
        if (payloadAttributes != nullptr)
        {
            // OP: FCU V1/V2 cannot carry OP attributes (gasLimit/eip1559Params/minBaseFee).
            // An attribute fault on the build path answers the -38003 InvalidPayloadAttributes
            // channel — the permanent attribute-fault signal — not the fork-era -38005.
            if constexpr (c_opMode)
            {
                if (version < 3)
                {
                    BOOST_THROW_EXCEPTION(InvalidPayloadAttributes{} << bcos::errinfo_comment{
                                              "OP payload attributes require "
                                              "engine_forkchoiceUpdatedV3; V1/V2 cannot "
                                              "carry them (JSON-RPC -38003)"});
                }
            }
            // Validate attributes before updating forkchoice. OP adds gasLimit /
            // eip1559Params / withdrawals / minBaseFee. On the OP path an attribute fault
            // answers the spec -38003 channel: the typed InvalidPayloadAttributes exception
            // propagates and the RPC endpoint maps it (EngineEndpoint.cpp). The generic path
            // keeps the pre-existing payloadStatus=INVALID answer.
            if (auto validationError =
                    detail::validatePayloadAttributes(*payloadAttributes, version);
                validationError.has_value())
            {
                if constexpr (c_opMode)
                {
                    BOOST_THROW_EXCEPTION(
                        InvalidPayloadAttributes{} << bcos::errinfo_comment{*validationError});
                }
                ForkchoiceUpdatedResult result{
                    .payloadStatus =
                        makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError),
                    .payloadId = std::nullopt,
                };
                co_return result;
            }
            if constexpr (c_opMode)
            {
                if (auto validationError = detail::validateOpPayloadAttributes(
                        *payloadAttributes, m_scheduler.get().isJovianActive());
                    validationError.has_value())
                {
                    BOOST_THROW_EXCEPTION(
                        InvalidPayloadAttributes{} << bcos::errinfo_comment{*validationError});
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

        // Update tracked head under lock. Read canonicity before taking x_state
        // (this path must not hold the mutex across co_await).
        auto canonicalHeadHash =
            co_await bcos::ledger::getBlockHash(view, *headBlockNumber, bcos::ledger::fromStorage);
        bool const headCanonical =
            canonicalHeadHash.has_value() && *canonicalHeadHash == forkchoiceState.headBlockHash;

        {
            std::unique_lock lock(x_state);
            if (m_trackedHeadBlock.has_value())
            {
                auto const& trackedHeadBlock = *m_trackedHeadBlock;
                if (*headBlockNumber < trackedHeadBlock.blockNumber)
                {
                    // Older head: answer VALID without a payloadId, uniformly. A one-level
                    // tip rebuild (canonical parent + attributes) would need to fork from
                    // the parent's committed state and unwind the merged tip, which neither
                    // the generic pipeline nor the OpScheduler delegate provides — building
                    // here would execute the sibling over the tip's already-merged state.
                    ForkchoiceUpdatedResult result{
                        .payloadStatus = makeStatus(PayloadValidationStatus::Valid,
                            forkchoiceState.headBlockHash, std::nullopt),
                        .payloadId = std::nullopt,
                    };
                    co_return result;
                }
                else if (*headBlockNumber == trackedHeadBlock.blockNumber)
                {
                    if (forkchoiceState.headBlockHash != trackedHeadBlock.hash && !headCanonical)
                    {
                        // Different hash at this height: follow if it is already canonical;
                        // otherwise -38002.
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
            // Build from payload attributes; FCU checks above already ran.
            co_return co_await buildOpPayload(
                forkchoiceState, *payloadAttributes, version, *headBlockNumber + 1);
        }
        else
        {
            // Seal from the committed nonce; execution advances nonce, not seal().
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

            // Payload ID is derived from attributes + parent hash, not a counter.
            auto payloadIdOpt = derivePayloadId(*payloadAttributes, forkchoiceState.headBlockHash,
                static_cast<std::uint32_t>(version));
            if (!payloadIdOpt.has_value())
            {
                ForkchoiceUpdatedResult result{
                    .payloadStatus = makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                        std::string("payloadAttributes.transactions contains undecodable hex")),
                    .payloadId = std::nullopt,
                };
                co_return result;
            }
            auto payloadId = *payloadIdOpt;
            auto nextBlockNumber = *headBlockNumber + 1;
            auto built = co_await buildPayload(forkchoiceState, *payloadAttributes, payloadId,
                version, nextBlockNumber, std::move(sealedTxs), view);
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
                publishPayloadEntry(payloadId, std::move(entry));
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
        /// Engine API version of the call that last wrote this entry: the forkchoiceUpdated
        /// version for a build, the newPayload version for a commit. getPayload's version
        /// window (detail::isGetPayloadVersionCompatible) is checked against it. Both commit
        /// paths rewrite the version at commit time (generic: the newPayload version; OP
        /// local-commit: 4, the only version the OP newPayload gate admits), so a
        /// post-commit re-query through getPayloadV4/V5 fails the window and answers -38005
        /// instead of replaying a committed payload. op-node never re-queries a committed
        /// payloadId (it fetches a build exactly once); the rewrite only closes the
        /// pre/post-commit sentinel fold for other clients.
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

    /// Insert or rebuild a payload cache entry: dedupe the order-deque position, append
    /// the ID, assign the cache slot, and bound both maps at c_maxPayloadEntries by
    /// front-eviction. Rebuilds of an existing ID move to the back so a refreshed entry
    /// cannot sit in a stale front slot and get evicted right away. Caller must have set
    /// m_blockHashToPayloadId already and hold x_state exclusively.
    /// Private helper: dedupe the order-deque position, append the ID, assign the
    /// cache slot, and bound both maps at c_maxPayloadEntries by front-eviction.
    void publishPayloadEntry(const PayloadID& payloadId, PayloadEntry&& entry)
    {
        auto orderIt = std::find(m_payloadOrder.begin(), m_payloadOrder.end(), payloadId);
        if (orderIt != m_payloadOrder.end())
        {
            m_payloadOrder.erase(orderIt);
        }
        m_payloadOrder.push_back(payloadId);
        m_payloadCache[payloadId] = std::move(entry);
        while (m_payloadOrder.size() > c_maxPayloadEntries)
        {
            auto const evictedId = m_payloadOrder.front();
            m_payloadOrder.pop_front();
            m_payloadCache.erase(evictedId);
            std::erase_if(
                m_blockHashToPayloadId, [&](auto const& kv) { return kv.second == evictedId; });
        }
    }


    /// Upper bound is per-instance (OP sets V4 at construction; default V3).
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

    /// OpConsensusRejected -> INVALID; other scheduler errors -> -32603.
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

    /// Build an OP payload: pre-execute with verify=false, then fill commitments and blockHash.
    bcos::task::Task<ForkchoiceUpdatedResult> buildOpPayload(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, std::uint32_t version,
        bcos::protocol::BlockNumber nextBlockNumber)
    {
        // Ledger config / parent header are storage reads. They are performed BEFORE
        // x_opExecute is taken so the lock is held once and covers the whole
        // reset -> probe(execute) -> adopt sequence: releasing the lock across the co_awaits
        // would let a concurrent buildOpPayload run its own probe+adopt passes (adopt stashes
        // the delegate's m_pending) and overwrite the shared delegate's pending block while
        // this build is suspended.
        ledger::LedgerConfig ledgerConfig;
        u256 baseFee;
        {
            auto view = m_globalStateStorage.get().fork();
            co_await ledger::getLedgerConfig(
                view, ledgerConfig, nextBlockNumber - 1, *m_blockFactory);
            auto parentNumberStr = boost::lexical_cast<std::string>(nextBlockNumber - 1);
            if (auto parentHeaderEntry = co_await storage2::readOne(view,
                    executor_v1::StateKeyView{ledger::SYS_NUMBER_2_BLOCK_HEADER, parentNumberStr});
                parentHeaderEntry.has_value())
            {
                auto stored = parentHeaderEntry->get();
                bcos::bytes parentHeaderBytes(stored.begin(), stored.end());
                auto parentHeader =
                    m_blockFactory->blockHeaderFactory()->createBlockHeader(parentHeaderBytes);
                try
                {
                    baseFee = calcOpBaseFee(*parentHeader, m_scheduler.get().isJovianActive());
                }
                catch (std::exception const& e)
                {
                    BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                              std::string("calcOpBaseFee failed: ") + e.what()});
                }
                // FISCO's own guard: attrs.timestamp <= parent is an attribute fault, so this
                // gate throws InvalidPayloadAttributes, which EngineEndpoint maps to the spec
                // -38003 channel (a permanent attribute fault for the CL, not a retryable
                // -32603). Without this gate we would build a payload whose read-back is
                // rejected at newPayload — a one-block stall.
                if (payloadAttributes.timestamp <=
                    static_cast<std::uint64_t>(parentHeader->timestamp()))
                {
                    // A timestamp fault is an attribute fault (spec -38003), not an internal
                    // error.
                    BOOST_THROW_EXCEPTION(InvalidPayloadAttributes{} << bcos::errinfo_comment{
                                              "buildOpPayload: payloadAttributes.timestamp must be "
                                              "strictly greater than the parent header timestamp"});
                }
            }
            else
            {
                BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                          "buildOpPayload: parent block header missing at height " +
                                          parentNumberStr});
            }
        }

        // Execute and commit through the OpScheduler delegate: same guard as
        // runOpNewPayloadSteps so a delegate-less OP engine fails cleanly instead of
        // dereferencing null at the first reset().
        // Wiring contract: with c_opMode active the runtime m_delegate MUST be an OpScheduler —
        // the only SchedulerInterface overriding adoptProbeAsPending (this path adopts the
        // retained probe below). Any other delegate silently falls back to the interface's
        // defaulted verify=true re-execution: correct, but it loses the single-execution
        // optimization. No RTTI dispatch is possible (OpScheduler is templated), so this is a
        // documented convention, not an enforced invariant.
        if (!m_delegate)
        {
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    "OP payload build requires an m_delegate (OpScheduler) for block execution"});
        }

        // Serialize OP reset/execute/commit on the shared delegate. Cache maps stay
        // under x_state only for the publish window (not across EVM time).
        std::unique_lock opLock(x_opExecute);

        // Leading L1-attributes deposit, then attrs.transactions, then sealed pool txs.
        // Decode each envelope ONCE here; buildOpBlock reuses EngineTransaction.decoded.
        // Synthesize the deposit only when the CL did not supply one.
        std::vector<EngineTransaction> forcedTxs;
        std::vector<h256> attrTxHashes;
        if (!payloadAttributes.transactions.has_value() || payloadAttributes.transactions->empty())
        {
            // The seam parameter is Engine-API seconds (payloadAttributes.timestamp is
            // FISCO-internal milliseconds, same convention PayloadId.h divides by 1000).
            auto raw = m_scheduler.get().synthesizeL1AttributesEnvelope(
                payloadAttributes.timestamp / 1000);
            auto const hash = bcos::crypto::keccak256Hash(bcos::ref(raw));
            forcedTxs.push_back(detail::preparedOpTransaction(std::move(raw), hash));
        }
        if (payloadAttributes.transactions.has_value())
        {
            forcedTxs.reserve(forcedTxs.size() + payloadAttributes.transactions->size());
            attrTxHashes.reserve(payloadAttributes.transactions->size());
            for (auto const& forcedHex : *payloadAttributes.transactions)
            {
                auto raw = detail::decodeOpAttributeHex(forcedHex);
                auto const hash = bcos::crypto::keccak256Hash(bcos::ref(raw));
                attrTxHashes.push_back(hash);
                forcedTxs.push_back(detail::preparedOpTransaction(std::move(raw), hash));
            }
        }
        // Payload ID from CL attributes only (not pool txs or the synthesized deposit).
        auto payloadId = bcos::engine::derivePayloadId(payloadAttributes,
            forkchoiceState.headBlockHash, attrTxHashes, static_cast<uint8_t>(version));
        // Seal over a throwaway view; the delegate owns the execution view.
        auto sealView = m_globalStateStorage.get().fork();
        std::vector<protocol::Transaction::Ptr> sealedTxs;
        if (!payloadAttributes.noTxPool.value_or(false))
        {
            sealView.newMutable();
            m_memPool.get().remove(sealView);
            m_memPool.get().seal(m_blockTxCountLimit, sealView, std::back_inserter(sealedTxs));
            // DA throttle is applied later, when envelope sizes are known.
        }

        struct PreparedSealedTx
        {
            crypto::HashType hash;
            EngineTransaction tx;
        };
        std::vector<PreparedSealedTx> sealedPrepared;
        for (auto& sealedTx : sealedTxs)
        {
            if (sealedTx->type() !=
                static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
            {
                BCOS_LOG(WARNING)
                    << LOG_BADGE("EngineService")
                    << LOG_DESC(
                           "buildOpPayload: excluding transaction without an EIP-2718 "
                           "wire form");
                continue;
            }
            auto env = bcostars::protocol::reassembleWeb3RawTransaction(
                sealedTx->extraTransactionBytes(), sealedTx->signatureData());
            auto const hash = sealedTx->hash();
            sealedPrepared.push_back(PreparedSealedTx{
                .hash = hash,
                .tx = detail::preparedOpTransactionFromSealed(std::move(sealedTx), std::move(env)),
            });
        }
        // Drop sealed envelopes over maxTxSize (0 = uncapped). They stay in the pool.
        if (m_daCaps)
        {
            auto over = [this](PreparedSealedTx const& entry) {
                return !m_daCaps->txFits(entry.tx.raw.size());
            };
            auto it = std::remove_if(sealedPrepared.begin(), sealedPrepared.end(), over);
            if (it != sealedPrepared.end())
            {
                BCOS_LOG(INFO) << LOG_BADGE("EngineService")
                               << LOG_DESC(
                                      "buildOpPayload: DA throttle dropped sealed txs "
                                      "over maxTxSize")
                               << LOG_KV("dropped", std::distance(it, sealedPrepared.end()))
                               << LOG_KV("maxTxSize",
                                      m_daCaps->maxTxSize.load(std::memory_order_relaxed));
                sealedPrepared.erase(it, sealedPrepared.end());
            }
        }

        auto const parentBeaconBlockRoot =
            payloadAttributes.parentBeaconBlockRoot.value_or(crypto::HashType{});
        std::uint64_t forcedBytes = 0;
        for (auto const& tx : forcedTxs)
        {
            forcedBytes += tx.raw.size();
        }
        auto const forcedN = forcedTxs.size();

        ExecutionPayload payload{
            .logsBloom = Bloom{},
            .parentHash = forkchoiceState.headBlockHash,
            .stateRoot = h256{},
            .receiptsRoot = h256{},
            .prevRandao = payloadAttributes.prevRandao,
            .gasLimit = payloadAttributes.gasLimit.has_value() ?
                            u256(*payloadAttributes.gasLimit) :
                            u256(std::get<0>(ledgerConfig.gasLimit())),
            .gasUsed = 0,
            .baseFeePerGas = baseFee,
            .blockHash = h256{},
            .transactions = std::move(forcedTxs),
            .extraData = detail::encodeOptimismExtraData(payloadAttributes),
            .feeRecipient = payloadAttributes.suggestedFeeRecipient,
            .timestamp = payloadAttributes.timestamp,
            .blockNumber = nextBlockNumber,
            .withdrawals = std::vector<WithdrawalV1>{},
            .blobGasUsed = u256(0),
            .excessBlobGas = u256(0),
            .blockAccessList = std::nullopt,
            .slotNumber = std::nullopt,
            .withdrawalsRoot = h256{},
        };

        // Probe: evict a sealed tx that fails validation and retry. Forced/deposit failures abort.
        // Forced txs stay in payload.transactions[0, forcedN); each retry only rebuilds the
        // sealed tail (R83) and reuses already-decoded Ptrs (R77).
        // I11-B: with no sealed pool txs there is nothing to evict; the single probe is then
        // adopted (see the adopt call below). The eviction branch below is unreachable when
        // sealedPrepared is empty — no pool guard is needed.
        std::size_t evicted = 0;
        bcos::protocol::BlockHeader::Ptr executedHeader;
        static constexpr std::size_t c_maxEvictionRetries = 16;
        while (evicted <= c_maxEvictionRetries)
        {
            payload.transactions.resize(forcedN);
            std::optional<bcos::engine::DACaps::Budget> budget;
            if (m_daCaps)
            {
                budget.emplace(*m_daCaps, forcedBytes);
            }
            for (auto const& sealed : sealedPrepared)
            {
                if (budget && !budget->admits(sealed.tx.raw.size()))
                {
                    break;  // pool-order prefix stop (seal order, not size order)
                }
                payload.transactions.push_back(sealed.tx);
            }

            const auto transactionsRoot =
                SchedulerType::computeTxRoot(detail::rawEnvelopesOf(payload));
            auto provisionalHeader =
                detail::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(), payload,
                    transactionsRoot, parentBeaconBlockRoot);
            auto block = buildOpBlock(payload, provisionalHeader);

            m_delegate->reset([](bcos::Error::Ptr) {});
            bcos::Error::Ptr executeError;
            m_delegate->executeBlock(block, /*verify=*/false,
                [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, bool) {
                    executeError = std::move(error);
                    executedHeader = std::move(header);
                });
            if (!executeError && executedHeader)
            {
                break;
            }
            auto const message =
                executeError ? executeError->errorMessage() : std::string("no executed header");
            auto culprit = executeError ? detail::culpritHashOf(*executeError) : std::nullopt;
            auto sealedIt = culprit.has_value() ?
                                std::find_if(sealedPrepared.begin(), sealedPrepared.end(),
                                    [&culprit](PreparedSealedTx const& entry) {
                                        return entry.hash == *culprit;
                                    }) :
                                sealedPrepared.end();
            // Unreachable when sealedPrepared is empty (find_if over an empty range returns
            // end()); there is then nothing to evict and the probe below is adopted as-is.
            if (sealedIt != sealedPrepared.end())
            {
                // The candidate was built under the CL-supplied gasLimit. A gasLimit below
                // the chain's own is a CL configuration fault, not a poisoned transaction —
                // evicting on it would let one FCU drain the pool (censorship). Re-probe the
                // same envelope set under the ledger gasLimit; only a transaction that fails
                // there is poison.
                if (payloadAttributes.gasLimit.has_value() &&
                    *payloadAttributes.gasLimit != std::get<0>(ledgerConfig.gasLimit()))
                {
                    auto const savedGasLimit = payload.gasLimit;
                    payload.gasLimit = u256(std::get<0>(ledgerConfig.gasLimit()));
                    bcos::Error::Ptr ledgerGasError;
                    bcos::protocol::BlockHeader::Ptr ledgerGasExecutedHeader;
                    try
                    {
                        auto ledgerGasHeader = detail::rebuildOpEthHeader(
                            m_blockFactory->blockHeaderFactory(), payload,
                            SchedulerType::computeTxRoot(detail::rawEnvelopesOf(payload)),
                            parentBeaconBlockRoot);
                        auto ledgerGasBlock = buildOpBlock(payload, ledgerGasHeader);
                        m_delegate->reset([](bcos::Error::Ptr) {});
                        m_delegate->executeBlock(ledgerGasBlock, /*verify=*/false,
                            [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr header,
                                bool) {
                                ledgerGasError = std::move(error);
                                ledgerGasExecutedHeader = std::move(header);
                            });
                    }
                    catch (...)
                    {
                        payload.gasLimit = savedGasLimit;
                        throw;
                    }
                    payload.gasLimit = savedGasLimit;
                    if (!ledgerGasError && ledgerGasExecutedHeader)
                    {
                        BOOST_THROW_EXCEPTION(
                            OpExecutionInternalError{} << bcos::errinfo_comment{
                                "OP payload build failed under the CL-supplied gasLimit, while the "
                                "chain gasLimit executes the same transactions: the CL gasLimit is "
                                "below the chain's own"});
                    }
                }
                std::array<crypto::HashType, 1> hashSpan{sealedIt->hash};
                m_memPool.get().remove(std::span<crypto::HashType const>(hashSpan));
                BCOS_LOG(WARNING)
                    << LOG_BADGE("EngineService")
                    << LOG_DESC("buildOpPayload: evicted poisoned pool transaction, retrying")
                    << LOG_KV("tx", sealedIt->hash.hexPrefixed()) << LOG_KV("reason", message);
                sealedPrepared.erase(sealedIt);
                ++evicted;
                continue;
            }
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    std::string("OP payload build execution failed: ") + message});
        }
        if (!executedHeader)
        {
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    "OP payload build: eviction retry limit reached without a valid probe"});
        }

        // Fill commitments and the final OP blockHash.
        payload.stateRoot = executedHeader->stateRoot();
        payload.receiptsRoot = executedHeader->receiptsRoot();
        payload.gasUsed = u256(executedHeader->gasUsed());
        {
            auto executedBloom = executedHeader->logsBloom();
            std::copy(executedBloom.begin(), executedBloom.end(), payload.logsBloom.begin());
        }
        payload.withdrawalsRoot = executedHeader->withdrawalsRoot();
        // Jovian: blobGasUsed is the DA footprint from the probe execution.
        if (auto executedBlobGas = executedHeader->blobGasUsed())
        {
            payload.blobGasUsed = *executedBlobGas;
        }
        auto finalHeader = detail::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(), payload,
            SchedulerType::computeTxRoot(detail::rawEnvelopesOf(payload)), parentBeaconBlockRoot);
        payload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*finalHeader);

        // Adopt the retained probe instead of a canonical re-execution: the probe already
        // produced the commitments
        // (stateRoot/receiptsRoot/gasUsed/logsBloom/withdrawalsRoot/blobGasUsed)
        // that were filled into `payload` above, and the final header is rebuilt from them.
        // adoptProbeAsPending verifies that header against the probe and stashes m_pending.
        auto finalBlock = buildOpBlock(payload, finalHeader);
        bcos::Error::Ptr adoptError;
        bcos::protocol::BlockHeader::Ptr adoptedHeader;
        m_delegate->adoptProbeAsPending(
            finalBlock, [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, bool) {
                adoptError = std::move(error);
                adoptedHeader = std::move(header);
            });
        if (adoptError || !adoptedHeader)
        {
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    std::string("OP payload build adopt failed: ") +
                    (adoptError ? adoptError->errorMessage() : "no executed header")});
        }

        // payloadId was derived at the top of buildOpPayload from the attributes;
        // it is in scope here (the adopt does not change the attributes).
        PayloadEntry entry{
            .version = version,
            .executionPayload = std::move(payload),
            .blockValue = 0,
            .blobsBundle = std::nullopt,
            .shouldOverrideBuilder = false,
            .parentBeaconBlockRoot = parentBeaconBlockRoot,
            .view = nullptr,
            .header = std::move(adoptedHeader),
            .receipts = {},
        };
        if (version == static_cast<std::uint32_t>(ApiVersion::V3))
        {
            entry.blobsBundle = BlobsBundleV1{};
        }
        {
            std::unique_lock stateLock(x_state);
            m_blockHashToPayloadId[entry.executionPayload.blockHash] = payloadId;
            publishPayloadEntry(payloadId, std::move(entry));
        }
        co_return ForkchoiceUpdatedResult{
            .payloadStatus = makeStatus(
                PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
            .payloadId = payloadId,
        };
    }

    GetPayloadResult handleGetPayload(const PayloadID& payloadId, std::uint32_t version) const
    {
        if (!isGetPayloadVersionSupported(version))
        {
            BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                                  << bcos::errinfo_comment{"Unsupported Engine API version"});
        }
        {
            // OP and generic paths share the payload cache.
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
            // A V3 commit has no withdrawalsRoot; do not serve it as a V4/V5 response.
            if (version >= static_cast<std::uint32_t>(ApiVersion::V4) &&
                !it->second.executionPayload.withdrawalsRoot.has_value())
            {
                BOOST_THROW_EXCEPTION(IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                                          "Payload does not carry the V4+ response shape"});
            }

            auto executionPayload = it->second.executionPayload;
            auto blockValue = it->second.blockValue;
            auto blobsBundle = it->second.blobsBundle;
            auto shouldOverrideBuilder = it->second.shouldOverrideBuilder;
            auto parentBeaconBlockRoot = it->second.parentBeaconBlockRoot;
            lock.unlock();
            return std::make_unique<GetPayloadData>(GetPayloadData{
                .executionPayload = std::move(executionPayload),
                .blockValue = std::move(blockValue),
                .blobsBundle = std::move(blobsBundle),
                .shouldOverrideBuilder = shouldOverrideBuilder,
                // V4/V5: present-but-empty executionRequests.
                .executionRequests = version >= static_cast<std::uint32_t>(ApiVersion::V4) ?
                                         std::optional<std::vector<bytes>>{std::in_place} :
                                         std::nullopt,
                .parentBeaconBlockRoot = parentBeaconBlockRoot,
            });
        }
    }

    bcos::task::Task<PayloadStatus> handleNewPayload(
        const NewPayloadRequest& request, std::uint32_t version)
    {
        if (!isNewPayloadVersionSupported(version))
        {
            BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                                  << bcos::errinfo_comment{"Unsupported Engine API version"});
        }
        // c_opMode picks the OP path at compile time; the else branch is the generic engine.
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
            // L2 forbids blob txs: expectedBlobVersionedHashes must be empty from V3.
            if (version >= 3 && !request.expectedBlobVersionedHashes.empty())
            {
                co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                    std::string("expectedBlobVersionedHashes must be empty (L2 forbids blob "
                                "transactions)"));
            }
            // V4: executionRequests must be present and empty.
            if (version >= 4)
            {
                if (!request.executionRequests.has_value() || !request.executionRequests->empty())
                {
                    co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                        std::string("executionRequests must be present and empty (L2 carries no "
                                    "execution requests)"));
                }
            }
            // parentBeaconBlockRoot is required from V3.
            if (version >= 3 && !request.parentBeaconBlockRoot.has_value())
            {
                co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                    std::string("parentBeaconBlockRoot must be a 32-byte hash for newPayloadV3 and "
                                "later"));
            }

            std::shared_ptr<ViewType> viewToCommit;
            bcos::protocol::BlockHeader::Ptr builtHeader;
            std::vector<protocol::TransactionReceipt::Ptr> builtReceipts;
            ExecutionPayload builtPayload;
            PayloadID payloadId;
            {
                std::unique_lock lock(x_state);
                auto parentKnown =
                    request.executionPayload.parentHash == m_forkchoiceState.headBlockHash ||
                    m_blockHashToPayloadId.contains(request.executionPayload.parentHash);
                if (!parentKnown)
                {
                    co_return makeStatus(
                        PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
                }

                auto payloadIdIt = m_blockHashToPayloadId.find(request.executionPayload.blockHash);
                if (payloadIdIt == m_blockHashToPayloadId.end())
                {
                    // External payload: cache key is the first 8 bytes of keccak256(blockHash).
                    auto hash =
                        bcos::crypto::keccak256Hash(request.executionPayload.blockHash.ref());
                    payloadId = bcos::toHex(hash.ref().getCroppedData(0, 8), "0x");
                    m_blockHashToPayloadId.emplace(request.executionPayload.blockHash, payloadId);
                }
                else
                {
                    payloadId = payloadIdIt->second;
                    if (auto builtIt = m_payloadCache.find(payloadId);
                        builtIt != m_payloadCache.end())
                    {
                        if (auto mismatch = detail::compareWithBuiltPayload(
                                request.executionPayload, builtIt->second.executionPayload))
                        {
                            co_return makeStatus(
                                PayloadValidationStatus::InvalidBlockHash, std::nullopt, mismatch);
                        }
                    }
                }

                if (auto it = m_payloadCache.find(payloadId);
                    it != m_payloadCache.end() && it->second.view)
                {
                    viewToCommit = std::move(it->second.view);
                    builtHeader = it->second.header;
                    builtReceipts = it->second.receipts;
                    builtPayload = it->second.executionPayload;
                }
            }

            if (viewToCommit)
            {
                m_globalStateStorage.get().pushView(std::move(*viewToCommit));
                bool merged = false;
                struct PushViewRollback
                {
                    GlobalStateStorageType& storage;
                    bool& merged;
                    ~PushViewRollback()
                    {
                        if (!merged)
                        {
                            storage.popFrontStorage();
                        }
                    }
                } rollback{m_globalStateStorage.get(), merged};
                if (m_ledger && builtHeader)
                {
                    typename GlobalStateStorageType::MutableStorage prewriteStorage;
                    auto block = m_blockFactory->createBlock();
                    block->setBlockHeader(builtHeader);
                    auto const& bloom = builtPayload.logsBloom;
                    block->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
                    for (auto const& tx : builtPayload.transactions)
                    {
                        if (tx.decoded)
                        {
                            block->appendTransaction(tx.decoded);
                        }
                    }
                    for (auto const& receipt : builtReceipts)
                    {
                        block->appendReceipt(receipt);
                    }
                    auto blockTxs = std::make_shared<protocol::ConstTransactions>(
                        builtPayload.transactions | ::ranges::views::filter([](auto const& tx) {
                            return tx.decoded != nullptr;
                        }) |
                        ::ranges::views::transform([](auto const& tx) {
                            return protocol::Transaction::ConstPtr(tx.decoded);
                        }) |
                        ::ranges::to<std::vector>());
                    co_await ledger::prewriteBlockToBuffer(
                        *m_ledger, blockTxs, block, prewriteStorage);
                    co_await m_globalStateStorage.get().mergeBackStorage(prewriteStorage);
                }
                else
                {
                    co_await m_globalStateStorage.get().mergeBackStorage();
                }
                merged = true;
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
            {
                std::unique_lock lock(x_state);
                m_payloadCache[payloadId] = std::move(entry);
                // Keep only the just-committed block; the newPayload parent check accepts
                // the head hash directly, so dropping older blockHash rows is safe. Prune
                // the order deque to the same set so no ghost IDs linger and skew the
                // eviction loop.
                std::erase_if(m_blockHashToPayloadId,
                    [&](auto const& kv) { return kv.first != request.executionPayload.blockHash; });
                std::erase_if(
                    m_payloadCache, [&](auto const& kv) { return kv.first != payloadId; });
                std::erase_if(
                    m_payloadOrder, [&](auto const& orderId) { return orderId != payloadId; });
            }

            co_return makeStatus(
                PayloadValidationStatus::Valid, request.executionPayload.blockHash, std::nullopt);
        }
    }
    /// OP newPayload: V4 gate, then runOpNewPayloadSteps. Unclassified exceptions become -32603.
    bcos::task::Task<PayloadStatus> handleOpNewPayload(
        const NewPayloadRequest& request, std::uint32_t version)
    {
        // Isthmus+ requires newPayloadV4 (-38005). Thrown before the try so it is not rewrapped.
        constexpr std::uint32_t c_opIsthmusPayloadVersion = 4;
        if (version != c_opIsthmusPayloadVersion)
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "Isthmus+ payloads require engine_newPayloadV4 (JSON-RPC -38005)"});
        }

        // Version gate stays outside this try. Already-classified errors rethrow.
        try
        {
            co_return co_await runOpNewPayloadSteps(request);
        }
        catch (const OpExecutionInternalError&)
        {
            // Already classified.
            throw;
        }
        catch (...)
        {
            // Unclassified failure: -32603, not INVALID.
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    "OP newPayload threw an unclassified exception outside block execution "
                    "(validation, comparison or registration phase)"});
        }
    }

    /// OP newPayload body. Instantiated only when c_opMode is true.
    bcos::task::Task<PayloadStatus> runOpNewPayloadSteps(const NewPayloadRequest& request)
    {
        auto const& payload = request.executionPayload;

        // Static checks + blockHash. Failures are INVALID with latestValidHash = null.
        if (auto validationError =
                detail::validateOpNewPayloadRequest(request, m_scheduler.get().isJovianActive());
            validationError.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError);
        }
        // transactionsRoot is not in the payload; derive it from the raw envelopes.
        const auto transactionsRoot = SchedulerType::computeTxRoot(detail::rawEnvelopesOf(payload));
        const auto ethHeader = detail::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(),
            payload, transactionsRoot, *request.parentBeaconBlockRoot);
        // Compare against keccak(RLP(header)), not BlockHeader::hash() (tars hasher).
        if (bcos::protocol::EthBlockHeader::computeHash(*ethHeader) != payload.blockHash)
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string("blockHash does not match the reconstructed block header"));
        }

        // Locally built payload: commit the pending block without re-execution.
        bcos::protocol::BlockHeader::Ptr builtHeader;
        PayloadID builtPayloadId;
        {
            std::shared_lock lock(x_state);
            auto it = m_blockHashToPayloadId.find(payload.blockHash);
            if (it != m_blockHashToPayloadId.end())
            {
                auto payloadId = it->second;
                if (auto entry = m_payloadCache.find(payloadId); entry != m_payloadCache.end())
                {
                    // Deterministic payload IDs are derived from attributes only: a stale
                    // retry of an older build can carry a payloadId whose cache entry was
                    // replaced by a newer build of the same attributes. Only the cached
                    // build whose blockHash equals the submitted payload may be committed
                    // without re-execution; anything else falls through to the full
                    // execution path below.
                    if (entry->second.executionPayload.blockHash == payload.blockHash)
                    {
                        builtHeader = entry->second.header;
                        builtPayloadId = payloadId;
                    }
                }
            }
        }
        if (builtHeader)
        {
            // Cached payload whose pending is gone: VALID if already in the ledger.
            auto knownView = m_globalStateStorage.get().fork();
            if (auto known = co_await bcos::ledger::getBlockNumber(
                    knownView, payload.blockHash, bcos::ledger::fromStorage);
                known.has_value())
            {
                co_return makeStatus(
                    PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
            }
            bcos::Error::Ptr commitError;
            std::unique_lock opLock(x_opExecute);
            m_delegate->commitBlock(
                builtHeader, [&](bcos::Error::Ptr error, bcos::ledger::LedgerConfig::Ptr) {
                    commitError = std::move(error);
                });
            if (commitError)
            {
                co_return mapDelegateError(*commitError, std::nullopt);
            }
            {
                // Align with the generic commit path: rewrite the entry's version so a
                // post-commit re-query through getPayloadV4/V5 fails the window (-38005)
                // instead of replaying a committed payload. 4 is the only version the OP
                // newPayload gate admits.
                std::unique_lock stateLock(x_state);
                if (auto entry = m_payloadCache.find(builtPayloadId); entry != m_payloadCache.end())
                {
                    entry->second.version = 4;
                }
            }
            co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
        }

        // parentKnown from SYS_HASH_2_NUMBER (not the in-memory cache).
        // Do not hold x_opExecute across these storage co_awaits.
        auto view = m_globalStateStorage.get().fork();
        auto parentBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, payload.parentHash, bcos::ledger::fromStorage);
        if (!parentBlockNumber.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
        // Parent is persisted; later INVALID reports latestValidHash = parentHash.
        const auto latestValidHash = std::make_optional(payload.parentHash);

        // Child height must be parent + 1 (index tables are keyed by number).
        if (payload.blockNumber != *parentBlockNumber + 1)
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("blockNumber must be exactly one greater than the parent's"));
        }

        // Parent must still be the canonical block at its height; otherwise SYNCING.
        if (auto canonicalParent = co_await bcos::ledger::getBlockHash(
                view, *parentBlockNumber, bcos::ledger::fromStorage);
            !canonicalParent.has_value() || *canonicalParent != payload.parentHash)
        {
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }

        // Timestamp / baseFee vs the stored parent header. A missing row at a resolved
        // parent height is a local storage fault (throw), not a first-block skip: the
        // parent number was already looked up above, so genesis has a header row.
        const auto parentNumberStr = boost::lexical_cast<std::string>(*parentBlockNumber);
        // Parent header from SYS_NUMBER_2_BLOCK_HEADER (copy string_view into bytes).
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
                // Stored parent header is undecodable: local storage fault.
                BOOST_THROW_EXCEPTION(
                    OpExecutionInternalError{} << bcos::errinfo_comment{
                        std::string("stored parent block header is undecodable: ") + e.what()});
            }
            // Height mismatch after tars decode is a local storage fault.
            if (parentHeader->number() != static_cast<int64_t>(*parentBlockNumber))
            {
                BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                          "stored parent block header height mismatch"});
            }
            // Timestamp is milliseconds on both sides. Do not use header->hash() here (tars
            // hasher).
            if (static_cast<uint64_t>(payload.timestamp) <=
                static_cast<uint64_t>(parentHeader->timestamp()))
            {
                co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                    std::string("timestamp must be strictly greater than the parent's"));
            }

            // baseFee must match calcOpBaseFee(parent).
            {
                try
                {
                    auto expectedBaseFee =
                        calcOpBaseFee(*parentHeader, m_scheduler.get().isJovianActive());
                    if (payload.baseFeePerGas != expectedBaseFee)
                    {
                        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                            std::string("baseFeePerGas does not match the value computed "
                                        "from the parent"));
                    }
                }
                catch (std::exception const& e)
                {
                    BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                              std::string("calcOpBaseFee failed: ") + e.what()});
                }
            }
        }
        else
        {
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    "runOpNewPayloadSteps: parent block header missing at height " +
                    parentNumberStr});
        }

        // Already-known block: VALID without re-execute (after static checks, before
        // occupied-height).
        if (auto knownBlockNumber = co_await bcos::ledger::getBlockNumber(
                view, payload.blockHash, bcos::ledger::fromStorage);
            knownBlockNumber.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
        }

        // Occupied height: OpScheduler has no ReorgUndo. Fail-closed like FCU's
        // !c_opMode one-level rebuild gate — do not admit a tip sibling that the
        // delegate would reject with a retryable InvalidBlockNumber.
        const auto childNumberStr = boost::lexical_cast<std::string>(payload.blockNumber);
        if (auto occupiedHeight = co_await storage2::readOne(
                view, executor_v1::StateKeyView{ledger::SYS_NUMBER_2_HASH, childNumberStr});
            occupiedHeight.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("a different block is already registered at this height"));
        }

        // Execute and commit through the OpScheduler delegate.
        if (!m_delegate)
        {
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    "OP newPayload requires an m_delegate (OpScheduler) for block execution"});
        }

        // extraTransactionBytes is the full envelope, not the signing preimage.
        auto block = buildOpBlock(payload, ethHeader);

        // Serialize delegate execute/commit only. Storage checks above must not hold
        // this mutex across co_await.
        std::unique_lock opLock(x_opExecute);

        // OpScheduler::executeBlock/commitBlock use task::syncWait; the callback
        // has published before these calls return.
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
        if (!executedHeader)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "OP newPayload: executeBlock returned no header"});
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

        // Delegate commit already persisted ledger tables and merged state.
        co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
    }

    /// Build a protocol::Block whose txs carry the payload's full EIP-2718 envelopes.
    /// extraTransactionBytes must be the sealed envelope, not the signing preimage. Used only
    /// by the delegate path.
    [[maybe_unused]] bcos::protocol::Block::Ptr buildOpBlock(
        const ExecutionPayload& payload, bcos::protocol::BlockHeader::Ptr header)
    {
        auto block = m_blockFactory->createBlock();
        block->setBlockHeader(std::move(header));
        auto& hashImpl = *m_blockFactory->cryptoSuite()->hashImpl();
        for (auto const& engineTx : payload.transactions)
        {
            if (engineTx.decoded)
            {
                block->appendTransaction(engineTx.decoded);
                continue;
            }
            // Raw-only carriers (newPayload, and any other carrier built without a decode)
            // decode here. The buildOpPayload probe and adopt passes never reach this fallback:
            // their forced and sealed envelopes are decoded ONCE into the carriers before the
            // probe loop, and the probe, ledger-gas re-probe, and adopt buildOpBlock passes all
            // reuse those already-decoded .decoded forms — OP decode-once is structural here
            // (R77), not a decode cache.
            auto const& env = engineTx.raw;
            const auto txHash = hashImpl.hash(env);
            auto prepared = detail::preparedOpTransaction(env, txHash);
            block->appendTransaction(std::move(prepared.decoded));
        }
        return block;
    }


    /// Payload ID from CL attribute txs only. Nullopt if an attribute tx hex is invalid.
    static std::optional<PayloadID> derivePayloadId(
        PayloadAttributes const& payloadAttributes, h256 const& parentHash, std::uint32_t version)
    {
        std::vector<h256> txHashes;
        if (payloadAttributes.transactions.has_value())
        {
            txHashes.reserve(payloadAttributes.transactions->size());
            for (auto const& hexTx : *payloadAttributes.transactions)
            {
                try
                {
                    auto raw = bcos::fromHex(hexTx);
                    txHashes.emplace_back(bcos::crypto::keccak256Hash(bcos::ref(raw)));
                }
                catch (bcos::BadHexCharacter const&)
                {
                    return std::nullopt;
                }
            }
        }
        return bcos::engine::derivePayloadId(
            payloadAttributes, parentHash, txHashes, static_cast<uint8_t>(version));
    }

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
            .extraData = detail::encodeOptimismExtraData(payloadAttributes),
            .feeRecipient = payloadAttributes.suggestedFeeRecipient,
            .timestamp = payloadAttributes.timestamp,
            .blockNumber = nextBlockNumber,
            .withdrawals = std::nullopt,
            .blobGasUsed = std::nullopt,
            .excessBlobGas = std::nullopt,
            .blockAccessList = std::nullopt,
            .slotNumber = std::nullopt,
            .withdrawalsRoot = std::nullopt,
        };

        // Step 2a: Get LedgerConfig via storage-based LedgerMethods
        // Uses the parent block number since system configs are effective up to the parent
        ledger::LedgerConfig ledgerConfig;
        co_await ledger::getLedgerConfig(view, ledgerConfig, nextBlockNumber - 1, *m_blockFactory);
        auto blockVersion = ledgerConfig.compatibilityVersion();

        // Header fork era comes from the chain's EVM revision, not the Engine API version
        // (#5517). Fail closed if the chain has no on-chain revision.
        auto chainRevision = ledgerConfig.evmcRevisionForBlock(nextBlockNumber);
        if (!chainRevision.has_value())
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "EngineService: no on-chain EVM revision configured for block " +
                    std::to_string(nextBlockNumber) +
                    "; cannot derive the Eth header fork era (a v2 chain persists evmc_revision "
                    "at genesis)"});
        }
        auto forkVersion = bcos::engine::detail::ethBlockVersionFor(*chainRevision);

        if (forkVersion >= bcos::protocol::EthBlockVersion::CANCUN &&
            !payloadAttributes.parentBeaconBlockRoot.has_value())
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "EngineService: chain EVM revision requires the V3 payload attributes "
                    "(parentBeaconBlockRoot); forkchoiceUpdated must be called at version >= 3"});
        }
        if (forkVersion >= bcos::protocol::EthBlockVersion::SHANGHAI &&
            !payloadAttributes.withdrawals.has_value())
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "EngineService: chain EVM revision requires the V2 payload attributes "
                    "(withdrawals); forkchoiceUpdated must be called at version >= 2"});
        }
        if (version >= static_cast<std::uint32_t>(ApiVersion::V3) &&
            forkVersion < bcos::protocol::EthBlockVersion::CANCUN)
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "EngineService: forkchoiceUpdatedV3 requires a CANCUN-or-later chain fork; "
                    "chain EVM revision maps to " +
                    std::to_string(static_cast<int>(forkVersion))});
        }
        if (version >= static_cast<std::uint32_t>(ApiVersion::V2) &&
            forkVersion < bcos::protocol::EthBlockVersion::SHANGHAI)
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "EngineService: forkchoiceUpdatedV2 requires a SHANGHAI-or-later chain "
                    "fork; chain EVM revision maps to " +
                    std::to_string(static_cast<int>(forkVersion))});
        }

        if (forkVersion >= bcos::protocol::EthBlockVersion::SHANGHAI)
        {
            executionPayload.withdrawals =
                payloadAttributes.withdrawals.value_or(std::vector<WithdrawalV1>{});
        }
        if (forkVersion >= bcos::protocol::EthBlockVersion::CANCUN)
        {
            executionPayload.blobGasUsed = u256(0);
            executionPayload.excessBlobGas = u256(0);
            executionPayload.withdrawalsRoot =
                bcos::engine::detail::withdrawalsRootFor(executionPayload);
        }

        executionPayload.gasLimit = std::get<0>(ledgerConfig.gasLimit());

        // Execute transactions (if any) and finalize the Eth header with its RLP hash.
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
            emptyHeader->setExtraData(executionPayload.extraData);
            emptyHeader->setStateRoot(co_await calculateStateRoot(view, emptyHeader->version()));
            emptyHeader->setReceiptsRoot(bcos::ledger::mpt::emptyRootHash());
            emptyHeader->setTxsRoot(bcos::ledger::mpt::emptyRootHash());
            emptyHeader->setGasUsed(0);
            detail::finalizeEthBlockHeader(*emptyHeader, executionPayload,
                payloadAttributes.parentBeaconBlockRoot, forkVersion);
            executionPayload.stateRoot = emptyHeader->stateRoot();
            executionPayload.receiptsRoot = bcos::ledger::mpt::emptyRootHash();
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
        blockHeader->setExtraData(executionPayload.extraData);

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
        // handling for malformed transactions. Empty list maps to the canonical empty-trie
        // root (validateHeader rejects an all-zero txsRoot).
        h256 txRoot = bcos::ledger::mpt::emptyRootHash();
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

        // Step 2e: Compute receipt root (Merkle over receipt hashes). Empty list maps to
        // the canonical empty-trie root (validateHeader rejects an all-zero receiptsRoot).
        h256 receiptRoot = bcos::ledger::mpt::emptyRootHash();
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

        // Step 2h: Set computed values and finalize the Eth header (RLP hash, #5517).
        blockHeader->setStateRoot(stateRoot);
        blockHeader->setReceiptsRoot(receiptRoot);
        blockHeader->setTxsRoot(txRoot);
        blockHeader->setGasUsed(totalGasUsed);
        executionPayload.logsBloom = logsBloom;
        detail::finalizeEthBlockHeader(
            *blockHeader, executionPayload, payloadAttributes.parentBeaconBlockRoot, forkVersion);

        // Step 2i: Fill the execution payload with real values
        executionPayload.stateRoot = stateRoot;
        executionPayload.receiptsRoot = receiptRoot;
        executionPayload.gasUsed = totalGasUsed;
        executionPayload.blockHash = blockHeader->hash();
        executionPayload.gasLimit = std::get<0>(ledgerConfig.gasLimit());

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
    /// Serializes OP delegate reset/execute/commit. Distinct from x_state so cache
    /// lookups do not hold the map lock across EVM time.
    mutable std::mutex x_opExecute;
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
    /// OP execute/commit delegate (OpScheduler). Null on the generic engine and some fixtures.
    bcos::scheduler::SchedulerInterface::Ptr m_delegate;

    /// DA throttling caps (null or all-zero = uncapped). No RPC writer on this node yet.
    std::shared_ptr<bcos::engine::DACaps> m_daCaps;
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
};

}  // namespace bcos::engine
