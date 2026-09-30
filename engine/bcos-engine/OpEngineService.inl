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
 * @file OpEngineService.inl
 * @brief OP Engine API service implementation (payload build, execute, commit)
 */

#pragma once

// This is the DEFINITION half of the split: OpEngineService.h is declarations-only.
// Including this.inl is the opt-in instantiation point — members use
// the canonical block hash (bcos-rlp-protocol) and the OP lane's estimatedDaSize.
// engine links rlp-protocol PUBLIC so installed consumers inherit the include dirs;
// instantiators still need to link opstack-executor.
#include "OpEngineService.h"
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <opstack-executor/OpRollupCost.h>

#include <iterator>
#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/view/transform.hpp>

namespace bcos::engine
{

namespace detail
{
/// DA-caps unit bridge: the caps count ESTIMATED DA bytes (the Fjord FastLZ size estimate
/// of the sealed envelope), which takes an evmc::bytes_view while the envelope carrier is
/// a bcos::bytes.
inline auto estimatedDaBytes(bcos::bytes const& env)
{
    return bcos::executor_v1::opstack::estimatedDaSize(evmc::bytes_view(env.data(), env.size()));
}

/// release ExecutionPayload keeps a single carrier: `transactions[i].raw`.
inline auto rawEnvelopes(ExecutionPayload const& payload)
{
    return payload.transactions |
           ::ranges::views::transform(
               [](EngineTransaction const& tx) -> bytes const& { return tx.raw; });
}

// isUndecodablePayloadFault / fcuInvalidIfUndecodable live in EngineServiceCommon.h
// (namespace bcos::engine::detail) so the Eth build path maps the same fault the same way.
}  // namespace detail

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<ForkchoiceUpdatedResult>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::updateForkchoice(
    const ForkchoiceState& forkchoiceState, const PayloadAttributes* payloadAttributes,
    std::uint32_t version)
{
    if (!isForkchoiceVersionSupported(version))
    {
        BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                              << bcos::errinfo_comment{"Unsupported Engine API version"});
    }
    std::vector<bcos::bytes> decodedForcedTxs;
    if (payloadAttributes != nullptr)
    {
        if (version < 3)
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "Isthmus+ payload building requires engine_forkchoiceUpdatedV3 "
                    "or V4 (JSON-RPC -38005)"});
        }
        if (auto validationError = engine_common::validatePayloadAttributes(
                *payloadAttributes, version, &decodedForcedTxs);
            validationError.has_value())
        {
            co_return ForkchoiceUpdatedResult{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError),
                .payloadId = std::nullopt,
            };
        }
        // Jovian attribute fields (minBaseFee) are keyed on the CHILD block's time — the
        // block these attributes build (op-node derive/attributes.go, nextL2Time).
        if (auto validationError = engine_common::op::validateOpPayloadAttributes(
                *payloadAttributes, m_scheduler.isJovianActive(payloadAttributes->timestamp));
            validationError.has_value())
        {
            co_return ForkchoiceUpdatedResult{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError),
                .payloadId = std::nullopt,
            };
        }
        if (auto validationError = engine_common::op::requireL1AttributesDeposit(
                *payloadAttributes, m_allowSynthesizedL1Attributes);
            validationError.has_value())
        {
            co_return ForkchoiceUpdatedResult{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError),
                .payloadId = std::nullopt,
            };
        }
    }

    if (forkchoiceState.headBlockHash == bcos::h256{})
    {
        // op-geth: "Forkchoice requested update to zero hash" → STATUS_INVALID.
        // SYNCING would tell the CL to wait on sync for a malformed request.
        co_return ForkchoiceUpdatedResult{
            .payloadStatus = makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string("Forkchoice requested update to zero hash")),
            .payloadId = std::nullopt,
        };
    }

    // Head: any block of the unfinalized window or of the finalized ledger. Unknown → SYNCING
    // (zero was INVALID above). A ledger block strictly below the finalized tip is a request
    // to rewind finalized state, which the OP lane never does (D7): -38002. The tip itself is
    // a legal head (restart: FCU(head=safe=finalized=tip), D1 §13.2).
    auto head = co_await resolveBlock(forkchoiceState.headBlockHash);
    if (!head.has_value())
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus =
                makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt),
            .payloadId = std::nullopt,
        };
    }
    auto const finalizedTip = co_await finalizedTipNumber();
    if (!head->inWindow && head->number < finalizedTip)
    {
        BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                  "Forkchoice head is below the finalized block; the finalized "
                                  "chain does not rewind"});
    }
    // All-zero safe/finalized hashes are the Engine-API "not set" value: skip resolution and
    // ancestry checks for that field (op-geth SetSafe/SetFinalized are only called for
    // non-zero hashes). A non-zero unresolvable safe/finalized is InvalidForkchoiceState
    // (op-geth); "canonical" for them means "on the head's ancestor chain", which for a
    // window head walks the window and for the finalized part reads NUMBER_2_HASH.
    bool const safeSet = forkchoiceState.safeBlockHash != bcos::h256{};
    bool const finalizedSet = forkchoiceState.finalizedBlockHash != bcos::h256{};
    std::optional<ResolvedBlock> safe;
    std::optional<ResolvedBlock> finalized;
    if (safeSet)
    {
        safe = co_await resolveBlock(forkchoiceState.safeBlockHash);
    }
    if (finalizedSet)
    {
        finalized = co_await resolveBlock(forkchoiceState.finalizedBlockHash);
    }
    if ((safeSet && !safe.has_value()) || (finalizedSet && !finalized.has_value()))
    {
        BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                  "Forkchoice safe or finalized block is unknown"});
    }
    bool const safeCanonical = !safeSet || co_await isOnChainOf(*head, *safe);
    bool const finalizedCanonical = !finalizedSet || co_await isOnChainOf(*head, *finalized);

    ResolvedForkchoice resolved{
        .state = forkchoiceState,
        .headNumber = head->number,
        .safeNumber = safe ? std::optional(safe->number) : std::nullopt,
        .finalizedNumber = finalized ? std::optional(finalized->number) : std::nullopt,
        // A ledger block is canonical by construction (the backend holds finalized blocks
        // only); a window block is vouched for by headKnown instead.
        .headCanonical = !head->inWindow,
        .payloadAttributesPresent = payloadAttributes != nullptr,
        .safeCanonical = safeCanonical,
        .finalizedCanonical = finalizedCanonical,
        .headKnown = true,
    };
    // Throws InvalidForkchoiceState (-38002) on ordering / ancestry violations. A Rewind is
    // a REAL head switch to an older block here (headKnown): the tracker moved, `latest`
    // follows it, and attributes below build on that older head (D1 §11.3 derivation reorg).
    auto const outcome = m_tracker.applyForkchoice(resolved);
    if (outcome == ForkchoiceApplyResult::Rewind)
    {
        BCOS_LOG(INFO) << LOG_BADGE("OpEngineService") << LOG_DESC("forkchoice head rewound")
                       << LOG_KV("head", forkchoiceState.headBlockHash.abridged())
                       << LOG_KV("number", head->number);
    }

    // Finalized advancing into the window: merge tip..F and prune the side branches. Runs
    // after the tracker accepted the triple, so a rejected FCU never finalizes anything.
    if (finalized && finalized->inWindow)
    {
        requireDelegate();
        bcos::Error::Ptr finalizeError;
        m_delegate->finalizeUpTo(
            finalized->hash, [&](bcos::Error::Ptr error) { finalizeError = std::move(error); });
        if (finalizeError)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      std::string("finalizing the unfinalized window failed: ") +
                                      finalizeError->errorMessage()});
        }
    }

    ForkchoiceUpdatedResult result{
        .payloadStatus =
            makeStatus(PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
        .payloadId = std::nullopt,
    };
    if (payloadAttributes == nullptr)
    {
        co_return result;
    }

    auto parentHeader = co_await loadHeaderOf(*head);
    if (!parentHeader)
    {
        // Head hash resolved (window or ledger) but its header row is gone: local-state
        // corruption — fail closed rather than pricing the block at 1 gwei.
        co_return ForkchoiceUpdatedResult{
            .payloadStatus =
                makeStatus(PayloadValidationStatus::Invalid, forkchoiceState.headBlockHash,
                    std::string("parent block header is missing from storage")),
            .payloadId = std::nullopt,
        };
    }
    co_return co_await buildOpPayload(forkchoiceState, *payloadAttributes, version,
        std::move(parentHeader), std::move(decodedForcedTxs));
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<std::optional<
    typename OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::ResolvedBlock>>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::resolveBlock(
    h256 const& blockHash)
{
    if (blockHash == bcos::h256{})
    {
        co_return std::nullopt;
    }
    if (m_delegate)
    {
        if (auto entry = m_delegate->unfinalizedBlock(blockHash))
        {
            co_return ResolvedBlock{.number = entry->number,
                .hash = entry->hash,
                .parentHash = entry->parentHash,
                .header = entry->header,
                .inWindow = true};
        }
    }
    // The finalized plane only: no anonymous pending layers exist on this lane any more.
    auto view = m_globalStateStorage.forkCommitted();
    auto number = co_await bcos::ledger::getBlockNumber(view, blockHash, bcos::ledger::fromStorage);
    if (!number.has_value())
    {
        co_return std::nullopt;
    }
    co_return ResolvedBlock{.number = *number,
        .hash = blockHash,
        .parentHash = {},
        .header = nullptr,
        .inWindow = false};
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<bcos::protocol::BlockHeader::Ptr>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::loadHeaderOf(
    ResolvedBlock const& block)
{
    if (block.header)
    {
        co_return block.header;
    }
    auto view = m_globalStateStorage.forkCommitted();
    auto const numberStr = boost::lexical_cast<std::string>(block.number);
    auto entry = co_await storage2::readOne(
        view, executor_v1::StateKeyView{ledger::SYS_NUMBER_2_BLOCK_HEADER, numberStr});
    if (!entry.has_value())
    {
        co_return nullptr;
    }
    bcos::protocol::BlockHeader::Ptr header;
    try
    {
        auto stored = entry->get();
        bcos::bytes bytes(stored.begin(), stored.end());
        header = m_blockFactory->blockHeaderFactory()->createBlockHeader(bytes);
    }
    catch (const std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            OpExecutionInternalError{} << bcos::errinfo_comment{
                std::string("stored parent block header is undecodable: ") + e.what()});
    }
    if (header->number() != block.number)
    {
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                  "stored parent block header height mismatch"});
    }
    co_return header;
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<bool> OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::isOnChainOf(
    ResolvedBlock const& head, ResolvedBlock const& target)
{
    if (target.number > head.number)
    {
        co_return false;
    }
    if (target.inWindow)
    {
        // Walk the window from the head; the target must be met before leaving it.
        auto cursor = head;
        while (cursor.inWindow)
        {
            if (cursor.hash == target.hash)
            {
                co_return true;
            }
            auto parent = co_await resolveBlock(cursor.parentHash);
            if (!parent.has_value())
            {
                co_return false;
            }
            cursor = *parent;
        }
        co_return false;
    }
    // A ledger target is on every chain that reaches the finalized tip, i.e. every resolvable
    // head's, when the ledger's own number→hash row names it (the backend holds finalized
    // blocks only, so that row IS canonicality; the current-number row is not consulted —
    // fixtures and freshly imported ledgers may lack it).
    auto view = m_globalStateStorage.forkCommitted();
    auto canonical =
        co_await bcos::ledger::getBlockHash(view, target.number, bcos::ledger::fromStorage);
    co_return canonical.has_value() && *canonical == target.hash;
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<bcos::protocol::BlockNumber>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::finalizedTipNumber()
{
    auto view = m_globalStateStorage.forkCommitted();
    co_return co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<ForkchoiceUpdatedResult>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::buildOpPayload(
    const ForkchoiceState& forkchoiceState, const PayloadAttributes& payloadAttributes,
    std::uint32_t version, bcos::protocol::BlockHeader::Ptr parentHeader,
    std::vector<bcos::bytes> decodedForcedTxs)
{
    const bcos::protocol::BlockNumber nextBlockNumber = parentHeader->number() + 1;
    // Same policy as EthEngineService: deterministic derivePayloadId, not a process-local
    // sequence counter. Reuse validate's decoded forced txs.
    // The id's version byte is the PAYLOAD SHAPE version (V3/V4-method → PayloadV3),
    // matching both the cache entry's version below and upstream: op-geth's
    // ForkchoiceUpdatedV3/V4 build the same PayloadV3 shape, so the same content under
    // either method must derive the same id (GetPayloadV4 accepts only PayloadV3 ids).
    auto payloadIdOpt =
        engine_common::derivePayloadId(payloadAttributes, forkchoiceState.headBlockHash,
            engine_common::payloadShapeVersion(version), decodedForcedTxs);
    if (!payloadIdOpt.has_value())
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus = makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string("payloadAttributes.transactions contains undecodable hex")),
            .payloadId = std::nullopt,
        };
    }
    auto payloadId = *payloadIdOpt;

    // PARENT time, not the child's: op-geth's CalcBaseFee(config, parent, time) keys both the
    // Holocene extraData decode and the Jovian DA-footprint branch on parent.Time
    // (consensus/misc/eip1559/eip1559.go:64-110). The L1-attributes layout needs it too
    // (op-node's isJovianButNotFirstBlock — see OpSchedulerSeam::synthesizeL1AttributesEnvelope).
    // The parent header is the resolved head's: a window entry when the head is unfinalized
    // (the derivation-reorg build on B1 while B2a is tracked, D1 §11.3), else the ledger row.
    const int64_t parentTimestampMs = parentHeader->timestamp();
    const u256 baseFee =
        calcOpBaseFee(*parentHeader, m_scheduler.isJovianActive(parentTimestampMs));

    requireDelegate();

    // Sequencer lane (noTxPool=false): seal against the PARENT's chain view (D1 §10.2), so
    // MemPoolImpl::remove() judges "already on chain" by the nonce the unfinalized head chain
    // carries — the finalized plane would be up to `unfinalized_window` blocks stale and
    // re-seal every tx already included upstream of the head. The provider is the
    // OpScheduler's viewAt; without one (stubs) fork() is what it always was. Building the
    // view only when sealing happens keeps the noTxPool (verifier) path free of the window
    // walk.
    std::vector<protocol::Transaction::Ptr> sealedTxs;
    if (!payloadAttributes.noTxPool.value_or(false))
    {
        std::optional<ViewType> chainView;
        if (m_chainViewProvider)
        {
            chainView = co_await m_chainViewProvider(forkchoiceState.headBlockHash);
        }
        auto sealView = chainView ? std::move(*chainView) : m_globalStateStorage.fork();
        sealView.newMutable();
        m_memPool.remove(sealView);
        m_memPool.seal(m_blockTxCountLimit, sealView, std::back_inserter(sealedTxs));
    }

    std::vector<bytes> forcedEnvelopes;
    // Reached only when tests set allowSynthesizedL1Attributes. Production
    // op_engine_rpc never invents this envelope (op-geth does not either).
    if (!payloadAttributes.transactions.has_value() || payloadAttributes.transactions->empty())
    {
        // CHILD time picks the calldata layout (op-node's L1InfoDeposit(..., l2Timestamp)),
        // with the parent passed alongside so the Jovian ACTIVATION block still emits the
        // Isthmus layout — op-node's isJovianButNotFirstBlock (derive/l1_block_info.go:462).
        forcedEnvelopes.push_back(m_scheduler.synthesizeL1AttributesEnvelope(
            payloadAttributes.timestamp, parentTimestampMs));
    }
    if (payloadAttributes.transactions.has_value())
    {
        forcedEnvelopes.insert(forcedEnvelopes.end(),
            std::make_move_iterator(decodedForcedTxs.begin()),
            std::make_move_iterator(decodedForcedTxs.end()));
    }

    std::vector<std::pair<crypto::HashType, bytes>> sealedEnvelopes;
    std::vector<std::string> sealedSenders;
    std::vector<std::optional<std::uint64_t>> sealedNonces;
    sealedEnvelopes.reserve(sealedTxs.size());
    sealedSenders.reserve(sealedTxs.size());
    sealedNonces.reserve(sealedTxs.size());
    for (auto& sealedTx : sealedTxs)
    {
        if (sealedTx->type() !=
            static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
        {
            BCOS_LOG(WARNING) << LOG_BADGE("OpEngineService")
                              << LOG_DESC(
                                     "buildOpPayload: excluding transaction without an "
                                     "EIP-2718 wire form");
            continue;
        }
        sealedEnvelopes.emplace_back(
            sealedTx->hash(), bcostars::protocol::reassembleWeb3RawTransaction(
                                  sealedTx->extraTransactionBytes(), sealedTx->signatureData()));
        sealedSenders.emplace_back(sealedTx->sender());
        sealedNonces.emplace_back(bcos::safeFromQuantity(sealedTx->nonce()));
    }

    std::set<crypto::HashType> evicted;
    // op-geth miner: excluding nonce n of sender S also drops S's later nonces from
    // this candidate and never evicts those successors from the pool.
    // Walk by nonce, not sealed-vector position: seal order is not a
    // nonce-order contract the build path may assume.
    auto skipSenderTail = [&](crypto::HashType const& hash) {
        evicted.insert(hash);
        std::string sender;
        std::optional<std::uint64_t> culpritNonce;
        for (std::size_t i = 0; i < sealedEnvelopes.size(); ++i)
        {
            if (sealedEnvelopes[i].first == hash)
            {
                sender = sealedSenders[i];
                culpritNonce = sealedNonces[i];
                break;
            }
        }
        if (sender.empty() || !culpritNonce.has_value())
        {
            return;
        }
        for (std::size_t i = 0; i < sealedEnvelopes.size(); ++i)
        {
            if (sealedSenders[i] != sender || sealedEnvelopes[i].first == hash)
            {
                continue;
            }
            if (sealedNonces[i].has_value() && *sealedNonces[i] > *culpritNonce)
            {
                evicted.insert(sealedEnvelopes[i].first);
            }
        }
    };
    if (m_daCaps && m_daCaps->maxTxSize.load(std::memory_order_relaxed) != 0)
    {
        // DACaps count ESTIMATED DA bytes — the Fjord FastLZ size estimate of the sealed
        // envelope (op-geth's RollupCostData().EstimatedDASize()), never the raw envelope
        // length. While the cap is unset (zero = uncapped) skip the estimate entirely:
        // FastLZ over every sealed envelope on every attempt would tax the uncapped
        // default path for nothing.
        for (auto const& [hash, env] : sealedEnvelopes)
        {
            if (!m_daCaps->txFits(detail::estimatedDaBytes(env)))
            {
                skipSenderTail(hash);
            }
        }
    }

    auto const parentBeaconBlockRoot = payloadAttributes.parentBeaconBlockRoot.value();

    auto assemblePayload = [&](std::vector<bytes> candidateEnvelopes) {
        std::vector<EngineTransaction> candidateTransactions;
        candidateTransactions.reserve(candidateEnvelopes.size());
        for (auto& env : candidateEnvelopes)
        {
            // Move: env is a non-const ref into the by-value candidates vector, consumed
            // here; the candidate bytes become the payload's single carrier unchanged.
            candidateTransactions.push_back(
                EngineTransaction{.raw = std::move(env), .decoded = nullptr});
        }
        ExecutionPayload candidate{
            .logsBloom = Bloom{},
            .parentHash = forkchoiceState.headBlockHash,
            .stateRoot = h256{},
            .receiptsRoot = h256{},
            .prevRandao = payloadAttributes.prevRandao,
            .gasLimit = u256(payloadAttributes.gasLimit.value()),
            .gasUsed = 0,
            .baseFeePerGas = baseFee,
            .blockHash = h256{},
            .transactions = std::move(candidateTransactions),
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
        return candidate;
    };

    ExecutionPayload payload;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    // Retry loop: each evicted culprit re-executes the whole candidate block from scratch
    // (no incremental prefix reuse), so k failing pool txs cost up to k+1 full build+execute
    // passes plus the always-on canonical verify pass. Bounded by the sealed-envelope count;
    // only reworked when per-envelope execution becomes reusable.
    while (true)
    {
        std::vector<bytes> candidateEnvelopes = forcedEnvelopes;
        std::optional<bcos::engine::DACaps::Budget> budget;
        if (m_daCaps && m_daCaps->maxBlockSize.load(std::memory_order_relaxed) != 0)
        {
            // Same estimated-DA unit as txFits above: the block-size budget accumulates
            // the forced (undroppable) envelopes' estimates and admits each sealed tx by
            // its estimate, not its raw length.
            std::uint64_t forcedBytes = 0;
            for (auto const& env : forcedEnvelopes)
            {
                forcedBytes += detail::estimatedDaBytes(env);
            }
            budget.emplace(*m_daCaps, forcedBytes);
        }
        for (auto const& [hash, env] : sealedEnvelopes)
        {
            if (evicted.count(hash) != 0)
            {
                continue;
            }
            if (budget && !budget->admits(detail::estimatedDaBytes(env)))
            {
                break;
            }
            candidateEnvelopes.push_back(env);
        }
        payload = assemblePayload(std::move(candidateEnvelopes));

        const auto transactionsRoot = SchedulerType::computeTxRoot(detail::rawEnvelopes(payload));
        auto provisionalHeader = engine_common::op::rebuildOpEthHeader(
            m_blockFactory->blockHeaderFactory(), payload, transactionsRoot, parentBeaconBlockRoot);
        bcos::protocol::Block::Ptr block;
        try
        {
            block = buildOpBlock(payload, provisionalHeader);
        }
        catch (const OpExecutionInternalError& e)
        {
            if (auto invalid = detail::fcuInvalidIfUndecodable(e))
            {
                co_return *invalid;
            }
            throw;
        }

        bcos::Error::Ptr resetError;
        m_delegate->reset([&](bcos::Error::Ptr error) { resetError = std::move(error); });
        if (resetError)
        {
            // The build loop's whole model rests on reset having done its documented effect
            // (dropping any uncommitted pending, restoring the watermark) before executeBlock
            // runs; a failed reset must not be silently ignored.
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    std::string("OP payload build reset failed: ") + resetError->errorMessage()});
        }
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
        auto culprit = executeError ? culpritTxHashFromError(*executeError) :
                                      std::optional<crypto::HashType>{};
        // A block-gas capacity fault means the tx is VALID but does not fit this
        // candidate (mempool seals by count only). OpRejectIsCapacity's contract is
        // "skip this build, do not evict": the tx must stay in the mempool for a later
        // block, so the eviction loop only excludes it from this candidate.
        bool const isCapacityReject =
            executeError != nullptr &&
            boost::get_error_info<bcos::engine::OpRejectIsCapacity>(*executeError) != nullptr;
        if (culprit.has_value() && evicted.count(*culprit) == 0 &&
            std::any_of(sealedEnvelopes.begin(), sealedEnvelopes.end(),
                [&culprit](auto const& entry) { return entry.first == *culprit; }))
        {
            skipSenderTail(*culprit);
            if (!isCapacityReject)
            {
                std::array<crypto::HashType, 1> hashSpan{*culprit};
                m_memPool.removeByHash(std::span<crypto::HashType const>(hashSpan));
            }
            continue;
        }
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                  std::string("OP payload build execution failed: ") + message});
    }

    payload.stateRoot = executedHeader->stateRoot();
    payload.receiptsRoot = executedHeader->receiptsRoot();
    payload.gasUsed = u256(executedHeader->gasUsed());
    {
        auto executedBloom = executedHeader->logsBloom();
        std::copy(executedBloom.begin(), executedBloom.end(), payload.logsBloom.begin());
    }
    payload.withdrawalsRoot = executedHeader->withdrawalsRoot();
    if (auto executedBlobGas = executedHeader->blobGasUsed())
    {
        payload.blobGasUsed = *executedBlobGas;
    }
    auto finalHeader =
        engine_common::op::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(), payload,
            SchedulerType::computeTxRoot(detail::rawEnvelopes(payload)), parentBeaconBlockRoot);
    payload.blockHash = bcos::protocol::canonicalBlockHash(*finalHeader);

    bcos::protocol::Block::Ptr finalBlock;
    try
    {
        finalBlock = buildOpBlock(payload, finalHeader);
    }
    catch (const OpExecutionInternalError& e)
    {
        if (auto invalid = detail::fcuInvalidIfUndecodable(e))
        {
            co_return *invalid;
        }
        throw;
    }
    bcos::Error::Ptr resetError;
    m_delegate->reset([&](bcos::Error::Ptr error) { resetError = std::move(error); });
    if (resetError)
    {
        BOOST_THROW_EXCEPTION(
            OpExecutionInternalError{} << bcos::errinfo_comment{
                std::string("OP payload build reset failed: ") + resetError->errorMessage()});
    }
    bcos::Error::Ptr canonicalError;
    bcos::protocol::BlockHeader::Ptr canonicalHeader;
    m_delegate->executeBlock(finalBlock, /*verify=*/true,
        [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, bool) {
            canonicalError = std::move(error);
            canonicalHeader = std::move(header);
        });
    if (canonicalError || !canonicalHeader)
    {
        BOOST_THROW_EXCEPTION(
            OpExecutionInternalError{} << bcos::errinfo_comment{
                std::string("OP payload build canonical pass failed: ") +
                (canonicalError ? canonicalError->errorMessage() : "no executed header")});
    }

    auto commonEntry = std::make_shared<BuiltPayload>();
    commonEntry->version = engine_common::payloadShapeVersion(version);
    commonEntry->executionPayload = std::move(payload);
    commonEntry->blockValue = 0;
    commonEntry->blobsBundle = std::nullopt;
    commonEntry->shouldOverrideBuilder = false;
    commonEntry->parentBeaconBlockRoot = parentBeaconBlockRoot;
    if (engine_common::payloadShapeVersion(version) == static_cast<std::uint32_t>(ApiVersion::V3))
    {
        commonEntry->blobsBundle = BlobsBundleV1{};
    }

    OpPayloadArtifacts stagedArtifact{.canonicalHeader = std::move(canonicalHeader)};
    {
        // Copy the hash before the entry is moved: publishBuiltPayload stores the
        // payload by move, which leaves the original object's blockHash member
        // moved-from, and putStaged then hashes that reference into hashToId.
        const auto blockHash = commonEntry->executionPayload.blockHash;
        auto guard = m_tracker.lockExclusive();
        publishBuiltPayload(guard, m_artifacts, payloadId, blockHash, std::move(commonEntry),
            std::move(stagedArtifact));
    }

    co_return ForkchoiceUpdatedResult{
        .payloadStatus =
            makeStatus(PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
        .payloadId = payloadId,
    };
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<PayloadStatus>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::newPayload(
    const NewPayloadRequest& request, std::uint32_t version)
{
    co_return co_await handleOpNewPayload(request, version);
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<PayloadStatus>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::handleOpNewPayload(
    const NewPayloadRequest& request, std::uint32_t version)
{
    if (!isNewPayloadVersionSupported(version))
    {
        BOOST_THROW_EXCEPTION(
            UnsupportedFork{} << bcos::errinfo_comment{
                "Isthmus+ payloads require engine_newPayloadV4 (JSON-RPC -38005)"});
    }

    try
    {
        co_return co_await runOpNewPayloadSteps(request);
    }
    catch (const OpExecutionInternalError&)
    {
        throw;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(
            OpExecutionInternalError{} << bcos::errinfo_comment{
                "OP newPayload threw an unclassified exception outside block execution "
                "(validation, comparison or registration phase)"});
    }
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<PayloadStatus>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::runOpNewPayloadSteps(
    const NewPayloadRequest& request)
{
    // This service keeps no per-payload state: the executed header of a VALID payload is its
    // window entry in the delegate (executedHeader(hash)), so a failing duplicate submission
    // cannot clobber a header a concurrent success just registered.
    auto const& payload = request.executionPayload;

    // The payload's OWN time decides which shape it must have. Isthmus is stated, not
    // defaulted: OpForkSchedule.h documents Isthmus as the OP-mode baseline with no entry in
    // the schedule (OP mode itself is the Isthmus+ admission check), so the pre-Isthmus arm
    // is unreachable here.
    if (auto validationError = engine_common::op::validateOpNewPayloadRequest(
            request, m_scheduler.isJovianActive(payload.timestamp), /*isthmusActive=*/true);
        validationError.has_value())
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError);
    }

    const auto transactionsRoot = SchedulerType::computeTxRoot(detail::rawEnvelopes(payload));
    const auto ethHeader =
        engine_common::op::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(), payload,
            transactionsRoot, *request.parentBeaconBlockRoot);
    if (bcos::protocol::canonicalBlockHash(*ethHeader) != payload.blockHash)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("blockHash does not match the reconstructed block header"));
    }

    // Already known (window or finalized ledger): VALID without re-execution. Covers the honest
    // retry of an admitted payload and a resend of a finalized block.
    if (auto known = co_await resolveBlock(payload.blockHash); known.has_value())
    {
        co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
    }

    // Window depth (D1 §12.4, [op_engine_rpc] unfinalized_window): the block would sit more
    // than the configured distance above the finalized tip. Backpressure, not a rejection —
    // SYNCING makes op-node retry once its finalized signal catches up; nothing is lost.
    // Applies to built-here payloads too (their staged block simply waits). Loud every 256
    // blocks over so a stalled batcher/finality is visible in the log.
    auto const finalizedTip = co_await finalizedTipNumber();
    if (auto const over =
            static_cast<int64_t>(payload.blockNumber) - finalizedTip - m_unfinalizedWindow;
        over > 0)
    {
        if (over % 256 == 1)
        {
            BCOS_LOG(WARNING) << LOG_BADGE("OpEngineService")
                              << LOG_DESC(
                                     "unfinalized window full; answering SYNCING until "
                                     "finalized advances")
                              << LOG_KV("blockNumber", payload.blockNumber)
                              << LOG_KV("finalized", finalizedTip)
                              << LOG_KV("unfinalizedWindow", m_unfinalizedWindow)
                              << LOG_KV("blocksOver", over);
        }
        co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    }

    {
        // Built here (FCU+attrs → getPayload → this newPayload): the canonical build pass
        // left the executed block STAGED in the delegate, so admit it as-is instead of
        // re-executing. The staged layer is reused, not the artifact — the artifact holds only
        // the canonical header; the executed view lives in the scheduler's staging slot.
        bcos::protocol::BlockHeader::Ptr builtHeader;
        {
            auto shared = m_tracker.lockShared();
            builtHeader = detail::findBuiltHeader(shared, m_artifacts, payload.blockHash);
        }
        if (builtHeader)
        {
            requireDelegate();
            bcos::Error::Ptr commitError;
            // The callback's LedgerConfig is deliberately dropped rather than published into
            // the admission holder: the delegate is an OpScheduler, whose
            // loadCommitLedgerConfig carries only number + timestamp -- chainId nullopt and
            // features empty -- and TxValidator reads chainId from the holder, so publishing it
            // fail-closes EIP-155 admission from the first committed block on. The holder is
            // republished from the ledger after every FINALIZE instead; see
            // OpLedgerConfigRepublish.h.
            m_delegate->commitBlock(builtHeader,
                [&](bcos::Error::Ptr error, bcos::ledger::LedgerConfig::Ptr /*ledgerConfig*/) {
                    commitError = std::move(error);
                });
            if (!commitError)
            {
                co_return makeStatus(
                    PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
            }
            // Only the "built staged block was dropped" fault may fall through to a full
            // execute+admit: OpScheduler tags it as bcos::engine::OpPendingDropped
            // ("Unexpected empty results!" — a reset for a later build ran in between), and
            // answering -32603 on every retry of a still-valid payload would wedge the CL.
            // Every other admit failure is a real error and keeps its documented routing
            // (INVALID for OpConsensusRejected, internal error otherwise) — falling through
            // on one would hide storage faults. Keyed on the tag, not on
            // SchedulerError::UnknownError: classifyException's catch-all maps every
            // unclassified fault to that code, so a code test cannot separate a dropped
            // staged block from a RocksDB or merge fault.
            bool const pendingDropped =
                boost::get_error_info<bcos::engine::OpPendingDropped>(*commitError) != nullptr;
            if (!pendingDropped)
            {
                co_return mapDelegateError(*commitError, std::nullopt);
            }
            BCOS_LOG(WARNING) << LOG_BADGE("OpEngineService")
                              << LOG_DESC("newPayload: built staged block dropped; re-executing")
                              << LOG_KV("blockHash", payload.blockHash.hex())
                              << LOG_KV("commitError", commitError->errorMessage());
        }
    }

    // Parent: a window block or a ledger block. Unknown → SYNCING (Engine API; on this lane
    // only after a restart, D1 §11.1). A ledger parent strictly below the finalized tip means
    // the payload itself sits at or below finalized with an unknown hash: an L1 reorg deeper
    // than finality, outside D7 — SYNCING too (D1 §12.3 row 2), never INVALID.
    auto parent = co_await resolveBlock(payload.parentHash);
    if (!parent.has_value())
    {
        co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    }
    if (!parent->inWindow && parent->number < finalizedTip)
    {
        BCOS_LOG(WARNING) << LOG_BADGE("OpEngineService")
                          << LOG_DESC("newPayload below the finalized tip; answering SYNCING")
                          << LOG_KV("blockNumber", payload.blockNumber)
                          << LOG_KV("finalized", finalizedTip);
        co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    }
    const auto latestValidHash = std::make_optional(payload.parentHash);

    if (payload.blockNumber != parent->number + 1)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("blockNumber must be exactly one greater than the parent's"));
    }

    auto parentHeader = co_await loadHeaderOf(*parent);
    if (!parentHeader)
    {
        // Parent hash resolved. Skipping timestamp / baseFee here would accept a payload we
        // cannot price — fail closed.
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("parent block header is missing from storage"));
    }
    if (static_cast<uint64_t>(payload.timestamp) <=
        static_cast<uint64_t>(parentHeader->timestamp()))
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("timestamp must be strictly greater than the parent's"));
    }
    {
        // PARENT time (op-geth eip1559.go:64-110 keys CalcBaseFee on parent.Time).
        auto expectedBaseFee =
            calcOpBaseFee(*parentHeader, m_scheduler.isJovianActive(parentHeader->timestamp()));
        if (payload.baseFeePerGas != expectedBaseFee)
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("baseFeePerGas does not match the value computed "
                            "from the parent"));
        }
    }

    requireDelegate();

    // Payload-content fault: an envelope the CL submitted cannot be decoded into a
    // transaction. op-geth answers INVALID at block construction for this class; it is
    // not a node-internal fault, so it must not surface as -32603. Internal
    // faults (storage, delegate) still throw and map to -32603 by the caller.
    bcos::protocol::Block::Ptr block;
    try
    {
        block = buildOpBlock(payload, ethHeader);
    }
    catch (const OpExecutionInternalError& e)
    {
        // Same tag discipline as the two FCU build sites: only the
        // tagged payload-content fault maps to a consensus INVALID; any other
        // OpExecutionInternalError must keep propagating as -32603.
        if (!detail::isUndecodablePayloadFault(e))
        {
            throw;
        }
        // Stable Engine API string only. FCU already returns this
        // exact phrase via fcuInvalidIfUndecodable; dump Boost diagnostics in logs,
        // not in validationError.
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("undecodable payload transaction envelope"));
    }

    // Execute on the parent's chain view (the delegate stacks the window layers itself),
    // then ADMIT into the window. Nothing reaches the backend until a later FCU finalizes.
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
    if (!executedHeader || !executedHeader->withdrawalsRoot().has_value())
    {
        // Presence of withdrawalsRoot is stamped by this node's scheduler, not the
        // CL payload. A missing field is a node-internal fault (-32603), never a
        // consensus INVALID the CL would discard.
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                  "executed header is missing withdrawalsRoot"});
    }
    if (executedHeader->withdrawalsRoot() != payload.withdrawalsRoot)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("withdrawalsRoot does not match the executed header"));
    }

    bcos::Error::Ptr commitError;
    // Not published: see the built-staged admit above (the delegate's LedgerConfig is the
    // number+timestamp stub; the holder is republished from the ledger on finalize).
    m_delegate->commitBlock(executedHeader,
        [&](bcos::Error::Ptr error, bcos::ledger::LedgerConfig::Ptr /*ledgerConfig*/) {
            commitError = std::move(error);
        });
    if (commitError)
    {
        co_return mapDelegateError(*commitError, latestValidHash);
    }
    co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
bcos::protocol::Block::Ptr
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::buildOpBlock(
    const ExecutionPayload& payload, bcos::protocol::BlockHeader::Ptr header)
{
    auto block = m_blockFactory->createBlock();
    block->setBlockHeader(std::move(header));
    auto& hashImpl = *m_blockFactory->cryptoSuite()->hashImpl();
    for (auto const& env : detail::rawEnvelopes(payload))
    {
        const auto txHash = hashImpl.hash(env);
        // allowDeposit=true: the OP lane accepts 0x7e deposit envelopes — the CL submits
        // them via payloadAttributes.transactions.
        auto tarsTx = engine_common::op::opEnvelopeToTars(env, txHash, /*allowDeposit=*/true);
        if (!tarsTx)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{}
                                  << OpPayloadUndecodable{true}
                                  << bcos::errinfo_comment{"undecodable payload "
                                                           "transaction envelope"});
        }
        auto tx = engine_common::decodedTransactionFromEnvelope(std::move(*tarsTx), env);
        block->appendTransaction(std::move(tx));
    }
    return block;
}

}  // namespace bcos::engine
