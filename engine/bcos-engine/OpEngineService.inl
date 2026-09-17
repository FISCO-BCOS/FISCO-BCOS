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
// the canonical block hash (bcos-rlp-protocol) and bcos::evm::opstack::estimatedDaSize.
// engine links rlp-protocol PUBLIC so installed consumers inherit the include dirs;
// instantiators still need to link bcos-evm-opstack.
#include "OpEngineService.h"
#include <bcos-evm/opstack/RollupCost.h>
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <opstack-executor/OpCommitments.h>

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
    return bcos::evm::opstack::estimatedDaSize(evmc::bytes_view(env.data(), env.size()));
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
EngineForkContext
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::requireOpEngineForkAt(
    uint64_t timestampSeconds) const
{
    auto const resolved = m_scheduler.resolveEngineForkAt(timestampSeconds);
    if (auto const* ctx = std::get_if<EngineForkContext>(&resolved))
    {
        return *ctx;
    }
    auto const error = std::get<OpForkResolutionError>(resolved);
    if (error == OpForkResolutionError::UnsupportedTimestamp)
    {
        BOOST_THROW_EXCEPTION(UnsupportedFork{} << bcos::errinfo_comment{
                                  "Unsupported timestamp for OP Engine API profile"});
    }
    BOOST_THROW_EXCEPTION(UnsupportedFork{} << bcos::errinfo_comment{
                              "Inconsistent execution config for OP Engine API profile"});
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
OpBaseFeeClock OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::baseFeeClockFor(
    bcos::protocol::BlockHeader const& parentHeader, OpForkId newForkId) const
{
    auto const parentTsSec =
        unixSecondsFromInternalMillis(static_cast<uint64_t>(parentHeader.timestamp()));
    auto const parentFork = m_scheduler.forkIdAt(parentTsSec);
    return OpBaseFeeClock{
        .parentIsHolocene = extraDataLayoutFor(parentFork) != OpExtraDataLayout::Empty,
        .parentIsJovian = extraDataLayoutFor(parentFork) == OpExtraDataLayout::Jovian17,
        .newBlockIsCanyon = newForkId != OpForkId::Regolith,
        .eip1559 = m_eip1559,
    };
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<GetPayloadResult>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::getPayload(
    const PayloadID& payloadId, std::uint32_t version)
{
    BuiltPayloadPtr built;
    {
        auto shared = m_tracker.lockShared();
        built = shared.findPayload(payloadId);
    }
    if (!built)
    {
        BOOST_THROW_EXCEPTION(UnknownPayload{} << bcos::errinfo_comment{"Unknown payload"});
    }

    // Payload timestamp is internal milliseconds; the seam takes Unix seconds.
    uint64_t const tsSec = unixSecondsFromInternalMillis(built->executionPayload.timestamp);
    auto const ctx = requireOpEngineForkAt(tsSec);
    if (version != static_cast<std::uint32_t>(ctx.api.getPayload))
    {
        BOOST_THROW_EXCEPTION(
            UnsupportedFork{} << bcos::errinfo_comment{
                "getPayload version does not match the OP Engine API profile at payload "
                "timestamp"});
    }

    engine_common::requireGetPayloadShape(
        built->version, built->executionPayload, built->parentBeaconBlockRoot, version);
    // Shape the response like the block the fork defines: the builder's carrier always
    // holds every optional, and only the fork says which of them exist for this block.
    co_return engine_common::assembleGetPayloadData(*built, version, ctx.forkId);
}

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
        // Profile keys on attrs.timestamp (internal ms → Unix seconds), never head.
        // The method number and the extras (extraData layout, baseFee clock) are all
        // derived from it, so the table decides which FCU version this fork builds
        // with — Regolith V1 and Canyon V2 included.
        uint64_t const tsSec = unixSecondsFromInternalMillis(payloadAttributes->timestamp);
        auto const ctx = requireOpEngineForkAt(tsSec);
        if (version != static_cast<std::uint32_t>(ctx.api.forkchoiceUpdated))
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "forkchoiceUpdated version does not match the OP Engine API profile "
                    "at attributes timestamp"});
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
        // block these attributes build (op-node derive/attributes.go, nextL2Time); ctx.forkId
        // is resolved from exactly that timestamp.
        if (auto validationError =
                engine_common::op::validateOpPayloadAttributes(*payloadAttributes, ctx.forkId);
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

    auto view = m_globalStateStorage.fork();
    auto headBlockNumber = co_await bcos::ledger::getBlockNumber(
        view, forkchoiceState.headBlockHash, bcos::ledger::fromStorage);
    // S6: an imported (not-yet-canonical) head is a legal FCU target — the OP lookup
    // falls through to the ImportedStore before answering SYNCING (design §4.2 row 2).
    bool headIsImported = false;
    if (!headBlockNumber.has_value())
    {
        if (auto importedHead = m_importedStore.get(forkchoiceState.headBlockHash);
            importedHead.has_value())
        {
            headIsImported = true;
            headBlockNumber = importedHead->number;
        }
        else
        {
            // Unknown head: SYNCING and nothing else (§4.5; CL resets, not success).
            co_return ForkchoiceUpdatedResult{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt),
                .payloadId = std::nullopt,
            };
        }
    }
    // SetCanonical BEFORE the safe/finalized resolution (geth order; design §4.2:
    // "SetCanonical 先于 safe/finalized 检查") — the head is imported, so it cannot
    // already be the canonical hash of its height (import never writes canonical keys).
    bcos::protocol::BlockNumber canonicalTipNumber = -1;
    // N6: a ledger-canonical head above the tip pointer means the pointer lags the
    // already-written canonical rows (commitBlock/canonicalizeImportedHead write
    // SYS_NUMBER_2_HASH and SYS_CURRENT_STATE in one batch, so this is the partial
    // persist / recovery shape). The head's rows exist — only the pointer move is
    // missing. Record it here, but DO NOT write yet: a later safe/finalized rejection
    // must not leave the pointer advanced (design §4.2 失败则全部回到调用前).
    std::optional<bcos::protocol::BlockNumber> ledgerTipAdvance;
    if (headIsImported)
    {
        co_await canonicalizeImportedHead(forkchoiceState.headBlockHash);
        // Fresh view over the new canonical flat: the safe/finalized checks below must
        // resolve on the NEW chain, not the pre-SetCanonical one.
        view = m_globalStateStorage.fork();
        headBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, forkchoiceState.headBlockHash, bcos::ledger::fromStorage);
        if (!headBlockNumber.has_value())
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "canonicalize did not make the head canonical"});
        }
        canonicalTipNumber = *headBlockNumber;
    }
    else
    {
        canonicalTipNumber =
            co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
        auto canonicalHeadHashEarly =
            co_await bcos::ledger::getBlockHash(view, *headBlockNumber, bcos::ledger::fromStorage);
        if (canonicalHeadHashEarly.has_value() &&
            *canonicalHeadHashEarly == forkchoiceState.headBlockHash &&
            *headBlockNumber > canonicalTipNumber)
        {
            canonicalTipNumber = *headBlockNumber;
            ledgerTipAdvance = *headBlockNumber;
        }
    }
    // All-zero safe/finalized hashes are the Engine-API "not set" value: skip number
    // resolution and canonical checks for that field (op-geth SetSafe/SetFinalized are
    // only called for non-zero hashes). A missing non-zero HEAD is SYNCING; a
    // zero HEAD is INVALID. A non-zero unresolvable safe/finalized is
    // InvalidForkchoiceState (op-geth).
    bool const safeSet = forkchoiceState.safeBlockHash != bcos::h256{};
    bool const finalizedSet = forkchoiceState.finalizedBlockHash != bcos::h256{};
    auto safeBlockNumber = safeSet ? co_await bcos::ledger::getBlockNumber(view,
                                         forkchoiceState.safeBlockHash, bcos::ledger::fromStorage) :
                                     std::nullopt;
    auto finalizedBlockNumber =
        finalizedSet ? co_await bcos::ledger::getBlockNumber(
                           view, forkchoiceState.finalizedBlockHash, bcos::ledger::fromStorage) :
                       std::nullopt;

    if (!headBlockNumber.has_value())
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus =
                makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt),
            .payloadId = std::nullopt,
        };
    }
    if ((safeSet && !safeBlockNumber.has_value()) ||
        (finalizedSet && !finalizedBlockNumber.has_value()))
    {
        BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                  "Forkchoice safe or finalized block is unknown"});
    }

    auto canonicalHeadHash =
        co_await bcos::ledger::getBlockHash(view, *headBlockNumber, bcos::ledger::fromStorage);
    bool const headCanonical =
        canonicalHeadHash.has_value() && *canonicalHeadHash == forkchoiceState.headBlockHash;
    // Same-number safe/finalized already resolved above: their canonical hash is the
    // head's (one NUMBER_2_HASH row per height), so reuse it instead of a second storage
    // read; zero (unset) fields skip resolution entirely. Heartbeat FCUs (all three
    // hashes equal) drop from 3 to 1 sequential reads.
    auto canonicalSafeHash =
        (!safeSet || *safeBlockNumber == *headBlockNumber) ?
            canonicalHeadHash :
            co_await bcos::ledger::getBlockHash(view, *safeBlockNumber, bcos::ledger::fromStorage);
    auto canonicalFinalizedHash = (!finalizedSet || *finalizedBlockNumber == *headBlockNumber) ?
                                      canonicalHeadHash :
                                      co_await bcos::ledger::getBlockHash(
                                          view, *finalizedBlockNumber, bcos::ledger::fromStorage);

    ResolvedForkchoice resolved{
        .state = forkchoiceState,
        .headNumber = *headBlockNumber,
        .safeNumber = safeBlockNumber,
        .finalizedNumber = finalizedBlockNumber,
        .headCanonical = headCanonical,
        .payloadAttributesPresent = payloadAttributes != nullptr,
        .safeCanonical = engine_common::forkchoiceHashIsCanonical(
            forkchoiceState.safeBlockHash, canonicalSafeHash),
        .finalizedCanonical = engine_common::forkchoiceHashIsCanonical(
            forkchoiceState.finalizedBlockHash, canonicalFinalizedHash),
        .allowNonLinearHead = true,
        .canonicalTipNumber = canonicalTipNumber,
    };
    const auto applyResult = m_tracker.applyForkchoice(resolved);
    if (applyResult == ForkchoiceApplyResult::Swallowed)
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus = makeStatus(
                PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
            .payloadId = std::nullopt,
        };
    }

    if (ledgerTipAdvance.has_value())
    {
        // N6: commit the deferred pointer move only now that the head is confirmed
        // canonical, the safe/finalized fields passed, and the tracker accepted the FCU.
        // The head's canonical rows already exist (the detection premise); only the tip
        // pointer lagged. NOT mergeView/mergeBackStorage: those merge the OLDEST queued
        // layer whenever the pending deque is non-empty (MultiLayerStorage.h's FIFO
        // warning), which would commit another block's in-flight layer from inside an FCU.
        auto row = std::make_shared<typename GlobalStateStorageType::MutableStorage>();
        bcos::storage::Entry numberEntry;
        numberEntry.set(std::to_string(*ledgerTipAdvance));
        co_await storage2::writeOne(*row,
            executor_v1::StateKey{
                bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
            std::move(numberEntry));
        co_await m_globalStateStorage.mergeToBackends(*row);
        if (m_delegate)
        {
            m_delegate->canonicalizedTo(*ledgerTipAdvance);
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

    co_return co_await buildOpPayload(forkchoiceState, *payloadAttributes, version,
        *headBlockNumber + 1, std::move(decodedForcedTxs));
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<ForkchoiceUpdatedResult>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::buildOpPayload(
    const ForkchoiceState& forkchoiceState, const PayloadAttributes& payloadAttributes,
    std::uint32_t version, bcos::protocol::BlockNumber nextBlockNumber,
    std::vector<bcos::bytes> decodedForcedTxs)
{
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

    // The header shape (and, below, the base-fee clock) key on the fork the attrs
    // timestamp selects. Re-resolving here is safe: the caller already proved this
    // timestamp resolves, and it is the same value.
    auto const ctx =
        requireOpEngineForkAt(unixSecondsFromInternalMillis(payloadAttributes.timestamp));

    u256 baseFee;
    uint64_t parentTsSec = 0;
    {
        auto view = m_globalStateStorage.fork();
        auto parentNumberStr = boost::lexical_cast<std::string>(nextBlockNumber - 1);
        auto parentHeaderEntry = co_await storage2::readOne(
            view, executor_v1::StateKeyView{ledger::SYS_NUMBER_2_BLOCK_HEADER, parentNumberStr});
        if (!parentHeaderEntry.has_value())
        {
            // Parent hash already resolved (canonical). Missing header is local-state
            // corruption — fail closed rather than pricing the block at 1 gwei.
            co_return ForkchoiceUpdatedResult{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Invalid, forkchoiceState.headBlockHash,
                        std::string("parent block header is missing from storage")),
                .payloadId = std::nullopt,
            };
        }
        auto stored = parentHeaderEntry->get();
        bcos::bytes parentHeaderBytes(stored.begin(), stored.end());
        auto parentHeader =
            m_blockFactory->blockHeaderFactory()->createBlockHeader(parentHeaderBytes);
        // PARENT time, not the child's: op-geth's CalcBaseFee(config, parent, time) keys both
        // the Holocene extraData decode and the Jovian DA-footprint branch on parent.Time
        // (consensus/misc/eip1559/eip1559.go:64-110) — baseFeeClockFor(parent, fork) resolves
        // that parent fork from the parent's own timestamp.
        parentTsSec =
            unixSecondsFromInternalMillis(static_cast<uint64_t>(parentHeader->timestamp()));
        baseFee = calcOpNextBlockBaseFee(*parentHeader, baseFeeClockFor(*parentHeader, ctx.forkId));
    }

    requireDelegate();

    // Q5 builder half: activation blocks are deposits-only. Skip the mempool
    // (treat as noTxPool) and FCU-INVALID any non-deposit already in attrs.
    // Hash-less execute rejects used to flatten to -32603 here.
    uint64_t const attrsTsSec = unixSecondsFromInternalMillis(payloadAttributes.timestamp);
    bool const activation = m_scheduler.isNoUserTxActivationBlock(parentTsSec, attrsTsSec);
    if (activation)
    {
        for (auto const& env : decodedForcedTxs)
        {
            if (dispatchRawTransaction(bcos::ref(env)) != RawTransactionKind::Deposit)
            {
                co_return ForkchoiceUpdatedResult{
                    .payloadStatus =
                        makeStatus(PayloadValidationStatus::Invalid, forkchoiceState.headBlockHash,
                            std::string("op block: unexpected non-deposit transactions in fork "
                                        "activation block")),
                    .payloadId = std::nullopt,
                };
            }
        }
    }

    auto sealView = m_globalStateStorage.fork();
    std::vector<protocol::Transaction::Ptr> sealedTxs;
    if (!activation && !payloadAttributes.noTxPool.value_or(false))
    {
        sealView.newMutable();
        m_memPool.remove(sealView);
        m_memPool.seal(m_blockTxCountLimit, sealView, std::back_inserter(sealedTxs));
    }

    std::vector<bytes> forcedEnvelopes;
    // Reached only when tests set allowSynthesizedL1Attributes. Production
    // op_engine_rpc never invents this envelope (op-geth does not either).
    if (!payloadAttributes.transactions.has_value() || payloadAttributes.transactions->empty())
    {
        // CHILD time picks the calldata layout, with the parent's fork deciding whether the
        // Jovian ACTIVATION block still emits the Isthmus layout (op-node's
        // isJovianButNotFirstBlock, derive/l1_block_info.go:462) — handled inside the seam.
        forcedEnvelopes.push_back(m_scheduler.synthesizeL1AttributesEnvelope(
            unixSecondsFromInternalMillis(payloadAttributes.timestamp)));
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

    auto const parentBeaconBlockRoot = payloadAttributes.parentBeaconBlockRoot;

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
            .extraData = detail::encodeOptimismExtraData(payloadAttributes, m_eip1559),
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
        auto provisionalHeader =
            engine_common::op::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(), payload,
                transactionsRoot, parentBeaconBlockRoot, ctx.forkId);
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
    // OP lane: EthBlockHeader::computeHash directly — pre-Canyon builds carry no
    // withdrawalsRoot, so canonicalBlockHash would fall back to header.hash() and stop
    // being the Ethereum RLP hash (pre-Bedrock/karst payloads must still verify).
    auto finalHeader = engine_common::op::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(),
        payload, SchedulerType::computeTxRoot(detail::rawEnvelopes(payload)), parentBeaconBlockRoot,
        ctx.forkId);
    payload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*finalHeader);

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

/// The SYS_NUMBER_2_TXS row body, byte-for-byte the shape Ledger.cpp's
/// prewriteBlockToBuffer writes: a metadata block of (txHash, recipient) pairs.
inline bcos::bytes encodeNumberToTxsRow(bcos::protocol::BlockFactory& blockFactory,
    std::vector<bcos::h256> const& txHashes, std::vector<std::string> const& recipients)
{
    auto metadataBlock = blockFactory.createBlock();
    for (std::size_t i = 0; i < txHashes.size(); ++i)
    {
        metadataBlock->appendTransactionMetaData(blockFactory.createTransactionMetaData(
            txHashes[i], i < recipients.size() ? recipients[i] : std::string{}));
    }
    bcos::bytes encoded;
    metadataBlock->encode(encoded);
    return encoded;
}

/// True for the ledger's canonical-index/metadata tables. A switch's whole-plane
/// replacement must scope its flat-absence scrub to the STATE plane: these rows can
/// belong to still-canonical ancestors (they are not part of the executed state) and
/// are trimmed explicitly by height instead (design §4.4.5/§4.4.6, review N2).
inline bool isLedgerCanonicalMetadataTable(std::string_view table)
{
    return table == bcos::ledger::SYS_CURRENT_STATE || table == bcos::ledger::SYS_HASH_2_NUMBER ||
           table == bcos::ledger::SYS_NUMBER_2_HASH ||
           table == bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES ||
           table == bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER ||
           table == bcos::ledger::SYS_NUMBER_2_TXS || table == bcos::ledger::SYS_HASH_2_TX ||
           table == bcos::ledger::SYS_HASH_2_RECEIPT || table == bcos::ledger::SYS_CHAIN_METADATA;
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<PayloadStatus>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::handleOpNewPayload(
    const NewPayloadRequest& request, std::uint32_t version)
{
    if (!isNewPayloadVersionSupported(version))
    {
        BOOST_THROW_EXCEPTION(
            UnsupportedFork{} << bcos::errinfo_comment{"unsupported newPayload method version"});
    }
    // The pair check stays OUTSIDE the try below: that catch (...) would fold
    // UnsupportedFork into an internal error (-32603) instead of -38005.
    uint64_t const tsSec = unixSecondsFromInternalMillis(request.executionPayload.timestamp);
    auto const ctx = requireOpEngineForkAt(tsSec);
    if (version != static_cast<std::uint32_t>(ctx.api.newPayload))
    {
        BOOST_THROW_EXCEPTION(UnsupportedFork{} << bcos::errinfo_comment{
                                  "newPayload version does not match the OP Engine API profile "
                                  "at payload timestamp"});
    }

    try
    {
        co_return co_await runOpNewPayloadSteps(request, ctx, version);
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

/// Header-level commitment snapshot for the engine-side import gate (the same field
/// set OpScheduler's verify arm compares via mismatchedFieldOf). Namespace-scope
/// inline: the .inl parses in TUs that never instantiate the consumer template.
inline bcos::evm::engine::OpBlockCommitments commitmentsOfHeader(
    bcos::protocol::BlockHeader const& h)
{
    auto bloom = h.logsBloom();
    bcos::h2048 logsBloom(reinterpret_cast<const bcos::byte*>(bloom.data()), bloom.size());
    std::optional<uint64_t> blobGasUsed;
    if (auto bg = h.blobGasUsed())
    {
        // Same bounds-checked narrowing as OpScheduler's headerCommitments (the sibling
        // projection of this surface): an out-of-range value fails closed with
        // OpConsensusError instead of silently truncating modulo 2^64. The value is not
        // wire-reachable (validateOpBlobGasUsed rejects it first); this keeps the two
        // projections from diverging (U4-F1, merging U6-F4).
        blobGasUsed =
            bcos::evm::engine::detail::narrowU256ToU64(*bg, "commitmentsOfHeader blobGasUsed");
    }
    return bcos::evm::engine::OpBlockCommitments{
        .receiptsRoot = h.receiptsRoot(),
        .logsBloom = logsBloom,
        .withdrawalsRoot = h.withdrawalsRoot().value_or(bcos::h256{}),
        .stateRoot = h.stateRoot(),
        .gasUsed = h.gasUsed(),
        .txRoot = h.txsRoot(),
        .blobGasUsed = blobGasUsed,
        .requestsHash = h.requestsHash(),
    };
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<PayloadStatus>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::runOpNewPayloadSteps(
    const NewPayloadRequest& request, const EngineForkContext& ctx, std::uint32_t version)
{
    // No reset of m_lastExecutedHeader here: a duplicate newPayload
    // arriving while another one is mid-flight must not clear a header the
    // concurrent success just published. Assignment happens only on the success
    // paths, so a failed run simply leaves the previous payload's header — the
    // "last executed" semantics the accessor documents.
    auto const& payload = request.executionPayload;

    if (auto validationError =
            engine_common::op::validateOpNewPayloadRequest(request, ctx.forkId, version);
        validationError.has_value())
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError);
    }

    const auto transactionsRoot = SchedulerType::computeTxRoot(detail::rawEnvelopes(payload));
    const auto ethHeader =
        engine_common::op::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(), payload,
            transactionsRoot, request.parentBeaconBlockRoot, ctx.forkId);
    // Direct EthBlockHeader::computeHash, NOT canonicalBlockHash — pre-Canyon payloads
    // (no withdrawalsRoot) must still verify against the eth RLP hash (see build path).
    if (bcos::protocol::EthBlockHeader::computeHash(*ethHeader) != payload.blockHash)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("blockHash does not match the reconstructed block header"));
    }

    {
        bcos::protocol::BlockHeader::Ptr builtHeader;
        {
            auto shared = m_tracker.lockShared();
            builtHeader = detail::findBuiltHeader(shared, m_artifacts, payload.blockHash);
        }
        if (builtHeader)
        {
            auto knownView = m_globalStateStorage.fork();
            if (auto known = co_await bcos::ledger::getBlockNumber(
                    knownView, payload.blockHash, bcos::ledger::fromStorage);
                known.has_value())
            {
                co_return makeStatus(
                    PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
            }
            requireDelegate();
            bcos::Error::Ptr commitError;
            // The callback's LedgerConfig is deliberately dropped rather than published into
            // the admission holder: the delegate is an OpScheduler, whose
            // loadCommitLedgerConfig carries only number + timestamp -- chainId nullopt and
            // features empty -- and TxValidator reads chainId from the holder, so publishing it
            // fail-closes EIP-155 admission from the first committed block on. The holder is
            // republished from the ledger after every commit instead; see
            // OpLedgerConfigRepublish.h.
            m_delegate->commitBlock(builtHeader,
                [&](bcos::Error::Ptr error, bcos::ledger::LedgerConfig::Ptr /*ledgerConfig*/) {
                    commitError = std::move(error);
                });
            if (!commitError)
            {
                std::lock_guard lock(m_lastExecutedHeaderMutex);
                m_lastExecutedHeader = builtHeader;
                co_return makeStatus(
                    PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
            }
            // Only the "built pending was dropped or replaced" fault may fall through to a
            // full execute+commit: OpScheduler tags it as bcos::engine::OpPendingDropped
            // ("Unexpected empty results!"), and answering -32603 on every retry of a
            // still-valid payload would wedge the CL. Every other commit failure is a real
            // error and keeps its documented routing (INVALID for OpConsensusRejected,
            // internal error otherwise) — falling through on one would hide storage faults.
            // Keyed on the tag, not on SchedulerError::UnknownError: classifyException's
            // catch-all maps every unclassified commit fault to that code, so a code test
            // cannot separate a dropped pending from a RocksDB or merge fault.
            bool const pendingDropped =
                boost::get_error_info<bcos::engine::OpPendingDropped>(*commitError) != nullptr;
            if (!pendingDropped)
            {
                co_return mapDelegateError(*commitError, std::nullopt);
            }
            BCOS_LOG(WARNING) << LOG_BADGE("OpEngineService")
                              << LOG_DESC("newPayload: built pending dropped; re-executing")
                              << LOG_KV("blockHash", payload.blockHash.hex())
                              << LOG_KV("commitError", commitError->errorMessage());
        }
    }

    auto view = m_globalStateStorage.fork();
    // OP parent lookup (design §4.1): canonical chain first, then the ImportedStore.
    // Missing in BOTH is the only parent-shape SYNCING (§4.5, no ACCEPTED) — the old
    // "known but not canonical → SYNCING" dead-end is gone: an imported parent
    // extends the block tree.
    const auto latestValidHash = std::make_optional(payload.parentHash);
    std::optional<bcos::protocol::BlockNumber> canonicalParentNumber =
        co_await bcos::ledger::getBlockNumber(view, payload.parentHash, bcos::ledger::fromStorage);
    bcos::protocol::BlockNumber parentBlockNumber = -1;
    bcos::protocol::BlockHeader::Ptr parentHeader;
    if (canonicalParentNumber.has_value())
    {
        parentBlockNumber = *canonicalParentNumber;
        const auto parentNumberStr = boost::lexical_cast<std::string>(*canonicalParentNumber);
        auto parentHeaderEntry = co_await storage2::readOne(
            view, executor_v1::StateKeyView{ledger::SYS_NUMBER_2_BLOCK_HEADER, parentNumberStr});
        if (!parentHeaderEntry.has_value())
        {
            // Parent hash already resolved and is canonical. Skipping timestamp / baseFee
            // here would accept a payload we cannot price — fail closed.
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("parent block header is missing from storage"));
        }
        const auto storedHeader = parentHeaderEntry->get();
        bcos::bytes parentHeaderBytes(storedHeader.begin(), storedHeader.end());
        parentHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader(parentHeaderBytes);
        if (parentHeader->number() != static_cast<int64_t>(*canonicalParentNumber))
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "stored parent block header height mismatch"});
        }
    }
    else if (auto importedParent = m_importedStore.get(payload.parentHash))
    {
        // Parent header BY HASH from the store — never NUMBER_2_BLOCK_HEADER[number]
        // (a same-height canonical sibling would masquerade as the parent).
        parentBlockNumber = importedParent->number;
        try
        {
            parentHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader(
                importedParent->headerBytes);
        }
        catch (const std::exception& e)
        {
            BOOST_THROW_EXCEPTION(
                OpExecutionInternalError{} << bcos::errinfo_comment{
                    std::string("imported parent block header is undecodable: ") + e.what()});
        }
    }
    else
    {
        co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    }

    if (payload.blockNumber != parentBlockNumber + 1)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("blockNumber must be exactly one greater than the parent's"));
    }

    if (static_cast<uint64_t>(payload.timestamp) <=
        static_cast<uint64_t>(parentHeader->timestamp()))
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("timestamp must be strictly greater than the parent's"));
    }

    {
        // The clock is shared with the FCU build (baseFeeClockFor). A Holocene
        // activation block still prices with the chain constants, because its parent
        // is pre-Holocene and the parent's fork is what selects the 1559 source.
        // PARENT time keys the choice (op-geth eip1559.go:64-110 CalcBaseFee on parent.Time).
        auto const expectedBaseFee =
            calcOpNextBlockBaseFee(*parentHeader, baseFeeClockFor(*parentHeader, ctx.forkId));
        if (payload.baseFeePerGas != expectedBaseFee)
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("baseFeePerGas does not match the value computed "
                            "from the parent"));
        }
    }

    // Idempotent replay: a hash that already landed — canonical or imported — is
    // VALID without re-execution (§4.5 同哈希已落下).
    if (auto knownBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, payload.blockHash, bcos::ledger::fromStorage);
        knownBlockNumber.has_value())
    {
        co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
    }
    if (m_importedStore.hasBlock(payload.blockHash))
    {
        co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
    }

    const auto childNumberStr = boost::lexical_cast<std::string>(payload.blockNumber);
    if (auto occupiedHeight = co_await storage2::readOne(
            view, executor_v1::StateKeyView{ledger::SYS_NUMBER_2_HASH, childNumberStr});
        occupiedHeight.has_value() && !canonicalParentNumber.has_value())
    {
        // Occupied height whose parent is IMPORTED: the imported slot already has
        // descendants — overwriting would orphan their state (§4.2 单分叉冲突) →
        // SYNCING. A CANONICAL parent (the ancestor-sibling case, Task 7) imports
        // fine: the block lands by hash and FCU decides canonicality later.
        co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    }

    // ONE plane decision for both parent flavours (canonical-below-tip ancestor sibling
    // and imported-parent chained import). The committed flat IS the parent plane
    // exactly when the parent is the canonical tip (canonicalTip == -1 means no
    // committed chain yet, where the committed plane is the genesis plane). Otherwise
    // the parent's materialized post-state is required, and its absence is SYNCING —
    // docs/2026-09-09-s3-engine-api-versions-design.md §4.5's "hasState failed" row —
    // never a wrong-plane execution on the committed flat.
    //
    // Deliberate divergence from op-geth: op-geth answers ACCEPTED when the parent block
    // is known but its state is not (eth/catalyst/api.go); this lane answers SYNCING,
    // because it holds no way to re-materialize a pruned parent plane and must not claim
    // it has accepted state it cannot validate. Answering ACCEPTED would require defining
    // latestValidHash semantics for that shape. Pinned by
    // OpEngineImportFcuTest/PrunedParentPlaneIsSyncingNotEmptyReExecute.
    // OPEN RISK: with no L2 derivation/sync path to rebuild the plane, a CL that waits for
    // the missing state will wait forever; see the campaign ledger's U6-F5 entry.
    auto const canonicalTip =
        co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
    std::vector<bcos::protocol::BlockHeader::Ptr> parentHeaders;
    std::shared_ptr<void> parentFlat;
    const bool parentIsCanonicalTip =
        canonicalParentNumber.has_value() &&
        (canonicalTip == -1 || *canonicalParentNumber == canonicalTip);
    if (!parentIsCanonicalTip)
    {
        auto parent = m_importedStore.get(payload.parentHash);
        if (!m_importedStore.hasState(payload.parentHash) || !parent.has_value() ||
            parent->postStateFlat == nullptr)
        {
            BCOS_LOG(INFO) << LOG_BADGE("OP_ENGINE")
                           << "SYNCING: parent post-state plane unavailable at height "
                           << (payload.blockNumber - 1);
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
        parentFlat = parent->postStateFlat;
        parentHeaders.push_back(
            m_blockFactory->blockHeaderFactory()->createBlockHeader(parent->headerBytes));
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

    // S5: import, not commit. The import plane is the PARENT's post-state: committed
    // flat when the parent is the canonical tip, otherwise the parent's stored
    // materialized flat (canonical-ancestor sibling / chained import). No canonical
    // table is written (design §4.2 newPayload condition 1).
    bcos::Error::Ptr executeError;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    std::shared_ptr<void> blockDelta;
    std::shared_ptr<void> blockFlat;
    m_delegate->importExecute(block, parentHeaders, parentFlat,
        [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr header,
            std::shared_ptr<void> delta, std::shared_ptr<void> flat) {
            executeError = std::move(error);
            executedHeader = std::move(header);
            blockDelta = std::move(delta);
            blockFlat = std::move(flat);
        });
    if (executeError)
    {
        co_return mapDelegateError(*executeError, latestValidHash);
    }
    if (!executedHeader)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("execution returned no header"));
    }
    // From Canyon the header field set includes withdrawalsRoot (the same gate
    // rebuildOpEthHeader uses, OpEngineService.cpp), and its presence is stamped by this
    // node's scheduler, not the CL payload. A missing field there is a node-internal fault
    // (-32603), never a consensus INVALID the CL would discard.
    if (ctx.forkId >= OpForkId::Canyon && !executedHeader->withdrawalsRoot().has_value())
    {
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                  "executed header is missing withdrawalsRoot"});
    }
    // The announced header projects the payload per fork (pre-Canyon: absent → the seal's
    // present-zero sentinel; Canyon..Holocene: empty-trie root; Isthmus+: the payload's own
    // root) — the same presence-insensitive comparison the scheduler's verify arm makes
    // (OpScheduler::coExecuteBlock's withdrawalsRoot comparison). Comparing the raw wire
    // optional collapsed "absent" to zero, which can never equal the Canyon empty-trie seal
    // and rejected every real CL payload in the Canyon..Holocene window (F-B2-1).
    if (executedHeader->withdrawalsRoot().value_or(bcos::h256{}) !=
        ethHeader->withdrawalsRoot().value_or(bcos::h256{}))
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("withdrawalsRoot does not match the executed header"));
    }
    // The scheduler's verify arm is not on the import path; the engine owns the
    // commitment gate (design §4.2 newPayload VALID condition 2 — the full
    // mismatchedFieldOf set, gasUsed/logsBloom included). A mismatch is a payload
    // fault: INVALID + parent, and the block is NOT stored (§4.5).
    // (Merge note: engine-cutover's commitBlock-at-newPayload arm is not carried over —
    // this tree's newPayload only imports into the parent plane; the delegate commit is
    // owned by the FCU canonicalize batch, so committing here would commit every block
    // twice. The delegate's LedgerConfig stub concern is covered by the republish
    // notifier wiring, see engine/OpLedgerConfigRepublish.h.)
    if (auto mismatch = bcos::evm::engine::mismatchedFieldOf(
            commitmentsOfHeader(*executedHeader), commitmentsOfHeader(*ethHeader)))
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("commitment mismatch on field ") + *mismatch);
    }

    // Land by hash. No canonical key is written here — FCU owns SetCanonical.
    bcos::bytes importedHeaderBytes;
    ethHeader->encode(importedHeaderBytes);
    ImportedBlock imported{.hash = payload.blockHash,
        .parent = payload.parentHash,
        .number = payload.blockNumber,
        .headerBytes = std::move(importedHeaderBytes),
        .txs = {},
        .txHashes = {},
        .encodedTxs = {},
        .receipts = {},
        .txRecipients = {},
        .storageDelta = std::move(blockDelta),
        .postStateFlat = std::move(blockFlat),
        .detached = false};
    for (auto const& env : detail::rawEnvelopes(payload))
    {
        imported.txs.push_back(env);
        imported.txHashes.push_back(m_blockFactory->cryptoSuite()->hashImpl()->hash(env));
    }
    // Canonical-row payloads: tars-encoded transactions (SYS_HASH_2_TX) and encoded
    // receipts (SYS_HASH_2_RECEIPT) — importExecute attached the receipts to the
    // block (commitPersist convention), so capture them here for canonicalize.
    for (auto&& receiptView : block->receipts())
    {
        auto receipt = std::move(receiptView).toShared();
        bcos::bytes encoded;
        receipt->encode(encoded);
        imported.receipts.push_back(std::move(encoded));
    }
    for (auto txView : block->transactions())
    {
        auto tx = std::move(txView).toShared();
        bcos::bytes encoded;
        tx->encode(encoded);
        imported.encodedTxs.push_back(std::move(encoded));
        imported.txRecipients.emplace_back(tx->to());
    }
    // Same-height occupant check (§4.2 单分叉冲突 vs §4.3 ancestor sibling): an
    // imported-live occupant with descendants must not lose its ancestor; a
    // CANONICAL occupant may be shadowed — its descendants stay on the canonical
    // chain until FCU switches labels.
    // Design §4.2: the occupancy DECISION and the put must be one atomic step, but no
    // await may sit under a POSIX lock (resumption can move threads and make the
    // unlock UB — the file's standing rule). So: read the ledger unlocked, then take
    // the lock for the sync-only decide+put, re-checking that no competing import
    // landed on this height in between (a compare-and-set on the occupancy).
    auto const observedOccupant = m_importedStore.occupantAt(payload.blockNumber);
    bool occupantCanonical = false;
    if (observedOccupant.has_value() && *observedOccupant != payload.blockHash)
    {
        auto occNumber = co_await bcos::ledger::getBlockNumber(
            view, *observedOccupant, bcos::ledger::fromStorage);
        occupantCanonical = occNumber.has_value() && *occNumber == payload.blockNumber;
        if (!occupantCanonical)
        {
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
    }
    {
        std::lock_guard treeLock(m_importedTreeMutex);
        if (m_canonicalizeInFlight)
        {
            // A canonicalize batch is mid-flight (it does not hold the POSIX lock
            // across its awaits, review F5): the tree is not interruptible, so fail
            // closed exactly like a competing import. CL retries.
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
        if (m_importedStore.occupantAt(payload.blockNumber) != observedOccupant)
        {
            // Another thread imported at this height while we were reading the ledger;
            // its occupant's canonicality is unknown here → retry-safe SYNCING.
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
        if (!m_importedStore.put(std::move(imported), occupantCanonical))
        {
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
    }

    {
        std::lock_guard lock(m_lastExecutedHeaderMutex);
        m_lastExecutedHeader = executedHeader;
    }
    co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
void OpEngineService<MemPoolType, GlobalStateStorageType,
    SchedulerType>::pruneFlatsAtOrBelowFinalized()
{
    // Memory bound (review F4): a block at/below the finalized marker can never be the
    // parent plane of a NEW import (op-node's promoteFinalized makes it irreversible),
    // so its materialized flat is dead weight. Absent a finalized marker the live
    // window is (tip .. tip] plus the unfinalized suffix — unbounded until the CL
    // finalizes, which the design's no-prune milestone accepted.
    if (auto finalized = m_tracker.finalizedBlockNumber(); finalized.has_value())
    {
        m_importedStore.pruneFlatsAtOrBelow(*finalized);
    }
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
template <class BackendType>
task::Task<void>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::recordCanonicalizeUndo(
    BackendType& backend, std::vector<CanonicalizeUndoRow>& undo,
    std::unordered_set<executor_v1::StateKey>& seen, executor_v1::StateKeyView key)
{
    if (!seen.emplace(key.m_table, key.m_key).second)
    {
        co_return;
    }
    auto prior = co_await storage2::readOne(backend, key);
    CanonicalizeUndoRow row;
    row.key = executor_v1::StateKey(key);
    if (prior.has_value())
    {
        row.prior.emplace(*prior);
    }
    // The cache layer is written by the same merges but read first, so it needs its own
    // prior (review F3). No-op on a cache-less composition.
    row.cachePrior = co_await m_globalStateStorage.readCacheLayer(row.key);
    undo.push_back(std::move(row));
    co_return;
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
template <class BackendType>
task::Task<void>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::rollbackCanonicalize(
    BackendType& backend, std::vector<CanonicalizeUndoRow> const& undo)
{
    // Reverse order is not required (each key is journaled once), but keeps the undo
    // symmetric with the write order for readability.
    for (auto it = undo.rbegin(); it != undo.rend(); ++it)
    {
        // substr on the string_view (not the string) keeps the views referencing the
        // journaled key for the whole iteration — temporary std::string views would dangle.
        auto const tableAndKey = std::string_view(it->key.m_tableAndKey);
        auto const view = executor_v1::StateKeyView(
            tableAndKey.substr(0, it->key.m_split), tableAndKey.substr(it->key.m_split + 1));
        if (it->prior.has_value())
        {
            co_await storage2::writeOne(backend, view, bcos::storage::Entry(*it->prior));
        }
        else
        {
            co_await storage2::removeOne(backend, view);
        }
        // Restore the cache-first read plane too (review F3): every merge this batch
        // performed reached backend + cache, so a backend-only rollback would leave the
        // cache holding de-canonicalized state. No-op without a cache layer.
        co_await m_globalStateStorage.writeCacheLayer(it->key, it->cachePrior);
    }
    co_return;
}

/// Re-materialize the head block's full post-state for a switch-SetCanonical whose own
/// flat was pruned (a re-org BACK onto an abandoned branch: pruneFlatsAbove released the
/// head's flat while the block stayed hash-addressable, review NEW-2). Copies the
/// deepest chain block that still carries a materialized flat, then replays the
/// remaining chain blocks' storage deltas in order — Entry rows write, DELETED
/// tombstones erase (a delta's surviving tombstones are exactly the rows that died
/// between the import-time committed plane and the block's parent flat). This is the
/// milestone's flat/delta stand-in for design §4.4.5's body replay. Returns nullptr when
/// NO chain block carries a flat — the caller must fail (缺 body, 禁止盲 rewind).
template <class MutableStorageT>
task::Task<std::shared_ptr<MutableStorageT>> reconstructSwitchFlat(
    std::vector<ImportedBlock> const& chain)
{
    std::size_t base = chain.size();
    for (std::size_t i = chain.size(); i-- > 0;)
    {
        if (chain[i].postStateFlat != nullptr)
        {
            base = i;
            break;
        }
    }
    if (base == chain.size())
    {
        co_return nullptr;
    }
    auto world = std::make_shared<MutableStorageT>();
    {
        auto baseFlat = std::static_pointer_cast<MutableStorageT>(chain[base].postStateFlat);
        auto flatIterator = co_await baseFlat->range();
        while (true)
        {
            auto item = co_await flatIterator.next();
            if (!item.has_value())
            {
                break;
            }
            auto& [stateKeyRef, valueVariant] = *item;
            if (auto* entry = std::get_if<bcos::storage::Entry>(&valueVariant))
            {
                co_await storage2::writeOne(
                    *world, executor_v1::StateKey(stateKeyRef.m_tableAndKey), *entry);
            }
        }
    }
    for (std::size_t i = base + 1; i < chain.size(); ++i)
    {
        auto delta = std::static_pointer_cast<MutableStorageT>(chain[i].storageDelta);
        if (!delta)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "canonicalize switch: replayed chain block has no "
                                      "storage delta"});
        }
        auto deltaIterator = co_await delta->range();
        while (true)
        {
            auto item = co_await deltaIterator.next();
            if (!item.has_value())
            {
                break;
            }
            auto& [stateKeyRef, valueVariant] = *item;
            if (auto* entry = std::get_if<bcos::storage::Entry>(&valueVariant))
            {
                co_await storage2::writeOne(
                    *world, executor_v1::StateKey(stateKeyRef.m_tableAndKey), *entry);
            }
            else if (std::get_if<bcos::storage2::DELETED_TYPE>(&valueVariant) != nullptr)
            {
                co_await storage2::removeOne(
                    *world, executor_v1::StateKey(stateKeyRef.m_tableAndKey));
            }
        }
    }
    co_return world;
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<void>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::canonicalizeImportedHead(
    const h256& headHash)
{
    namespace detail = bcos::evm::engine::detail;
    using MutableStorageT = typename GlobalStateStorageType::MutableStorage;
    // Design §4.2 engine exclusion over canonicalize. A POSIX mutex must not span the
    // awaited storage body — task::syncWait can resume the coroutine on another thread
    // (libtask/bcos-task/Wait.h) and the unlock would cross threads. The lock therefore
    // covers only this sync entry section and the sync exit section at the end;
    // m_canonicalizeInFlight, guarded by the same lock, is what excludes a concurrent
    // import/canonicalize across the awaits. Fails CLOSED when a batch is already in
    // flight rather than queueing behind it.
    {
        std::lock_guard gate(m_importedTreeMutex);
        if (m_canonicalizeInFlight)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "canonicalize: another import/canonicalize is in flight"});
        }
        m_canonicalizeInFlight = true;
    }

    // Undo journal (review F3): the batch mutates the backend incrementally (per-block
    // merge or whole-plane replacement). Record every key's prior value before touching
    // it, so ANY failure — a null delta mid-chain, a merge error, or the state-root
    // post-condition — restores the backend to its pre-call rows instead of leaving a
    // half-written plane. Not a general journal: scoped to this call. Declared before
    // the try so the catch-time rollback can see it.
    std::vector<CanonicalizeUndoRow> undo;
    std::unordered_set<executor_v1::StateKey> undoSeen;
    auto& backend = m_globalStateStorage.m_latestBackend;
    bcos::protocol::BlockNumber newHeadNumber = -1;
    bcos::h256 newHeadHash{};

    std::exception_ptr canonicalizeFailure;
    try
    {
        // Collect the chain head → ... → child-of-canonical (store walk); the parent is
        // canonical when HASH_2_NUMBER resolves it (import never writes that key).
        std::vector<ImportedBlock> chain;  // genesis-side first after the reverse below
        // The canonical height of the chain root's parent (the walk's break condition).
        // This is the forward-vs-switch discriminator (review NEW-2): forward canonicalize
        // is legal only when the root's canonical parent IS the current tip.
        std::optional<bcos::protocol::BlockNumber> rootParentNumber;
        {
            auto view = m_globalStateStorage.fork();
            auto cursor = m_importedStore.get(headHash);
            if (!cursor.has_value())
            {
                BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                          "canonicalize: head is not in the ImportedStore"});
            }
            while (true)
            {
                chain.push_back(*cursor);
                auto const parentNumber = co_await bcos::ledger::getBlockNumber(
                    view, cursor->parent, bcos::ledger::fromStorage);
                if (parentNumber.has_value())
                {
                    rootParentNumber = parentNumber;
                    break;
                }
                auto parent = m_importedStore.get(cursor->parent);
                if (!parent.has_value())
                {
                    // A switch/reorg SetCanonical (head not a linear extension of the
                    // canonical tip) needs the §4.4.5 replay — not the forward merge.
                    BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                              "canonicalize: imported chain does not root in the "
                                              "canonical chain (switch case is Task 7)"});
                }
                cursor = parent;
            }
        }
        std::reverse(chain.begin(), chain.end());

        auto viewForTip = m_globalStateStorage.forkCommitted();
        auto const currentTip =
            co_await bcos::ledger::getCurrentBlockNumber(viewForTip, bcos::ledger::fromStorage);

        // SWITCH detection (design §4.4.5): the forward per-block merge is legal only
        // when the chain roots AT the current canonical tip — the root's canonical
        // parent IS the tip block. A chain rooted below the tip must take the
        // whole-plane replacement path. That covers the head at/below the tip
        // (same-height switch or rollback) AND a divergent root: a re-org BACK onto an
        // abandoned branch (C → B′ → back to C) walks B-C with A canonical at 1 while
        // the tip is B′@2 — classifying it as forward would merge the deltas onto the
        // WRONG plane (review NEW-2).
        bool const rootsAtCanonicalTip =
            currentTip != -1 && rootParentNumber.has_value() && *rootParentNumber == currentTip;
        if (currentTip != -1 && !rootsAtCanonicalTip)
        {
            auto const headBlock = chain.back();
            // The head's materialized post-state flat, used directly when it survived
            // the flat pruning. A re-org BACK onto an abandoned branch has its head flat
            // released by pruneFlatsAbove (the block stays hash-addressable), so the
            // world is re-materialized from the deepest surviving ancestor flat plus the
            // remaining chain deltas — this milestone's flat/delta stand-in for design
            // §4.4.5's replay (缺 body → 失败 below, 禁止盲 rewind).
            std::shared_ptr<typename GlobalStateStorageType::MutableStorage> flat;
            if (headBlock.postStateFlat != nullptr)
            {
                flat = std::static_pointer_cast<typename GlobalStateStorageType::MutableStorage>(
                    headBlock.postStateFlat);
            }
            else
            {
                flat =
                    co_await reconstructSwitchFlat<typename GlobalStateStorageType::MutableStorage>(
                        chain);
            }
            if (!flat)
            {
                BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                          "canonicalize switch: head has no post-state flat and "
                                          "no replayable ancestor flat"});
            }

            // The switch's whole-plane replacement is staged into ONE delta and applied
            // through a single mergeToBackends at the end (review NEW-3): the merge
            // dual-writes the latest backend AND the process-lifetime cache layer from
            // the same source of truth, so fork()/forkCommitted() readers — including
            // the §4.2 verifyCanonicalStateRoot post-condition — see the new plane
            // coherently (the production cache is process-lifetime and read FIRST).
            // Nothing mutates either layer before that commit point, so a staging
            // failure is observably free; the undo journal (one pass over the staged
            // delta, recorded while both layers still hold their pre-call values) covers
            // a failure at/after the commit — the post-condition — by restoring BOTH
            // layers (review F3's guarantee, now uniform with the forward branch).
            typename GlobalStateStorageType::MutableStorage stagedDelta;

            // (1) Stage tombstones for backend STATE-plane rows absent from the head flat
            // (C-era accounts / storage slots). Ledger metadata is NOT in the flat (the
            // flat is materialized at import, before SetCanonical writes its canonical
            // rows), so scrubbing it here was deleting still-canonical ancestors'
            // hash-keyed rows (review N2); those tables are trimmed by height in step (3)
            // instead. The scan reads the backend, which is untouched until the commit.
            {
                // The collection iterator is scoped: MemoryStorage's range() holds the
                // storage lock for the iterator's lifetime, so mutating the same storage
                // before it is destroyed self-deadlocks on that lock.
                std::vector<executor_v1::StateKey> doomed;
                {
                    auto backendIterator = co_await backend.range();
                    while (true)
                    {
                        auto item = co_await backendIterator.next();
                        if (!item.has_value())
                        {
                            break;
                        }
                        auto const& backendKey = std::get<0>(*item);
                        auto const tableAndKey = std::string_view(backendKey.m_tableAndKey);
                        if (isLedgerCanonicalMetadataTable(
                                tableAndKey.substr(0, backendKey.m_split)))
                        {
                            continue;
                        }
                        auto present = co_await storage2::readOne(*flat,
                            executor_v1::StateKeyView(tableAndKey.substr(0, backendKey.m_split),
                                tableAndKey.substr(backendKey.m_split + 1)));
                        if (!present.has_value())
                        {
                            doomed.push_back(executor_v1::StateKey(backendKey.m_tableAndKey));
                        }
                    }
                }
                for (auto const& key : doomed)
                {
                    auto const tableAndKey = std::string_view(key.m_tableAndKey);
                    auto const keyView = executor_v1::StateKeyView(
                        tableAndKey.substr(0, key.m_split), tableAndKey.substr(key.m_split + 1));
                    co_await storage2::removeOne(stagedDelta, keyView);
                }
            }

            // (2) Stage every head-flat row (the replacement plane's live values).
            {
                auto flatIterator = co_await flat->range();
                while (true)
                {
                    auto item = co_await flatIterator.next();
                    if (!item.has_value())
                    {
                        break;
                    }
                    auto& [stateKeyRef, valueVariant] = *item;
                    if (auto* entry = std::get_if<bcos::storage::Entry>(&valueVariant))
                    {
                        co_await storage2::writeOne(stagedDelta,
                            executor_v1::StateKey(stateKeyRef.m_tableAndKey), std::move(*entry));
                    }
                }
            }

            // (2.5) Design §4.2: 沿新链每一高度写满 canonical rows. A switch whose chain
            // carries more than one non-canonical block (the NEW-2 back-reorg: C → B′ →
            // back to C walks B-C) must re-canonicalize EVERY height above the fork
            // point, not just the head — the B′ switch overwrote/removed the
            // intermediate heights' rows. For a single-block chain (same-height switch /
            // rollback) the loop is empty and the staged rows are exactly the head set.
            for (std::size_t i = 0; i + 1 < chain.size(); ++i)
            {
                auto const& block = chain[i];
                // The height's CURRENT canonical occupant (read from the untouched backend)
                // may be a replaced sibling — the B′ the first switch installed, in the
                // back-reorg shape. Its hash→number row and the height's nonces must be
                // tombstoned together with the overwrite below, or the de-canonicalized
                // block still resolves by hash onto the NEW chain's height (review NEW-1's
                // rule, generalized to every re-canonicalized chain height).
                auto const blockNumberStr = std::to_string(block.number);
                auto const occupantEntry = co_await storage2::readOne(backend,
                    executor_v1::StateKeyView(bcos::ledger::SYS_NUMBER_2_HASH, blockNumberStr));
                if (occupantEntry.has_value())
                {
                    auto const occupantBytes = occupantEntry->get();
                    if (occupantBytes.size() == bcos::crypto::HashType::SIZE)
                    {
                        bcos::crypto::HashType const occupant(
                            occupantBytes, bcos::crypto::HashType::FromBinary);
                        if (occupant != block.hash)
                        {
                            co_await storage2::removeOne(
                                stagedDelta, executor_v1::StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                                                 bcos::concepts::bytebuffer::toView(occupant)});
                            co_await storage2::removeOne(stagedDelta,
                                executor_v1::StateKey{
                                    bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES, blockNumberStr});
                        }
                    }
                }
                bcos::storage::Entry numberEntry;
                numberEntry.set(std::to_string(block.number));
                co_await storage2::writeOne(stagedDelta,
                    executor_v1::StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                        bcos::concepts::bytebuffer::toView(block.hash)},
                    std::move(numberEntry));
                bcos::storage::Entry hashEntry;
                hashEntry.set(block.hash.asBytes());
                co_await storage2::writeOne(stagedDelta,
                    executor_v1::StateKey{
                        bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(block.number)},
                    std::move(hashEntry));
                bcos::storage::Entry headerEntry;
                headerEntry.set(block.headerBytes);
                co_await storage2::writeOne(stagedDelta,
                    executor_v1::StateKey{
                        bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(block.number)},
                    std::move(headerEntry));
                // SYS_NUMBER_2_TXS[number]: the by-number tx list (review N1).
                bcos::storage::Entry numberToTxsEntry;
                numberToTxsEntry.set(
                    encodeNumberToTxsRow(*m_blockFactory, block.txHashes, block.txRecipients));
                co_await storage2::writeOne(stagedDelta,
                    executor_v1::StateKey{
                        bcos::ledger::SYS_NUMBER_2_TXS, std::to_string(block.number)},
                    std::move(numberToTxsEntry));
                // Hash-keyed bodies (SYS_HASH_2_TX / SYS_HASH_2_RECEIPT): a switch never
                // REMOVES them, but that only helps heights that were canonical before.
                // An intermediate height that was never canonical (the NEW-2 back-reorg
                // walks a chain that was imported but never pinned) has no body rows yet,
                // so the by-number row staged above would list tx hashes with no
                // resolvable body and ledger::getBlockData's batch get would fail the
                // canonical block. Stage them per height, mirroring the trim loop in
                // canonicalizeImportedHead, canonicalizeImportedHead's forward branch,
                // and the head step (3) below.
                for (std::size_t j = 0; j < block.encodedTxs.size(); ++j)
                {
                    bcos::storage::Entry txEntry;
                    txEntry.set(block.encodedTxs[j]);
                    co_await storage2::writeOne(stagedDelta,
                        executor_v1::StateKey{bcos::ledger::SYS_HASH_2_TX,
                            bcos::concepts::bytebuffer::toView(block.txHashes[j])},
                        std::move(txEntry));
                    if (j < block.receipts.size())
                    {
                        bcos::storage::Entry receiptEntry;
                        receiptEntry.set(block.receipts[j]);
                        co_await storage2::writeOne(stagedDelta,
                            executor_v1::StateKey{bcos::ledger::SYS_HASH_2_RECEIPT,
                                bcos::concepts::bytebuffer::toView(block.txHashes[j])},
                            std::move(receiptEntry));
                    }
                }
            }

            // (3) Canonical rows for the new head + explicit height trim above it. The
            // ancestors at/below the fork point keep their rows (step (1) left metadata
            // alone); heights at/above the new head are de-canonicalized and lose their
            // number mappings. Bodies stay hash-addressable (SYS_HASH_2_TX /
            // SYS_HASH_2_RECEIPT untouched, design §4.2).
            bcos::storage::Entry headNumberEntry;
            auto const headNumberStr = std::to_string(headBlock.number);
            // Same-height switch (the design §5 mandatory L1 reorg A-B-C → B'@2): the
            // block replaced at head.number must be de-canonicalized together with the
            // heights above it. Capture its hash BEFORE the NUMBER_2_HASH[head] overwrite
            // below; its HASH_2_NUMBER then has to go too, or eth_getBlockByHash(oldSibling)
            // resolves hash→number→the NEW head (EthEndpoint detour) and handleOpNewPayload
            // misclassifies the orphan as the canonical tip (review NEW-1). The read is
            // against the untouched backend: the staged delta is not applied yet.
            std::optional<bcos::crypto::HashType> replacedHeadHash;
            {
                auto const oldHeadHashEntry = co_await storage2::readOne(backend,
                    executor_v1::StateKeyView(bcos::ledger::SYS_NUMBER_2_HASH, headNumberStr));
                if (oldHeadHashEntry.has_value())
                {
                    auto const oldHeadHashBytes = oldHeadHashEntry->get();
                    if (oldHeadHashBytes.size() == bcos::crypto::HashType::SIZE)
                    {
                        replacedHeadHash.emplace(
                            oldHeadHashBytes, bcos::crypto::HashType::FromBinary);
                    }
                }
            }
            auto const headHashKeyView = executor_v1::StateKeyView(bcos::ledger::SYS_HASH_2_NUMBER,
                bcos::concepts::bytebuffer::toView(headBlock.hash));
            headNumberEntry.set(headNumberStr);
            co_await storage2::writeOne(
                stagedDelta, executor_v1::StateKey(headHashKeyView), std::move(headNumberEntry));
            auto const numberHashKeyView =
                executor_v1::StateKeyView(bcos::ledger::SYS_NUMBER_2_HASH, headNumberStr);
            bcos::storage::Entry hashEntry;
            hashEntry.set(headBlock.hash.asBytes());
            co_await storage2::writeOne(
                stagedDelta, executor_v1::StateKey(numberHashKeyView), std::move(hashEntry));
            auto const headerKeyView =
                executor_v1::StateKeyView(bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, headNumberStr);
            bcos::storage::Entry headerEntry;
            headerEntry.set(headBlock.headerBytes);
            co_await storage2::writeOne(
                stagedDelta, executor_v1::StateKey(headerKeyView), std::move(headerEntry));
            auto const currentKeyView = executor_v1::StateKeyView(
                bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER);
            bcos::storage::Entry numberEntry;
            numberEntry.set(headNumberStr);
            co_await storage2::writeOne(
                stagedDelta, executor_v1::StateKey(currentKeyView), std::move(numberEntry));
            for (std::size_t i = 0; i < headBlock.encodedTxs.size(); ++i)
            {
                auto const txHashView = bcos::concepts::bytebuffer::toView(headBlock.txHashes[i]);
                auto const txKeyView =
                    executor_v1::StateKeyView(bcos::ledger::SYS_HASH_2_TX, txHashView);
                bcos::storage::Entry txEntry;
                txEntry.set(headBlock.encodedTxs[i]);
                co_await storage2::writeOne(
                    stagedDelta, executor_v1::StateKey(txKeyView), std::move(txEntry));
                if (i < headBlock.receipts.size())
                {
                    auto const receiptKeyView =
                        executor_v1::StateKeyView(bcos::ledger::SYS_HASH_2_RECEIPT, txHashView);
                    bcos::storage::Entry receiptEntry;
                    receiptEntry.set(headBlock.receipts[i]);
                    co_await storage2::writeOne(stagedDelta, executor_v1::StateKey(receiptKeyView),
                        std::move(receiptEntry));
                }
            }
            // SYS_NUMBER_2_TXS[number]: the by-number tx list ledger::getBlockData reads
            // before resolving SYS_HASH_2_TX. Without it eth_getBlockByNumber returns the
            // canonical imported block with no transactions (review N1).
            {
                auto const numberToTxsKeyView =
                    executor_v1::StateKeyView(bcos::ledger::SYS_NUMBER_2_TXS, headNumberStr);
                bcos::storage::Entry numberToTxsEntry;
                numberToTxsEntry.set(encodeNumberToTxsRow(
                    *m_blockFactory, headBlock.txHashes, headBlock.txRecipients));
                co_await storage2::writeOne(stagedDelta, executor_v1::StateKey(numberToTxsKeyView),
                    std::move(numberToTxsEntry));
            }
            // Same-height replacement: drop the replaced block's hash→number mapping and
            // its height's nonces (never rewritten by this branch). The trim loop below
            // starts above the head, so this height is handled here — but only via the
            // captured old hash, never by re-reading NUMBER_2_HASH[head] (already the new
            // head by now; that would delete the new head's own mapping).
            if (replacedHeadHash.has_value() && *replacedHeadHash != headBlock.hash)
            {
                auto const replacedHashNumberView =
                    executor_v1::StateKeyView(bcos::ledger::SYS_HASH_2_NUMBER,
                        bcos::concepts::bytebuffer::toView(*replacedHeadHash));
                co_await storage2::removeOne(stagedDelta, replacedHashNumberView);
                auto const headNoncesView = executor_v1::StateKeyView(
                    bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES, headNumberStr);
                co_await storage2::removeOne(stagedDelta, headNoncesView);
            }
            for (auto k = headBlock.number + 1; k <= currentTip; ++k)
            {
                auto const numberStr = std::to_string(k);
                // Capture this height's OLD canonical hash before its number->hash row goes:
                // the block is no longer canonical, so its hash->number entry must not
                // outlive the mapping (it would resolve a de-canonicalized block).
                auto const oldHashEntry = co_await storage2::readOne(
                    backend, executor_v1::StateKeyView(bcos::ledger::SYS_NUMBER_2_HASH, numberStr));
                auto const numberHashOldView =
                    executor_v1::StateKeyView(bcos::ledger::SYS_NUMBER_2_HASH, numberStr);
                co_await storage2::removeOne(stagedDelta, numberHashOldView);
                auto const headerOldView =
                    executor_v1::StateKeyView(bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, numberStr);
                co_await storage2::removeOne(stagedDelta, headerOldView);
                auto const txsOldView =
                    executor_v1::StateKeyView(bcos::ledger::SYS_NUMBER_2_TXS, numberStr);
                co_await storage2::removeOne(stagedDelta, txsOldView);
                auto const noncesOldView =
                    executor_v1::StateKeyView(bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES, numberStr);
                co_await storage2::removeOne(stagedDelta, noncesOldView);
                if (oldHashEntry.has_value())
                {
                    auto const oldHashBytes = oldHashEntry->get();
                    if (oldHashBytes.size() == bcos::crypto::HashType::SIZE)
                    {
                        bcos::crypto::HashType const oldHash(
                            oldHashBytes, bcos::crypto::HashType::FromBinary);
                        auto const oldHashNumberView =
                            executor_v1::StateKeyView(bcos::ledger::SYS_HASH_2_NUMBER,
                                bcos::concepts::bytebuffer::toView(oldHash));
                        co_await storage2::removeOne(stagedDelta, oldHashNumberView);
                    }
                }
            }

            // Journal every staged key (backend AND cache prior, first occurrence) while
            // both layers still hold their pre-call values, then COMMIT the whole-plane
            // replacement: one mergeToBackends applies the staged delta to the latest
            // backend and the cache from the same source (review NEW-3). Tombstoned keys
            // erase in both layers — MemoryStorage's merge writes the DELETED marker
            // through, and a non-logical-deletion target erases the row physically.
            {
                auto stagedIterator = co_await stagedDelta.range();
                while (true)
                {
                    auto item = co_await stagedIterator.next();
                    if (!item.has_value())
                    {
                        break;
                    }
                    co_await recordCanonicalizeUndo(
                        backend, undo, undoSeen, executor_v1::StateKeyView(std::get<0>(*item)));
                }
            }
            co_await m_globalStateStorage.mergeToBackends(stagedDelta);

            // Design §4.2 post-condition: the relabelled tip must really be backed by the
            // head's world state — this is the check that catches a stale/partial plane.
            if (m_delegate)
            {
                auto headHeader =
                    m_blockFactory->blockHeaderFactory()->createBlockHeader(headBlock.headerBytes);
                m_delegate->verifyCanonicalStateRoot(headHeader->stateRoot());
                m_delegate->canonicalizedTo(headBlock.number);
            }
            newHeadNumber = headBlock.number;
            newHeadHash = headBlock.hash;
        }
        else
        {
            for (auto& block : chain)
            {
                auto delta = std::static_pointer_cast<MutableStorageT>(block.storageDelta);
                if (!delta)
                {
                    BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                              "canonicalize: imported block has no storage delta"});
                }
                auto header =
                    m_blockFactory->blockHeaderFactory()->createBlockHeader(block.headerBytes);
                if (header->number() != block.number)
                {
                    BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                              "canonicalize: stored header height mismatch"});
                }

                // This height's canonical keys ride the SAME merge as the block's delta
                // (一块一配): HASH_2_NUMBER / NUMBER_2_HASH / NUMBER_2_BLOCK_HEADER, plus
                // SYS_CURRENT_STATE on the head's own merge.
                bcos::storage::Entry numberEntry;
                numberEntry.set(std::to_string(block.number));
                co_await storage2::writeOne(*delta,
                    executor_v1::StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                        bcos::concepts::bytebuffer::toView(block.hash)},
                    std::move(numberEntry));
                bcos::storage::Entry hashEntry;
                hashEntry.set(block.hash.asBytes());
                co_await storage2::writeOne(*delta,
                    executor_v1::StateKey{
                        bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(block.number)},
                    std::move(hashEntry));
                bcos::storage::Entry headerEntry;
                headerEntry.set(block.headerBytes);
                co_await storage2::writeOne(*delta,
                    executor_v1::StateKey{
                        bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(block.number)},
                    std::move(headerEntry));
                // Canonical tx/receipt rows (prewriteBlockToBuffer's phase-2 equivalent,
                // writeNonces=false): keyed by tx hash, index-aligned with the block's txs.
                for (std::size_t i = 0; i < block.encodedTxs.size(); ++i)
                {
                    bcos::storage::Entry txEntry;
                    txEntry.set(block.encodedTxs[i]);
                    co_await storage2::writeOne(*delta,
                        executor_v1::StateKey{bcos::ledger::SYS_HASH_2_TX,
                            bcos::concepts::bytebuffer::toView(block.txHashes[i])},
                        std::move(txEntry));
                    if (i < block.receipts.size())
                    {
                        bcos::storage::Entry receiptEntry;
                        receiptEntry.set(block.receipts[i]);
                        co_await storage2::writeOne(*delta,
                            executor_v1::StateKey{bcos::ledger::SYS_HASH_2_RECEIPT,
                                bcos::concepts::bytebuffer::toView(block.txHashes[i])},
                            std::move(receiptEntry));
                    }
                }
                // SYS_NUMBER_2_TXS[number]: mirrors the ledger's prewrite row so the block is
                // retrievable by number after the FCU canonicalizes it (review N1).
                {
                    bcos::storage::Entry numberToTxsEntry;
                    numberToTxsEntry.set(
                        encodeNumberToTxsRow(*m_blockFactory, block.txHashes, block.txRecipients));
                    co_await storage2::writeOne(*delta,
                        executor_v1::StateKey{
                            bcos::ledger::SYS_NUMBER_2_TXS, std::to_string(block.number)},
                        std::move(numberToTxsEntry));
                }
                if (block.hash == headHash)
                {
                    bcos::storage::Entry currentEntry;
                    currentEntry.set(std::to_string(block.number));
                    co_await storage2::writeOne(*delta,
                        executor_v1::StateKey{
                            bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
                        std::move(currentEntry));
                }
                // Journal every key this block's merge will write before touching the backend, so
                // a failure in a later block (or the post-condition) undoes this block too
                // (review F3).
                {
                    auto deltaIterator = co_await delta->range();
                    while (true)
                    {
                        auto item = co_await deltaIterator.next();
                        if (!item.has_value())
                        {
                            break;
                        }
                        co_await recordCanonicalizeUndo(
                            backend, undo, undoSeen, executor_v1::StateKeyView(std::get<0>(*item)));
                    }
                }
                // The imported chain never occupies the MLS pending deque — mergeToBackends
                // (design §4.2: 不要对空 deque 调 mergeBackStorage).
                co_await m_globalStateStorage.mergeToBackends(*delta);
            }

            if (m_delegate)
            {
                auto headHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader(
                    chain.back().headerBytes);
                m_delegate->verifyCanonicalStateRoot(headHeader->stateRoot());
                m_delegate->canonicalizedTo(chain.back().number);
            }
            newHeadNumber = chain.back().number;
            newHeadHash = chain.back().hash;
        }
    }
    catch (...)
    {
        // co_await is not permitted in a catch handler; save the failure, restore the
        // backend observably, then rethrow the original exception.
        canonicalizeFailure = std::current_exception();
    }
    if (canonicalizeFailure)
    {
        // Restore, then release the gate (a rollback failure must not mask the original
        // fault nor leave the tree permanently uninterruptible).
        try
        {
            co_await rollbackCanonicalize(backend, undo);
        }
        catch (...)
        {}
        {
            std::lock_guard gate(m_importedTreeMutex);
            m_canonicalizeInFlight = false;
        }
        std::rethrow_exception(canonicalizeFailure);
    }

    {
        // Sync exit section: no await may sit under the lock. adopt + prune are
        // memory-only (ImportedStore's own mutex) and stay serialized with an import's
        // put by the same gate.
        std::lock_guard gate(m_importedTreeMutex);
        m_importedStore.adoptCanonicalHead(newHeadNumber, newHeadHash);
        m_importedStore.pruneFlatsAbove(newHeadNumber);
        pruneFlatsAtOrBelowFinalized();
        m_canonicalizeInFlight = false;
    }
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
