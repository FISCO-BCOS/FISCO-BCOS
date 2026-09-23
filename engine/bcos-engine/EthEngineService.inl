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
 * @file EthEngineService.inl
 * @brief Ethereum Engine API service implementation (template bodies)
 */

#pragma once

#include "EngineStorageCommit.h"
#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/indirect.hpp>
#include <range/v3/view/transform.hpp>

#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/protocol/BlobSchedule.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>
#include <bcos-rlp-protocol/EthWithdrawal.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <limits>
#include <optional>
#include <unordered_set>

namespace bcos::engine
{

template <class MemPoolType, class GlobalStateStorageType, class ExecutorType, class SchedulerType>
    requires executor_v1::TransactionExecutor<ExecutorType,
                 typename GlobalStateStorageType::ViewType> &&
             scheduler_v1::TransactionScheduler<SchedulerType,
                 typename GlobalStateStorageType::ViewType, ExecutorType,
                 std::vector<protocol::Transaction::Ptr>>
task::Task<ForkchoiceUpdatedResult> EthEngineService<MemPoolType, GlobalStateStorageType,
    ExecutorType, SchedulerType>::updateForkchoice(const ForkchoiceState& forkchoiceState,
    const PayloadAttributes* payloadAttributes, std::uint32_t version)
{
    if (!isForkchoiceVersionSupported(version))
    {
        BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                              << bcos::errinfo_comment{"Unsupported Engine API version"});
    }
    if (m_clSync)
    {
        // The CL is alive and directing: latch CL-driven mode for the devp2p sync loop
        // (the latch never clears, so a CL disconnect cannot revive autonomous advance
        // and commit past the head a returning CL expects).
        m_clSync->noteForkchoiceServed();
    }
    std::vector<bcos::bytes> decodedForcedTxs;
    if (payloadAttributes != nullptr)
    {
        if (auto validationError = engine_common::validatePayloadAttributes(
                *payloadAttributes, version, &decodedForcedTxs,
                m_externalPayloadVerifier != nullptr);
            validationError.has_value())
        {
            co_return ForkchoiceUpdatedResult{
                .payloadStatus = engine_common::makeStatus(
                    PayloadValidationStatus::Invalid, std::nullopt, validationError),
                .payloadId = std::nullopt,
            };
        }
    }

    auto view = m_globalStateStorage.fork();
    auto headBlockNumber = co_await bcos::ledger::getBlockNumber(
        view, forkchoiceState.headBlockHash, bcos::ledger::fromStorage);
    // All-zero safe/finalized hashes are the Engine-API "not set" value: skip number
    // resolution and canonical checks for that field (op-geth SetSafe/SetFinalized are
    // only called for non-zero hashes). A missing HEAD is SYNCING; a non-zero
    // unresolvable safe/finalized is InvalidForkchoiceState (op-geth).
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
        detail::warnSyncingRateLimited<detail::c_forkchoiceHeadUnknown>(
            "forkchoice head unknown; answering SYNCING (backfill requested)",
            forkchoiceState.headBlockHash);
        if (m_clSync)
        {
            // The CL named a head this node has not committed: the devp2p backfiller
            // pulls toward it, and the CL's retry then resolves.
            m_clSync->requestBackfill(forkchoiceState.headBlockHash);
        }
        co_return ForkchoiceUpdatedResult{
            .payloadStatus = engine_common::makeStatus(
                PayloadValidationStatus::Syncing, std::nullopt, std::nullopt),
            .payloadId = std::nullopt,
        };
    }
    if ((safeSet && !safeBlockNumber.has_value()) ||
        (finalizedSet && !finalizedBlockNumber.has_value()))
    {
        BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                  "Forkchoice safe or finalized block is unknown"});
    }
    if (safeBlockNumber.has_value() && *safeBlockNumber > *headBlockNumber)
    {
        BOOST_THROW_EXCEPTION(
            InvalidForkchoiceState{} << bcos::errinfo_comment{
                "Forkchoice safe block number must not exceed head block number"});
    }
    if (finalizedBlockNumber.has_value() && *finalizedBlockNumber > *headBlockNumber)
    {
        BOOST_THROW_EXCEPTION(
            InvalidForkchoiceState{} << bcos::errinfo_comment{
                "Forkchoice finalized block number must not exceed head block number"});
    }
    if (finalizedBlockNumber.has_value() && safeBlockNumber.has_value() &&
        *finalizedBlockNumber > *safeBlockNumber)
    {
        BOOST_THROW_EXCEPTION(
            InvalidForkchoiceState{} << bcos::errinfo_comment{
                "Forkchoice finalized block number must not exceed safe block number"});
    }

    auto canonicalHeadHash =
        co_await bcos::ledger::getBlockHash(view, *headBlockNumber, bcos::ledger::fromStorage);
    bool const headCanonical =
        canonicalHeadHash.has_value() && *canonicalHeadHash == forkchoiceState.headBlockHash;
    // EL mode: the CL's head may lag the committed tip (the devp2p sync loop commits
    // blocks the CL has not voted on yet). Phase 3: rewind the committed chain to the CL's
    // head (EthereumChainRollback.h). headCanonical is guaranteed here — a rollback deletes
    // the orphan blocks' hash->number rows, so any head hash that RESOLVED above is
    // canonical. The tip read doubles as the build guard's ground truth below.
    bcos::protocol::BlockNumber ledgerTip = 0;
    if (m_externalPayloadVerifier)
    {
        ledgerTip = co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
        if (*headBlockNumber < ledgerTip)
        {
            // Never rewind below a finalized height the tracker already accepted: finality
            // is the CL's promise that the prefix is immutable.
            auto const storedFinalized = m_tracker.finalizedBlockNumber();
            if (storedFinalized.has_value() && *headBlockNumber < *storedFinalized)
            {
                BOOST_THROW_EXCEPTION(
                    InvalidForkchoiceState{} << bcos::errinfo_comment{
                        "forkchoice head is below the stored finalized block; refusing to "
                        "rewind finalized state"});
            }
            auto rollback = co_await m_externalPayloadVerifier->rollbackToCommitted(
                m_globalStateStorage, *headBlockNumber);
            if (!rollback.rolledBack)
            {
                // Beyond the reorg window (or a journal row missing): local state cannot
                // serve this head — the CL must backfill from the network, so answer
                // SYNCING (and log loud: on a healthy chain this never fires).
                BCOS_LOG(ERROR) << LOG_BADGE("EthEngineService")
                                << LOG_DESC("forkchoice head rollback refused; answering SYNCING")
                                << LOG_KV("head", forkchoiceState.headBlockHash.abridged())
                                << LOG_KV("headNumber", *headBlockNumber)
                                << LOG_KV("ledgerTip", ledgerTip)
                                << LOG_KV("reason", rollback.error);
                co_return ForkchoiceUpdatedResult{
                    .payloadStatus = engine_common::makeStatus(
                        PayloadValidationStatus::Syncing, std::nullopt, std::nullopt),
                    .payloadId = std::nullopt,
                };
            }
            BCOS_LOG(INFO) << LOG_BADGE("EthEngineService")
                           << LOG_DESC("forkchoice head behind the tip: chain rewound")
                           << LOG_KV("head", forkchoiceState.headBlockHash.abridged())
                           << LOG_KV("newTip", *headBlockNumber)
                           << LOG_KV("oldTip", ledgerTip);
            ledgerTip = *headBlockNumber;
        }
    }
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
        .allowCanonicalHeadJump = m_externalPayloadVerifier != nullptr,
    };
    if (m_tracker.applyForkchoice(resolved) == ForkchoiceApplyResult::Swallowed)
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus = engine_common::makeStatus(
                PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
            .payloadId = std::nullopt,
        };
    }

    ForkchoiceUpdatedResult result{
        .payloadStatus = engine_common::makeStatus(
            PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
        .payloadId = std::nullopt,
    };
    if (payloadAttributes == nullptr)
    {
        co_return result;
    }
    if (m_externalPayloadVerifier && *headBlockNumber != ledgerTip)
    {
        // EL mode: building requires the head to be the committed tip — the executor
        // runs on the latest state, so a payload parented to an older head would mint a
        // block whose stateRoot commits to the wrong parent state. The CL must let the
        // backfill catch up (or move its head to the tip) before asking for a payload.
        BOOST_THROW_EXCEPTION(InvalidPayloadAttributes{} << bcos::errinfo_comment{
                                  "forkchoice head is not the committed ledger tip; cannot "
                                  "build a payload on a non-tip head"});
    }

    // EL mode: resolve the L1 build context (parent Ethereum header + the base fee /
    // excess blob gas / header fork derived from it and the CL's timestamp) BEFORE
    // sealing, so the mempool can order the candidates by effective priority fee.
    std::optional<L1BuildInput> l1Input;
    if (m_externalPayloadVerifier)
    {
        // The Engine-API timestamp is seconds on the wire and milliseconds inside; an L1
        // header ticks in whole seconds, so a sub-second remainder cannot build a block.
        if (payloadAttributes->timestamp % 1000 != 0)
        {
            BOOST_THROW_EXCEPTION(
                InvalidPayloadAttributes{} << bcos::errinfo_comment{
                    "payloadAttributes.timestamp must be a whole number of seconds on the "
                    "L1 build path"});
        }
        auto parentBlock = co_await bcos::ledger::getBlockData(
            view, *headBlockNumber, bcos::ledger::HEADER, *m_blockFactory);
        l1Input.emplace();
        l1Input->parentHeader =
            bcos::protocol::EthBlockHeader(*parentBlock->blockHeader()).data();
        l1Input->l1Context = co_await m_externalPayloadVerifier->deriveL1Context(
            l1Input->parentHeader, payloadAttributes->timestamp / 1000);
    }

    std::vector<protocol::Transaction::Ptr> sealedTxs;
    view.newMutable();
    if (!payloadAttributes->noTxPool.value_or(false))
    {
        m_memPool.remove(view);
        if (l1Input.has_value())
        {
            m_memPool.seal(m_blockTxCountLimit, view, std::back_inserter(sealedTxs),
                l1Input->l1Context.baseFee);
        }
        else
        {
            m_memPool.seal(m_blockTxCountLimit, view, std::back_inserter(sealedTxs));
        }
    }

    if (l1Input.has_value() && !sealedTxs.empty())
    {
        // EL mode: a blob transaction whose EIP-4844 sidecar never arrived cannot be
        // assembled into a blobsBundle, so it is unbuildable — skip it, and with it the
        // rest of the sender's nonce suffix (the executor would reject the gap). The
        // transaction stays pooled; a later block picks it up once the sidecar lands.
        std::unordered_set<std::string> blobStalledSenders;
        std::vector<protocol::Transaction::Ptr> buildableTxs;
        buildableTxs.reserve(sealedTxs.size());
        for (auto& sealedTx : sealedTxs)
        {
            std::string sender{sealedTx->sender()};
            if (blobStalledSenders.contains(sender))
            {
                continue;
            }
            if (sealedTx->type() ==
                    static_cast<std::uint8_t>(protocol::TransactionType::Web3Transaction) &&
                !sealedTx->blobVersionedHashes().empty() &&
                !m_memPool.blobSidecar(sealedTx->hash()).has_value())
            {
                blobStalledSenders.insert(std::move(sender));
                BCOS_LOG(WARNING) << LOG_BADGE("EthEngineService")
                                  << LOG_DESC("seal: skipping blob transaction without sidecar")
                                  << LOG_KV("hash", sealedTx->hash().hexPrefixed());
                continue;
            }
            buildableTxs.push_back(std::move(sealedTx));
        }
        sealedTxs = std::move(buildableTxs);
    }

    // Payload ID: deterministic derive from attributes + parent (op-geth-aligned), never a
    // process-local sequence counter (not reproducible across restarts). The version byte is
    // the PAYLOAD SHAPE version, exactly as the OP lane derives it and as the cache entry
    // below stores it: two methods that build the same shape must mint one id for the same
    // content (a raw-version byte would mint two once maxEngineVersion exceeds V3).
    auto payloadIdOpt =
        engine_common::derivePayloadId(*payloadAttributes, forkchoiceState.headBlockHash,
            engine_common::payloadShapeVersion(version), decodedForcedTxs);
    if (!payloadIdOpt.has_value())
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus =
                engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                    std::string("payloadAttributes.transactions contains undecodable hex")),
            .payloadId = std::nullopt,
        };
    }
    auto payloadId = *payloadIdOpt;
    auto nextBlockNumber = *headBlockNumber + 1;
    std::optional<BuildPayloadResult> built;
    try
    {
        built = co_await buildPayload(forkchoiceState, *payloadAttributes, payloadId, version,
            nextBlockNumber, std::move(sealedTxs), view, std::move(decodedForcedTxs),
            std::move(l1Input));
    }
    catch (OpExecutionInternalError const& e)
    {
        // An envelope the CL submitted but this service cannot decode is a payload-content
        // fault: answer a terminal INVALID (same contract as the OP lane's
        // fcuInvalidIfUndecodable), never a retryable -32603. Every other internal fault
        // keeps propagating so the endpoint maps it to -32603.
        if (auto invalid = detail::fcuInvalidIfUndecodable(e))
        {
            co_return *invalid;
        }
        throw;
    }

    auto commonEntry = std::make_shared<BuiltPayload>();
    commonEntry->version = engine_common::payloadShapeVersion(version);
    commonEntry->executionPayload = std::move(built->executionPayload);
    commonEntry->blockValue = 0;
    commonEntry->blobsBundle = std::nullopt;
    commonEntry->shouldOverrideBuilder = false;
    commonEntry->parentBeaconBlockRoot = payloadAttributes->parentBeaconBlockRoot;
    commonEntry->executionRequests = std::move(built->executionRequests);
    // getPayloadV3 and V4 both answer a BlobsBundleV1 on an L1 (Cancun/Prague); the OP
    // lane keeps its historical V3-only present-but-empty bundle.
    auto const answersBundle = m_externalPayloadVerifier ?
                                   (version == static_cast<std::uint32_t>(ApiVersion::V3) ||
                                       version == static_cast<std::uint32_t>(ApiVersion::V4)) :
                                   (version == static_cast<std::uint32_t>(ApiVersion::V3));
    if (answersBundle)
    {
        commonEntry->blobsBundle = BlobsBundleV1{};
    }
    if (m_externalPayloadVerifier && commonEntry->blobsBundle.has_value())
    {
        // The bundle is the concatenation of the block's blob-transaction sidecars in
        // block order (EIP-4844). The seal-time filter above guarantees a sidecar for
        // every pooled blob transaction that made it into the payload; a forced
        // (payloadAttributes.transactions) blob envelope carries no sidecar — the block
        // is built regardless and the hole is logged.
        auto& bundle = *commonEntry->blobsBundle;
        for (auto const& tx : commonEntry->executionPayload.transactions)
        {
            if (tx.decoded == nullptr || tx.decoded->blobVersionedHashes().empty())
            {
                continue;
            }
            auto sidecar = m_memPool.blobSidecar(tx.decoded->hash());
            if (!sidecar.has_value())
            {
                BCOS_LOG(WARNING) << LOG_BADGE("EthEngineService")
                                  << LOG_DESC("getPayload: blob transaction without sidecar in "
                                              "the built payload")
                                  << LOG_KV("hash", tx.decoded->hash().hexPrefixed());
                continue;
            }
            bundle.commitments.insert(bundle.commitments.end(), sidecar->commitments.begin(),
                sidecar->commitments.end());
            bundle.proofs.insert(
                bundle.proofs.end(), sidecar->proofs.begin(), sidecar->proofs.end());
            bundle.blobs.insert(bundle.blobs.end(), sidecar->blobs.begin(), sidecar->blobs.end());
        }
    }

    auto stagedArtifact = EthPayloadArtifacts<ViewType>{
        .view = std::make_shared<ViewType>(std::move(view)),
        .header = std::move(built->header),
        .receipts = std::move(built->receipts),
        .mptDelta = std::move(built->mptDelta),
    };

    {
        // commonEntry is null after the move, so capture the hash before the
        // argument list is evaluated — argument evaluation order is unspecified and
        // publishBuiltPayload stores the payload by move, needing the hash
        // independently of the moved pointer.
        const auto blockHash = commonEntry->executionPayload.blockHash;
        auto guard = m_tracker.lockExclusive();
        publishBuiltPayload(guard, m_artifacts, payloadId, blockHash, std::move(commonEntry),
            std::move(stagedArtifact));
    }
    result.payloadId = payloadId;
    co_return result;
}

template <class MemPoolType, class GlobalStateStorageType, class ExecutorType, class SchedulerType>
    requires executor_v1::TransactionExecutor<ExecutorType,
                 typename GlobalStateStorageType::ViewType> &&
             scheduler_v1::TransactionScheduler<SchedulerType,
                 typename GlobalStateStorageType::ViewType, ExecutorType,
                 std::vector<protocol::Transaction::Ptr>>
task::Task<PayloadStatus>
EthEngineService<MemPoolType, GlobalStateStorageType, ExecutorType, SchedulerType>::newPayload(
    const NewPayloadRequest& request, std::uint32_t version)
{
    if (!isNewPayloadVersionSupported(version))
    {
        BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                              << bcos::errinfo_comment{"Unsupported Engine API version"});
    }
    // EL mode validates against the vanilla L1 dialect (validateExecutionPayloadL1);
    // the OP/single-node wiring keeps the OP-flavored gate, lifted for blob envelopes
    // only when the chain admits them (executor_version==2).
    auto validationError = m_externalPayloadVerifier ?
                               detail::validateExecutionPayloadL1(
                                   request.executionPayload, version) :
                               detail::validateExecutionPayload(
                                   request.executionPayload, version, m_allowBlobTransactions);
    if (validationError.has_value())
    {
        // The shape/semantic validators only check shape/semantic rules — a failure
        // there is a malformed payload, never a blockHash mismatch (that path is
        // compareWithBuiltPayload's / executionPayloadToEthBlock's and returns
        // InvalidBlockHash directly).
        auto status = PayloadValidationStatus::Invalid;
        co_return engine_common::makeStatus(status, std::nullopt, validationError);
    }
    if (version <= static_cast<std::uint32_t>(ApiVersion::V2) &&
        request.parentBeaconBlockRoot.has_value())
    {
        co_return engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("parentBeaconBlockRoot is only valid for newPayloadV3 and later"));
    }
    if (version >= static_cast<std::uint32_t>(ApiVersion::V3) &&
        !request.parentBeaconBlockRoot.has_value())
    {
        co_return engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("parentBeaconBlockRoot must be a 32-byte hash for newPayloadV3 and "
                        "later"));
    }
    if (!m_externalPayloadVerifier && !m_allowBlobTransactions &&
        version >= static_cast<std::uint32_t>(ApiVersion::V3) &&
        !request.expectedBlobVersionedHashes.empty())
    {
        // Blob-refusing lanes only: an L1 payload legitimately names its blob versioned
        // hashes. The message stays byte-identical to EngineServiceImpl's (parity tests
        // compare the two services' validationError strings).
        co_return engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("expectedBlobVersionedHashes must be empty (L2 forbids blob "
                        "transactions)"));
    }
    if (m_externalPayloadVerifier && version >= static_cast<std::uint32_t>(ApiVersion::V3))
    {
        // EL mode (EIP-4844): the CL's expected list must equal the payload's blob
        // versioned hashes, flattened across the blob transactions in block order.
        std::vector<h256> payloadVersionedHashes;
        for (auto const& tx : request.executionPayload.transactions)
        {
            if (tx.decoded != nullptr)
            {
                auto const& hashes = tx.decoded->blobVersionedHashes();
                payloadVersionedHashes.insert(
                    payloadVersionedHashes.end(), hashes.begin(), hashes.end());
                continue;
            }
            if (dispatchRawTransaction(bcos::bytesConstRef(tx.raw.data(), tx.raw.size())) !=
                RawTransactionKind::Blob)
            {
                continue;
            }
            // External lane: raw stripped envelope — decode it to read the hashes.
            bcos::bytes rawCopy = tx.raw;
            auto rawRef = bcos::ref(rawCopy);
            rpc::Web3Transaction web3Tx;
            if (auto result = web3Tx.tryDecode(rawRef); !result.has_value())
            {
                co_return engine_common::makeStatus(PayloadValidationStatus::Invalid,
                    std::nullopt,
                    std::string("blob transaction envelope is undecodable: ") +
                        result.error().message);
            }
            payloadVersionedHashes.insert(payloadVersionedHashes.end(),
                web3Tx.blobVersionedHashes.begin(), web3Tx.blobVersionedHashes.end());
        }
        if (payloadVersionedHashes != request.expectedBlobVersionedHashes)
        {
            co_return engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string("expectedBlobVersionedHashes do not match the payload's blob "
                            "transactions"));
        }
    }
    if (version >= static_cast<std::uint32_t>(ApiVersion::V4))
    {
        // EL mode (Prague+) requires the list present but it may be non-empty (EIP-7685
        // requests commit to the header requestsHash); the OP lane keeps the
        // present-but-empty rule.
        auto const requestsError = m_externalPayloadVerifier ?
                                       !request.executionRequests.has_value() :
                                       !request.executionRequests.has_value() ||
                                           !request.executionRequests->empty();
        if (requestsError)
        {
            co_return engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string(m_externalPayloadVerifier ?
                                "executionRequests are required for newPayloadV4 and later" :
                                "executionRequests must be a present-but-empty list on this "
                                "chain"));
        }
    }

    if (m_externalPayloadVerifier)
    {
        // EL mode: a payload this node's forkchoiceUpdated never built belongs to the
        // external lane (CL-pushed L1 blocks, executed+committed via the shared
        // EthereumBlockVerifier). The exclusive tracker lock is released before the
        // co_await inside newPayloadExternal.
        bool builtHere;
        {
            auto guard = m_tracker.lockExclusive();
            builtHere = guard.payloadIdForHash(request.executionPayload.blockHash).has_value();
        }
        if (!builtHere)
        {
            co_return co_await newPayloadExternal(request);
        }
    }

    BuiltPayloadPtr cached;
    PayloadID payloadId;
    std::optional<EthPayloadArtifacts<ViewType>> localArtifact;
    // Set when THIS block still owns a queued (or about-to-be-queued) state layer: either this
    // call pushes one, or a previous attempt pushed it and failed, leaving it queued for the
    // retry (commit_retry_without_ledger_drains_after_failed_merge). A payload whose artifact
    // was consumed by a successful commit owns nothing, so a no-op duplicate must not drain —
    // otherwise it would merge another block's in-flight layer (and could fail on it).
    bool stateLayerQueued = false;
    enum class NewPayloadMiss
    {
        None,
        ParentUnknown,
        NotBuiltHere,
        CacheMiss,
    };
    auto miss = NewPayloadMiss::None;
    {
        auto guard = m_tracker.lockExclusive();
        auto const& forkchoiceState = guard.forkchoiceState();
        auto parentKnown = request.executionPayload.parentHash == forkchoiceState.headBlockHash ||
                           guard.payloadIdForHash(request.executionPayload.parentHash).has_value();
        if (!parentKnown)
        {
            miss = NewPayloadMiss::ParentUnknown;
        }
        else if (auto existingId = guard.payloadIdForHash(request.executionPayload.blockHash);
                 !existingId)
        {
            miss = NewPayloadMiss::NotBuiltHere;
        }
        else
        {
            payloadId = *existingId;
            cached = guard.findPayload(payloadId);
            if (!cached)
            {
                miss = NewPayloadMiss::CacheMiss;
            }
        }
    }
    if (miss != NewPayloadMiss::None)
    {
        auto view = m_globalStateStorage.fork();
        ledger::LedgerConfig missLedgerConfig;
        auto const blockNumber = request.executionPayload.blockNumber;
        auto const parentNumber = blockNumber > 0 ? blockNumber - 1 : 0;
        // Not caught: getLedgerConfig surfaces node-local faults — a corrupted persisted
        // consensus parameter (InvalidEVMCRevisionConfig / InvalidWeb3ChainIdConfig, or a
        // boost::bad_lexical_cast from another malformed SYS_CONFIG field applyLedgerConfig
        // parses) or a storage read fault — whose contract is to halt loudly, not to be
        // absorbed into SYNCING. Absorbing one would leave a node that cannot sync and no
        // reason why; the RPC layer turns the exception into -32603 with the diagnostic
        // instead. An era this node merely cannot resolve is the nullopt branch below, and
        // that is the case that answers SYNCING.
        co_await ledger::getLedgerConfig(view, missLedgerConfig, parentNumber, *m_blockFactory);
        auto const forkVersion = detail::ethBlockVersionForBlock(missLedgerConfig, blockNumber);
        if (!forkVersion.has_value())
        {
            co_return engine_common::makeStatus(
                PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
        // Prague+ L1 payloads may carry real EIP-7685 requests; their hash goes into
        // the reconstructed header. Absent/empty lists hash to the empty-requests
        // constant, so OP-shaped payloads are unaffected.
        std::optional<bcos::h256> requestsHash;
        if (request.executionRequests.has_value() && !request.executionRequests->empty())
        {
            requestsHash = detail::calculateRequestsHash(*request.executionRequests);
        }
        if (auto hashError =
                detail::matchReconstructedEthBlockHash(m_blockFactory->blockHeaderFactory(),
                    request.executionPayload, request.parentBeaconBlockRoot, *forkVersion,
                    requestsHash);
            hashError.has_value())
        {
            co_return engine_common::makeStatus(
                PayloadValidationStatus::InvalidBlockHash, std::nullopt, hashError);
        }
        if (miss == NewPayloadMiss::ParentUnknown)
        {
            detail::warnSyncingRateLimited<detail::c_newPayloadParentUnknown>(
                "newPayload parent unknown; answering SYNCING",
                request.executionPayload.parentHash);
            if (m_clSync)
            {
                // The missing parent is the gap the backfiller must close first.
                m_clSync->requestBackfill(request.executionPayload.parentHash);
            }
        }
        else if (miss == NewPayloadMiss::NotBuiltHere)
        {
            detail::warnSyncingRateLimited<detail::c_newPayloadNotBuiltHere>(
                "newPayload block not built here; answering SYNCING",
                request.executionPayload.blockHash);
        }
        else
        {
            detail::warnSyncingRateLimited<detail::c_newPayloadCacheMiss>(
                "newPayload cache miss; answering SYNCING", request.executionPayload.blockHash);
        }
        co_return engine_common::makeStatus(
            PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    }
    // compare (O(tx bytes) memcmp) runs on the immutable shared
    // entry after releasing the exclusive tracker lock.
    if (auto mismatch =
            detail::compareWithBuiltPayload(request.executionPayload, cached->executionPayload))
    {
        co_return engine_common::makeStatus(
            PayloadValidationStatus::InvalidBlockHash, std::nullopt, mismatch);
    }
    {
        auto guard = m_tracker.lockExclusive();
        auto artifactIt = m_artifacts.find(payloadId);
        // Keep artifacts until the durable write succeeds so a retry can finish the block.
        // pushView + view.reset() happen under the lock so a concurrent duplicate
        // newPayload never pushes the same view twice; header/receipts are only
        // read here and consumed after the I/O succeeds, so their presence is the
        // retry discriminator.
        if (artifactIt != m_artifacts.end() &&
            (artifactIt->second.view || artifactIt->second.header))
        {
            stateLayerQueued = true;
            if (artifactIt->second.view)
            {
                m_globalStateStorage.pushView(std::move(*artifactIt->second.view));
                artifactIt->second.view.reset();
            }
            if (m_ledger && artifactIt->second.header)
            {
                localArtifact = artifactIt->second;
            }
        }
    }

    // MPT pruning (CommitObserver) serialization: m_commitMutex guards the whole commit
    // section — the pruning hooks stage the block's counting work on one shared overlay
    // between coPreparePruneRows and onCommit (BaselineSchedulerMPTHelpers.h's
    // prepareMPTPruneRows contract), so [prepare -> merge -> onCommit] must be serialized
    // against every other commit, exactly like BaselineScheduler::m_commitMutex (likewise
    // held across co_await). The mutex also settles the concurrent-duplicate race the
    // comments below describe: the first call to enter commits; a duplicate that popped the
    // still-unconsumed artifact blocks here and is caught by the re-validation next.
    std::unique_lock commitLock(m_commitMutex);
    if (localArtifact)
    {
        bool committedByDuplicate = false;
        {
            auto guard = m_tracker.lockExclusive();
            // commitRetainedPayload (below) clears m_artifacts after a successful commit, so
            // an artifact that vanished while this call waited on m_commitMutex means a
            // concurrent duplicate already landed this block's rows AND counted its delta —
            // re-firing either would merge idempotent rows but DOUBLE-COUNT the reference
            // movements.
            committedByDuplicate = !m_artifacts.contains(payloadId);
        }
        if (committedByDuplicate)
        {
            // The block IS committed: skip the commit work, own no queued layer (the no-op
            // duplicate rule at stateLayerQueued), and let the fail-closed guard below answer
            // from the ledger row (present -> the idempotent VALID).
            localArtifact = std::nullopt;
            stateLayerQueued = false;
        }
    }

    if (localArtifact)
    {
        typename GlobalStateStorageType::MutableStorage prewriteStorage;
        auto block = m_blockFactory->createBlock();
        block->setBlockHeader(localArtifact->header);
        auto const& bloom = cached->executionPayload.logsBloom;
        block->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
        for (auto const& tx : cached->executionPayload.transactions)
        {
            if (tx.decoded)
            {
                block->appendTransaction(tx.decoded);
            }
        }
        for (auto const& receipt : localArtifact->receipts)
        {
            block->appendReceipt(receipt);
        }
        auto blockTxs = std::make_shared<protocol::ConstTransactions>(
            cached->executionPayload.transactions |
            ::ranges::views::filter([](auto const& tx) { return tx.decoded != nullptr; }) |
            ::ranges::views::transform(
                [](auto const& tx) { return protocol::Transaction::ConstPtr(tx.decoded); }) |
            ::ranges::to<std::vector>());
        co_await ledger::prewriteBlockToBuffer(*m_ledger, blockTxs, block, prewriteStorage);
        // EL mode: persist the EIP-4895 withdrawals sidecar exactly like the external
        // verifier's commit (EthereumBlockVerifier step 8 — one number-keyed row holding
        // the RLP LIST of the per-item encodings), so engine_getPayloadBodies* can serve
        // a built-here block's withdrawals.
        if (m_externalPayloadVerifier && cached->executionPayload.withdrawals.has_value())
        {
            bcos::bytes withdrawalsPayload;
            for (auto const& withdrawal : *cached->executionPayload.withdrawals)
            {
                bcos::protocol::EthWithdrawalData data;
                data.index = static_cast<std::uint64_t>(withdrawal.index);
                data.validatorIndex = static_cast<std::uint64_t>(withdrawal.validatorIndex);
                data.address = withdrawal.address;
                data.amount = static_cast<std::uint64_t>(withdrawal.amount);
                bcos::codec::rlp::encode(withdrawalsPayload, data);
            }
            bcos::bytes encodedWithdrawals;
            encodedWithdrawals.reserve(withdrawalsPayload.size() + 8);
            bcos::codec::rlp::encodeHeader(encodedWithdrawals,
                bcos::codec::rlp::Header{.isList = true, .payloadLength = withdrawalsPayload.size()});
            encodedWithdrawals.insert(
                encodedWithdrawals.end(), withdrawalsPayload.begin(), withdrawalsPayload.end());
            bcos::storage::Entry withdrawalsEntry;
            withdrawalsEntry.set(std::move(encodedWithdrawals));
            co_await storage2::writeOne(prewriteStorage,
                executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_WITHDRAWALS,
                    std::to_string(cached->executionPayload.blockNumber)},
                std::move(withdrawalsEntry));
        }
        // EL mode: persist the block's EIP-4844 blob sidecars (one number-keyed row, the
        // RLP LIST of per-blob [commitment, proof, blob] items in block order) so
        // engine_getBlobsV1 can still answer after the pool entries are gone. Only a
        // built-here block reaches this commit with a bundle; externally received
        // payloads carry no blob bodies, so their absence reads as "not held".
        if (m_externalPayloadVerifier && cached->blobsBundle.has_value() &&
            !cached->blobsBundle->blobs.empty())
        {
            auto const& bundle = *cached->blobsBundle;
            bcos::bytes blobsPayload;
            for (std::size_t i = 0; i < bundle.blobs.size(); ++i)
            {
                bcos::bytes item;
                bcos::codec::rlp::encode(item, bundle.commitments[i]);
                bcos::codec::rlp::encode(item, bundle.proofs[i]);
                bcos::codec::rlp::encode(item, bundle.blobs[i]);
                bcos::codec::rlp::encodeHeader(blobsPayload,
                    bcos::codec::rlp::Header{.isList = true, .payloadLength = item.size()});
                blobsPayload.insert(blobsPayload.end(), item.begin(), item.end());
            }
            bcos::bytes encodedBlobs;
            encodedBlobs.reserve(blobsPayload.size() + 8);
            bcos::codec::rlp::encodeHeader(encodedBlobs,
                bcos::codec::rlp::Header{.isList = true, .payloadLength = blobsPayload.size()});
            encodedBlobs.insert(encodedBlobs.end(), blobsPayload.begin(), blobsPayload.end());
            bcos::storage::Entry blobsEntry;
            blobsEntry.set(std::move(encodedBlobs));
            co_await storage2::writeOne(prewriteStorage,
                executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_BLOBS,
                    std::to_string(cached->executionPayload.blockNumber)},
                std::move(blobsEntry));
        }
        // MPT pruning: the observer turns the block's delta into the deletion keys of expired
        // node rows, applied to prewriteStorage so deletions land in the SAME WriteBatch as
        // the block data — the same hook (and ordering) as BaselineScheduler::coCommitBlock.
        // A throw here fails the commit before any row lands; the CL's retry re-runs it (the
        // pruner's staged overlay is discarded and re-derived, so the retry reproduces the
        // identical batch).
        if (localArtifact->mptDelta)
        {
            co_await scheduler_v1::prepareMPTPruneRows(*m_commitObserver,
                cached->executionPayload.blockNumber, *localArtifact->mptDelta, prewriteStorage);
        }
        // Land the prewritten rows together with the queued view layer, then let a
        // later commit drain any layer a previously failed attempt left queued.
        // mergeBackStorage throws NotExistsImmutableStorageError on an empty deque;
        // the empty case is NOT an error — a concurrent duplicate may have drained
        // the layer between the attempts, and its rows are identical to ours, so
        // the prewritten rows land directly via mergeToBackends (idempotent) instead
        // of surfacing a spurious internal error where the Engine API requires the
        // idempotent VALID. Awaits stay OUT of the catch handlers (C++20 forbids
        // them there) — the handler only records whether the rows still need landing.
        bool rowsLanded = false;
        try
        {
            co_await m_globalStateStorage.mergeBackStorage(prewriteStorage);
            rowsLanded = true;
        }
        catch (bcos::storage2::NotExistsImmutableStorageError const&)
        {
            // Queue already empty — nothing to pair with; land the rows below.
        }
        if (!rowsLanded)
        {
            co_await m_globalStateStorage.mergeToBackends(prewriteStorage);
        }
        // Drain remaining queued layers. An abandoned failed merge would
        // otherwise make every later commit merge one-behind, leaving the newest
        // payload's state queued in-memory — lost on restart — though it answered VALID.
        co_await engine_common::drainQueuedLayers(m_globalStateStorage);
        // CommitObserver timing contract (CommitObserver.h): AFTER the block's WriteBatch
        // landed. Deliberately before commitRetainedPayload below and with no co_await in
        // between: if a retry ever re-ran the hooks for an already-counted block the
        // double-count would degrade to leaks, while skipping onCommit for a merged block
        // would under-count and could later delete a live node — the ordering chosen fails
        // toward the leak side. onCommit must not throw (Noop and MPTPruner don't).
        if (localArtifact->mptDelta)
        {
            m_commitObserver->onCommit(
                cached->executionPayload.blockNumber, *localArtifact->mptDelta);
        }
    }
    else if (stateLayerQueued)
    {
        // A queued layer without durable work still has to reach the backends — the merge must
        // not depend on ledger persistence (CI-found: the pushed view stayed queued in memory
        // and every committed balance was lost).
        co_await engine_common::drainQueuedLayers(m_globalStateStorage);
    }

    // Fail-closed guard: if no local artifact was available above, either
    // the block was already committed by an earlier call (artifacts.clear() in
    // commitRetainedPayload) or a previous commit attempt died mid-persist. Only the
    // former may answer VALID - a ledger row proves the persist actually happened. When
    // the ledger has no row for this blockHash, answering VALID would fabricate a
    // committed block (the retry previously skipped the merge and still returned VALID).
    if (!localArtifact && m_ledger)
    {
        auto checkView = m_globalStateStorage.fork();
        if (!co_await bcos::ledger::getBlockNumber(
                checkView, cached->executionPayload.blockHash, bcos::ledger::fromStorage))
        {
            detail::warnSyncingUnlimited(
                "newPayload has no ledger row and no retryable artifact; answering SYNCING "
                "(a previous attempt died mid-persist)",
                cached->executionPayload.blockHash);
            co_return engine_common::makeStatus(
                PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
    }

    {
        auto guard = m_tracker.lockExclusive();
        // Keep the locally built body (executed at FCU). Do not rewrite from the CL request.
        detail::commitRetainedPayload(
            guard, m_artifacts, payloadId, cached->executionPayload.blockHash, cached);
    }

    // Publish the post-commit configuration into the admission holder (TxValidator's
    // "whoever commits a block publishes" contract). This lane bypasses
    // MultiVersionScheduler's publishing wrapper, so without this the engine-driven modes
    // would admit every later transaction against the boot snapshot. Fires on every VALID
    // answer — including the idempotent-duplicate path — so a failed refetch is retried by
    // the CL's resubmission instead of silently leaving admission one block behind. A
    // failing refetch propagates: the block is durable and the CL retries, the same
    // fail-stop the per-block refetch elsewhere applies.
    if (m_ledgerConfigState && m_ledger)
    {
        auto ledgerConfig = co_await ledger::getLedgerConfig(*m_ledger);
        m_ledgerConfigState->set(std::make_shared<const bcos::ledger::LedgerConfig>(*ledgerConfig));
    }

    co_return engine_common::makeStatus(
        PayloadValidationStatus::Valid, cached->executionPayload.blockHash, std::nullopt);
}

template <class MemPoolType, class GlobalStateStorageType, class ExecutorType, class SchedulerType>
    requires executor_v1::TransactionExecutor<ExecutorType,
                 typename GlobalStateStorageType::ViewType> &&
             scheduler_v1::TransactionScheduler<SchedulerType,
                 typename GlobalStateStorageType::ViewType, ExecutorType,
                 std::vector<protocol::Transaction::Ptr>>
task::Task<PayloadStatus>
EthEngineService<MemPoolType, GlobalStateStorageType, ExecutorType,
    SchedulerType>::newPayloadExternal(const NewPayloadRequest& request)
{
    auto const& payload = request.executionPayload;
    auto converted = detail::executionPayloadToEthBlock(request);
    if (auto* error = std::get_if<std::string>(&converted))
    {
        co_return engine_common::makeStatus(
            PayloadValidationStatus::InvalidBlockHash, std::nullopt, *error);
    }
    auto external = std::move(std::get<engine_common::ExternalPayloadBlock>(converted));

    auto view = m_globalStateStorage.fork();
    // Idempotent redelivery: the hash already maps to a committed number.
    auto const committedNumber =
        co_await bcos::ledger::getBlockNumber(view, payload.blockHash, bcos::ledger::fromStorage);
    if (committedNumber.has_value())
    {
        if (*committedNumber != payload.blockNumber)
        {
            co_return engine_common::makeStatus(PayloadValidationStatus::Invalid,
                payload.parentHash,
                std::string("blockHash already maps to a different block number"));
        }
        co_return engine_common::makeStatus(
            PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
    }
    auto const head = co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
    // The parent height this payload commits on: the tip for the common head+1 case; the
    // resolved canonical parent for the shallow-reorg route below.
    bcos::protocol::BlockNumber parentNumber = head;
    if (payload.blockNumber != head + 1)
    {
        // Phase 3 shallow reorg (EthereumChainRollback.h): a payload AT/BELOW the tip whose
        // parent resolves to the canonical block at number-1 is a competing fork head the
        // CL wants validated — route it through the verifier, whose stale retry rewinds the
        // committed chain to number-1 (reorg window permitting) and commits this payload.
        bool reorgRoute = false;
        if (payload.blockNumber >= 1 && payload.blockNumber <= head)
        {
            auto const resolvedParent = co_await bcos::ledger::getBlockNumber(
                view, payload.parentHash, bcos::ledger::fromStorage);
            if (resolvedParent.has_value() && *resolvedParent == payload.blockNumber - 1)
            {
                parentNumber = *resolvedParent;
                reorgRoute = true;
            }
        }
        if (!reorgRoute)
        {
            // Ahead of the head (a gap only EL sync fills) or behind it without a canonical
            // parent row (a side fork we cannot place or a pruned segment): neither is
            // decidable here.
            if (m_clSync)
            {
                // The backfiller pulls toward the payload itself; the CL's retry then
                // finds it committed (idempotent VALID) or commits on top of the
                // backfilled parent. A payload at/behind the tip resolves to the
                // side-fork log in the sync loop.
                m_clSync->requestBackfill(payload.blockHash);
            }
            co_return engine_common::makeStatus(
                PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
    }
    else
    {
        auto const headHash =
            co_await bcos::ledger::getBlockHash(view, head, bcos::ledger::fromStorage);
        if (!headHash.has_value() || *headHash != payload.parentHash)
        {
            // The parent is not the local head — unknown parent or a sibling fork; this
            // node holds no state for side forks, so the CL keeps it in SYNCING.
            if (m_clSync)
            {
                // A fork at the tip cannot be closed by download (the committed head
                // stands); the backfill target lets the sync loop detect and log exactly
                // that instead of the CL retrying blind.
                m_clSync->requestBackfill(payload.blockHash);
            }
            co_return engine_common::makeStatus(
                PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
    }
    auto const parentBlock = co_await bcos::ledger::getBlockData(
        view, parentNumber, bcos::ledger::HEADER, *m_blockFactory);
    external.parentHeader = bcos::protocol::EthBlockHeader(*parentBlock->blockHeader()).data();

    auto result = co_await m_externalPayloadVerifier->verifyAndCommit(m_globalStateStorage, external);
    switch (result.outcome)
    {
    case engine_common::ExternalPayloadOutcome::Valid:
        // Same republish contract as the built-payload commit path above: whoever
        // commits a block publishes the post-commit configuration for TxValidator.
        if (m_ledgerConfigState && m_ledger)
        {
            auto ledgerConfig = co_await ledger::getLedgerConfig(*m_ledger);
            m_ledgerConfigState->set(
                std::make_shared<const bcos::ledger::LedgerConfig>(*ledgerConfig));
        }
        co_return engine_common::makeStatus(
            PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
    case engine_common::ExternalPayloadOutcome::StaleOrOutOfOrder:
        // Lost the head+1 race (another commit lane landed a block first).
        if (m_clSync)
        {
            // The payload's height is now at/behind the tip: the backfill target
            // resolves to either the already-committed block (cleared quietly) or a
            // side fork (logged by the sync loop).
            m_clSync->requestBackfill(payload.blockHash);
        }
        co_return engine_common::makeStatus(
            PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    case engine_common::ExternalPayloadOutcome::Invalid:
    default:
        // The parent is the committed head, so it is the latest valid hash the CL
        // needs for its forkchoice recovery.
        co_return engine_common::makeStatus(PayloadValidationStatus::Invalid, payload.parentHash,
            result.error.empty() ? std::nullopt : std::optional<std::string>(result.error));
    }
}

template <class MemPoolType, class GlobalStateStorageType, class ExecutorType, class SchedulerType>
    requires executor_v1::TransactionExecutor<ExecutorType,
                 typename GlobalStateStorageType::ViewType> &&
             scheduler_v1::TransactionScheduler<SchedulerType,
                 typename GlobalStateStorageType::ViewType, ExecutorType,
                 std::vector<protocol::Transaction::Ptr>>
task::Task<typename EthEngineService<MemPoolType, GlobalStateStorageType, ExecutorType,
    SchedulerType>::BuildPayloadResult>
EthEngineService<MemPoolType, GlobalStateStorageType, ExecutorType, SchedulerType>::buildPayload(
    const ForkchoiceState& forkchoiceState, const PayloadAttributes& payloadAttributes,
    const PayloadID& payloadId, std::uint32_t version, bcos::protocol::BlockNumber nextBlockNumber,
    std::vector<protocol::Transaction::Ptr> sealedTxs, ViewType& view,
    std::vector<bcos::bytes> decodedForcedTxs, std::optional<L1BuildInput> l1Input) const
{
    std::vector<EngineTransaction> engineTransactions;
    engineTransactions.reserve(
        (payloadAttributes.transactions ? payloadAttributes.transactions->size() : 0) +
        sealedTxs.size());
    if (payloadAttributes.transactions.has_value())
    {
        // Forced envelopes are raw-only on the wire, so give each one the same executable
        // `decoded` form a sealed pool transaction already carries. Carrying them raw
        // (decoded == nullptr) made collectExecutableTransactions skip them: they entered
        // transactionsRoot but were neither executed, persisted, nor given a receipt, so
        // transactionsRoot covered N envelopes while receiptsRoot covered only the M sealed
        // txs. Decoding here lets executeBlock run them and makes N == M.
        auto& hashImpl = *m_blockFactory->cryptoSuite()->hashImpl();
        for (auto& raw : decodedForcedTxs)
        {
            const auto txHash = hashImpl.hash(raw);
            // allowDeposit=false: a 0x7e deposit envelope is an OP-Stack extension and
            // invalid on the Eth lane.
            auto tarsTx = engine_common::op::opEnvelopeToTars(raw, txHash, /*allowDeposit=*/false);
            if (!tarsTx)
            {
                // validatePayloadAttributes only dispatches on the envelope's type byte, so a
                // body that fails RLP decode reaches here from a remote CL. Tag it as a
                // payload-content fault: updateForkchoice maps the tag to a terminal INVALID
                // (same contract as the OP lane's fcuInvalidIfUndecodable) — an untagged
                // OpExecutionInternalError would surface as -32603 and the CL would resubmit
                // the identical attributes forever.
                BOOST_THROW_EXCEPTION(
                    OpExecutionInternalError{}
                    << OpPayloadUndecodable{true}
                    << bcos::errinfo_comment{"forced payloadAttributes.transactions envelope "
                                             "is undecodable"});
            }
            // Same carrier the OP build path uses (OpEngineService::buildOpBlock): keep the
            // raw EIP-2718 envelope on extraTransactionBytes so the executor sees the exact
            // wire form.
            auto decoded = engine_common::decodedTransactionFromEnvelope(std::move(*tarsTx), raw);
            engineTransactions.push_back(EngineTransaction{
                .raw = std::move(raw),
                .decoded = std::move(decoded),
            });
        }
    }
    for (auto& sealedTx : sealedTxs)
    {
        if (sealedTx->type() !=
            static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
        {
            BCOS_LOG(WARNING) << LOG_BADGE("EthEngineService")
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

    // EL mode: the L1 build diverges here — no OP extraData, the fork comes from the
    // CL's timestamp via the chain's fork schedule, and execution runs through the
    // shared verifier phase so a built block verifies by construction.
    if (l1Input.has_value())
    {
        co_return co_await buildL1Payload(forkchoiceState, payloadAttributes, nextBlockNumber,
            std::move(engineTransactions), view, *l1Input);
    }

    // Match release EngineServiceImpl: stamp OP extraData and derive the Eth header fork
    // from the on-chain EVM revision, not from the Engine API method version.
    bytes extraData = detail::encodeOptimismExtraData(payloadAttributes);

    ExecutionPayload executionPayload{
        .logsBloom = Bloom{},
        .parentHash = forkchoiceState.headBlockHash,
        .stateRoot = h256{},
        .receiptsRoot = h256{},
        .prevRandao = payloadAttributes.prevRandao,
        .gasLimit = 0,
        .gasUsed = 0,
        .baseFeePerGas = 0,
        .blockHash = h256{},
        .transactions = std::move(engineTransactions),
        .extraData = extraData,
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

    ledger::LedgerConfig ledgerConfig;
    co_await ledger::getLedgerConfig(view, ledgerConfig, nextBlockNumber - 1, *m_blockFactory);
    auto blockVersion = ledgerConfig.compatibilityVersion();

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
    auto forkVersion = detail::ethBlockVersionFor(*chainRevision);

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
        executionPayload.withdrawalsRoot = detail::withdrawalsRootFor(executionPayload);
    }

    executionPayload.gasLimit = std::get<0>(ledgerConfig.gasLimit());

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
        emptyHeader->setExtraData(std::move(extraData));
        auto emptyResolution = co_await engine_common::resolveEngineBlockStateRoot(view,
            *emptyHeader, ledgerConfig, *m_blockFactory->cryptoSuite()->hashImpl(),
            *m_blockFactory, *m_commitObserver);
        emptyHeader->setReceiptsRoot(bcos::ledger::mpt::emptyRootHash());
        emptyHeader->setTxsRoot(bcos::ledger::mpt::emptyRootHash());
        emptyHeader->setGasUsed(0);
        detail::finalizeEthBlockHeader(
            *emptyHeader, executionPayload, payloadAttributes.parentBeaconBlockRoot, forkVersion);
        executionPayload.stateRoot = emptyHeader->stateRoot();
        executionPayload.receiptsRoot = bcos::ledger::mpt::emptyRootHash();
        executionPayload.gasUsed = 0;
        executionPayload.blockHash = emptyHeader->hash();
        co_return BuildPayloadResult{.executionPayload = std::move(executionPayload),
            .header = std::move(emptyHeader),
            .receipts = {},
            .mptDelta = engine_common::shareMptDelta(std::move(emptyResolution.mptDelta))};
    }

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
    blockHeader->setExtraData(std::move(extraData));

    // Executed transactions, with each one's EIP-2718 type byte kept index-parallel to
    // `receipts` for the receipts-root leaf prefix below. Forced entries arrive already
    // decoded (buildPayload's opEnvelopeToTars step), so every envelope in
    // executionPayload.transactions has an executable form: collectExecutableTransactions
    // skips nothing here, and transactionsRoot and receiptsRoot cover the same set (N == M).
    // allowBlob=true: this is the pure-Ethereum lane (executor_version==2), whose admission
    // path lets blob transactions in — the OP lane keeps the default refusal.
    auto executable =
        engine_common::collectExecutableTransactions(executionPayload.transactions, true);
    auto receipts = co_await m_scheduler.executeBlock(view, m_executor, *blockHeader,
        executable.transactions | ::ranges::views::indirect, ledgerConfig);

    // The Ethereum header commitments, built by the shared engine_common helper that
    // EngineServiceImpl also uses (that class has no production caller left; the parity tests
    // keep it as an oracle) so the implementations cannot drift.
    // transactionsRoot is the index-keyed MPT over the raw EIP-2718 envelopes and MUST
    // match the cache-miss reconstruction (EngineServiceCommon.cpp transactionsRootFromPayload)
    // and the OP path's computeTxRoot, otherwise newPayload rejects this node's own payloads
    // with INVALID_BLOCK_HASH. receiptsRoot is the same construction over the RLP receipt leaves
    // and MUST match the OP block seal (sealOpBlock).
    auto const commitments = engine_common::buildHeaderCommitments(
        executionPayload.transactions, receipts, executable.types);
    h256 const txRoot = commitments.transactionsRoot;
    h256 const receiptRoot = commitments.receiptsRoot;
    // gas used and the block-level logsBloom come out of the same call, so neither can be
    // derived from pre-normalization receipts.
    u256 const totalGasUsed = commitments.gasUsed;
    Bloom const& logsBloom = commitments.logsBloom;

    auto resolution = co_await engine_common::resolveEngineBlockStateRoot(view, *blockHeader,
        ledgerConfig, *m_blockFactory->cryptoSuite()->hashImpl(), *m_blockFactory,
        *m_commitObserver);
    h256 const stateRoot = resolution.stateRoot;
    blockHeader->setReceiptsRoot(receiptRoot);
    blockHeader->setTxsRoot(txRoot);
    blockHeader->setGasUsed(totalGasUsed);

    executionPayload.logsBloom = logsBloom;
    detail::finalizeEthBlockHeader(
        *blockHeader, executionPayload, payloadAttributes.parentBeaconBlockRoot, forkVersion);

    executionPayload.stateRoot = stateRoot;
    executionPayload.receiptsRoot = receiptRoot;
    executionPayload.gasUsed = totalGasUsed;
    executionPayload.blockHash = blockHeader->hash();
    executionPayload.gasLimit = std::get<0>(ledgerConfig.gasLimit());

    co_return BuildPayloadResult{.executionPayload = std::move(executionPayload),
        .header = std::move(blockHeader),
        .receipts = std::move(receipts),
        .mptDelta = engine_common::shareMptDelta(std::move(resolution.mptDelta))};
}

template <class MemPoolType, class GlobalStateStorageType, class ExecutorType, class SchedulerType>
    requires executor_v1::TransactionExecutor<ExecutorType,
                 typename GlobalStateStorageType::ViewType> &&
             scheduler_v1::TransactionScheduler<SchedulerType,
                 typename GlobalStateStorageType::ViewType, ExecutorType,
                 std::vector<protocol::Transaction::Ptr>>
task::Task<typename EthEngineService<MemPoolType, GlobalStateStorageType, ExecutorType,
    SchedulerType>::BuildPayloadResult>
EthEngineService<MemPoolType, GlobalStateStorageType, ExecutorType, SchedulerType>::
    buildL1Payload(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, bcos::protocol::BlockNumber nextBlockNumber,
        std::vector<EngineTransaction> engineTransactions, ViewType& view,
        L1BuildInput const& l1Input) const
{
    auto const& l1Context = l1Input.l1Context;
    auto const forkVersion = l1Context.forkVersion;

    ledger::LedgerConfig ledgerConfig;
    co_await ledger::getLedgerConfig(view, ledgerConfig, nextBlockNumber - 1, *m_blockFactory);
    auto const gasLimit = u256(std::get<0>(ledgerConfig.gasLimit()));

    if (forkVersion >= bcos::protocol::EthBlockVersion::CANCUN &&
        !payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        BOOST_THROW_EXCEPTION(
            InvalidPayloadAttributes{} << bcos::errinfo_comment{
                "EngineService: the L1 fork derived from the payload timestamp is CANCUN or "
                "later, which requires the V3 payload attributes (parentBeaconBlockRoot); "
                "forkchoiceUpdated must be called at version >= 3"});
    }

    // The raw EIP-2718 envelopes plus the block's blob gas (EIP-4844: BLOB_GAS_PER_BLOB
    // per versioned hash). The OP path hard-codes blobGasUsed 0; here every blob-carrying
    // transaction counts.
    std::vector<bcos::bytes> rawTransactions;
    rawTransactions.reserve(engineTransactions.size());
    u256 blobGasUsed = 0;
    for (auto const& tx : engineTransactions)
    {
        rawTransactions.push_back(tx.raw);
        if (tx.decoded)
        {
            blobGasUsed +=
                tx.decoded->blobVersionedHashes().size() * protocol::BLOB_GAS_PER_BLOB;
        }
    }

    // EIP-4895: per-item withdrawal RLP, the same encoding executionPayloadToEthBlock
    // produces for the newPayload direction, so build and verify agree byte for byte.
    std::optional<std::vector<bcos::bytes>> rawWithdrawals;
    std::optional<bcos::h256> withdrawalsHash;
    if (forkVersion >= bcos::protocol::EthBlockVersion::SHANGHAI)
    {
        auto const& withdrawals =
            payloadAttributes.withdrawals.value_or(std::vector<WithdrawalV1>{});
        auto& encoded = rawWithdrawals.emplace();
        encoded.reserve(withdrawals.size());
        for (auto const& withdrawal : withdrawals)
        {
            if (withdrawal.index > std::numeric_limits<std::uint64_t>::max() ||
                withdrawal.validatorIndex > std::numeric_limits<std::uint64_t>::max() ||
                withdrawal.amount > std::numeric_limits<std::uint64_t>::max())
            {
                BOOST_THROW_EXCEPTION(
                    InvalidPayloadAttributes{} << bcos::errinfo_comment{
                        "EngineService: withdrawal index/validatorIndex/amount exceeds "
                        "uint64"});
            }
            bcos::protocol::EthWithdrawalData data;
            data.index = static_cast<std::uint64_t>(withdrawal.index);
            data.validatorIndex = static_cast<std::uint64_t>(withdrawal.validatorIndex);
            data.address = withdrawal.address;
            data.amount = static_cast<std::uint64_t>(withdrawal.amount);
            bcos::bytes item;
            bcos::codec::rlp::encode(item, data);
            encoded.push_back(std::move(item));
        }
        std::vector<bcos::bytesConstRef> refs;
        refs.reserve(encoded.size());
        for (auto const& item : encoded)
        {
            refs.push_back(bcos::ref(item));
        }
        // The L1 header root is the MPT over the list — never the ExecutionPayloadV4
        // withdrawalsRoot dialect (that is the OP MessagePasser storage root).
        withdrawalsHash = bcos::ledger::mpt::calculateWithdrawalsRoot(refs);
    }

    // The header context fields; the root fields stay zero — execution stamps them.
    bcos::protocol::EthBlockHeaderData ethHeader;
    ethHeader.parentInfo = bcos::protocol::ParentInfo{
        .blockNumber = nextBlockNumber - 1, .blockHash = forkchoiceState.headBlockHash};
    ethHeader.uncleHash = bcos::protocol::c_emptyOmmersHash;
    ethHeader.coinbase = payloadAttributes.suggestedFeeRecipient;
    ethHeader.nonce = engine_common::c_posNonce;
    ethHeader.difficulty = bcos::u256(0);
    ethHeader.number = nextBlockNumber;
    ethHeader.timestamp = static_cast<int64_t>(payloadAttributes.timestamp / 1000);
    ethHeader.prevRandao = payloadAttributes.prevRandao;
    ethHeader.gasLimit = gasLimit;
    ethHeader.extraData = {};  // L1: no OP extraData encoding
    ethHeader.baseFee = l1Context.baseFee;
    ethHeader.withdrawalsHash = withdrawalsHash;
    if (forkVersion >= bcos::protocol::EthBlockVersion::CANCUN)
    {
        ethHeader.blobGasUsed = blobGasUsed;
        ethHeader.excessBlobGas = l1Context.excessBlobGas;
        ethHeader.parentBeaconRoot = payloadAttributes.parentBeaconBlockRoot;
    }

    auto built = co_await m_externalPayloadVerifier->buildL1Block(view,
        engine_common::ExternalBuildBlock{.ethHeader = ethHeader,
            .parentHeader = l1Input.parentHeader,
            .rawTransactions = rawTransactions,
            .rawWithdrawals = rawWithdrawals});
    if (!built.ok)
    {
        // A transaction the pool admitted but the block cannot execute (a decode or
        // execution fault) fails the whole build — the CL gets an internal error, never
        // a silently truncated block. (geth's builder would drop the offender and
        // rebuild; this lane builds once and fails loudly.)
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                  "EngineService: L1 payload build failed: " + built.error});
    }

    ExecutionPayload executionPayload{
        .logsBloom = built.computation.logsBloom,
        .parentHash = forkchoiceState.headBlockHash,
        .stateRoot = built.stateRoot,
        .receiptsRoot = built.computation.receiptsRoot,
        .prevRandao = payloadAttributes.prevRandao,
        .gasLimit = gasLimit,
        .gasUsed = built.computation.gasUsed,
        .baseFeePerGas = l1Context.baseFee,
        .blockHash = h256{},
        .transactions = std::move(engineTransactions),
        .extraData = {},
        .feeRecipient = payloadAttributes.suggestedFeeRecipient,
        .timestamp = payloadAttributes.timestamp,
        .blockNumber = nextBlockNumber,
        .withdrawals = forkVersion >= bcos::protocol::EthBlockVersion::SHANGHAI ?
                           std::optional<std::vector<WithdrawalV1>>(
                               payloadAttributes.withdrawals.value_or(
                                   std::vector<WithdrawalV1>{})) :
                           std::nullopt,
        .blobGasUsed = forkVersion >= bcos::protocol::EthBlockVersion::CANCUN ?
                           std::optional<u256>(blobGasUsed) :
                           std::nullopt,
        .excessBlobGas = forkVersion >= bcos::protocol::EthBlockVersion::CANCUN ?
                             std::optional<u256>(l1Context.excessBlobGas) :
                             std::nullopt,
        .blockAccessList = std::nullopt,
        .slotNumber = std::nullopt,
        // L1 headers commit to the withdrawals MPT root in withdrawalsHash; the
        // ExecutionPayloadV4 withdrawalsRoot field stays unset (OP dialect).
        .withdrawalsRoot = std::nullopt,
    };

    // The staged FISCO header: same field set the OP path stamps, with the executed roots
    // and the L1 fork-gated fields (withdrawals MPT root, real requestsHash on Prague+).
    auto blockHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    blockHeader->setParentInfo(bcos::protocol::ParentInfo{
        .blockNumber = nextBlockNumber - 1, .blockHash = forkchoiceState.headBlockHash});
    blockHeader->setNumber(nextBlockNumber);
    blockHeader->setVersion(ledgerConfig.compatibilityVersion());
    blockHeader->setTimestamp(static_cast<int64_t>(payloadAttributes.timestamp));
    blockHeader->setCoinbase(payloadAttributes.suggestedFeeRecipient);
    blockHeader->setPrevRandao(payloadAttributes.prevRandao);
    blockHeader->setGasLimit(gasLimit);
    blockHeader->setExtraData({});
    blockHeader->setStateRoot(built.stateRoot);
    blockHeader->setReceiptsRoot(built.computation.receiptsRoot);
    blockHeader->setTxsRoot(built.computation.txsRoot);
    blockHeader->setGasUsed(built.computation.gasUsed);
    detail::finalizeEthBlockHeader(*blockHeader, executionPayload,
        payloadAttributes.parentBeaconBlockRoot, forkVersion, withdrawalsHash,
        built.requestsHash);
    executionPayload.blockHash = blockHeader->hash();

    co_return BuildPayloadResult{.executionPayload = std::move(executionPayload),
        .header = std::move(blockHeader),
        .receipts = std::move(built.receipts),
        .mptDelta = std::move(built.mptDelta),
        .executionRequests =
            forkVersion >= bcos::protocol::EthBlockVersion::PRAGUE ?
                std::optional<std::vector<bytes>>(std::move(built.executionRequests)) :
                std::nullopt};
}

}  // namespace bcos::engine
