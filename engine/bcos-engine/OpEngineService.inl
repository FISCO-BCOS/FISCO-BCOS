/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/view/transform.hpp>

namespace bcos::engine
{

namespace op_detail
{
/// release ExecutionPayload keeps a single carrier: `transactions[i].raw`.
inline auto rawEnvelopes(ExecutionPayload const& payload)
{
    return payload.transactions |
           ::ranges::views::transform(
               [](EngineTransaction const& tx) -> bytes const& { return tx.raw; });
}

inline std::optional<ForkchoiceUpdatedResult> fcuInvalidIfUndecodable(
    OpExecutionInternalError const& error)
{
    if (boost::get_error_info<OpPayloadUndecodable>(error) == nullptr)
    {
        return std::nullopt;
    }
    return ForkchoiceUpdatedResult{
        .payloadStatus = engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("undecodable payload transaction envelope")),
        .payloadId = std::nullopt,
    };
}
}  // namespace op_detail

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
    if (payloadAttributes != nullptr)
    {
        if (version < 3)
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "Isthmus+ payload building requires engine_forkchoiceUpdatedV3 "
                    "(JSON-RPC -38005; FCU V4 is unimplemented)"});
        }
        if (auto validationError =
                engine_common::validatePayloadAttributes(*payloadAttributes, version);
            validationError.has_value())
        {
            co_return ForkchoiceUpdatedResult{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError),
                .payloadId = std::nullopt,
            };
        }
        if (auto validationError = engine_common::op::validateOpPayloadAttributes(
                *payloadAttributes, m_scheduler.isJovianActive());
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

    auto view = m_globalStateStorage.fork();
    auto headBlockNumber = co_await bcos::ledger::getBlockNumber(
        view, forkchoiceState.headBlockHash, bcos::ledger::fromStorage);
    // All-zero safe/finalized hashes are the Engine-API "not set" value: skip number
    // resolution and canonical checks for that field (op-geth SetSafe/SetFinalized are
    // only called for non-zero hashes). A missing HEAD is SYNCING; a non-zero
    // unresolvable safe/finalized is InvalidForkchoiceState (op-geth, finding BJ).
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
    };
    if (m_tracker.applyForkchoice(resolved) == ForkchoiceApplyResult::Swallowed)
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus = makeStatus(
                PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
            .payloadId = std::nullopt,
        };
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

    co_return co_await buildOpPayload(
        forkchoiceState, *payloadAttributes, version, *headBlockNumber + 1);
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<ForkchoiceUpdatedResult>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::buildOpPayload(
    const ForkchoiceState& forkchoiceState, const PayloadAttributes& payloadAttributes,
    std::uint32_t version, bcos::protocol::BlockNumber nextBlockNumber)
{
    // Same policy as EthEngineService (option B): deterministic derivePayloadId, not a
    // process-local sequence counter.
    auto payloadIdOpt =
        engine_common::derivePayloadId(payloadAttributes, forkchoiceState.headBlockHash, version);
    if (!payloadIdOpt.has_value())
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus = makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string("payloadAttributes.transactions contains undecodable hex")),
            .payloadId = std::nullopt,
        };
    }
    auto payloadId = *payloadIdOpt;

    u256 baseFee;
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
                .payloadStatus = makeStatus(PayloadValidationStatus::Invalid,
                    forkchoiceState.headBlockHash,
                    std::string("parent block header is missing from storage")),
                .payloadId = std::nullopt,
            };
        }
        auto stored = parentHeaderEntry->get();
        bcos::bytes parentHeaderBytes(stored.begin(), stored.end());
        auto parentHeader =
            m_blockFactory->blockHeaderFactory()->createBlockHeader(parentHeaderBytes);
        baseFee = calcOpBaseFee(*parentHeader, m_scheduler.isJovianActive());
    }

    requireDelegate();

    auto sealView = m_globalStateStorage.fork();
    std::vector<protocol::Transaction::Ptr> sealedTxs;
    if (!payloadAttributes.noTxPool.value_or(false))
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
        forcedEnvelopes.push_back(m_scheduler.synthesizeL1AttributesEnvelope());
    }
    if (payloadAttributes.transactions.has_value())
    {
        for (auto const& forcedHex : *payloadAttributes.transactions)
        {
            forcedEnvelopes.push_back(fromHex(forcedHex));
        }
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
    // this candidate and never evicts those successors from the pool (R3-F1).
    // Walk by nonce, not sealed-vector position (finding BU): seal order is not a
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
    if (m_daCaps)
    {
        for (auto const& [hash, env] : sealedEnvelopes)
        {
            if (!m_daCaps->txFits(env.size()))
            {
                skipSenderTail(hash);
            }
        }
    }

    ledger::LedgerConfig ledgerConfig;
    {
        auto view = m_globalStateStorage.fork();
        co_await ledger::getLedgerConfig(view, ledgerConfig, nextBlockNumber - 1, *m_blockFactory);
    }

    auto const parentBeaconBlockRoot = payloadAttributes.parentBeaconBlockRoot.value();

    auto assemblePayload = [&](std::vector<bytes> candidateEnvelopes) {
        std::vector<EngineTransaction> candidateTransactions;
        candidateTransactions.reserve(candidateEnvelopes.size());
        for (auto& env : candidateEnvelopes)
        {
            candidateTransactions.push_back(EngineTransaction{.raw = env, .decoded = nullptr});
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
    // only reworked when per-envelope execution becomes reusable (finding AX).
    while (true)
    {
        std::vector<bytes> candidateEnvelopes = forcedEnvelopes;
        std::optional<bcos::engine::DACaps::Budget> budget;
        if (m_daCaps)
        {
            std::uint64_t forcedBytes = 0;
            for (auto const& env : forcedEnvelopes)
            {
                forcedBytes += env.size();
            }
            budget.emplace(*m_daCaps, forcedBytes);
        }
        for (auto const& [hash, env] : sealedEnvelopes)
        {
            if (evicted.count(hash) != 0)
            {
                continue;
            }
            if (budget && !budget->admits(env.size()))
            {
                break;
            }
            candidateEnvelopes.push_back(env);
        }
        payload = assemblePayload(std::move(candidateEnvelopes));

        const auto transactionsRoot =
            SchedulerType::computeTxRoot(op_detail::rawEnvelopes(payload));
        auto provisionalHeader = engine_common::op::rebuildOpEthHeader(
            m_blockFactory->blockHeaderFactory(), payload, transactionsRoot, parentBeaconBlockRoot);
        bcos::protocol::Block::Ptr block;
        try
        {
            block = buildOpBlock(payload, provisionalHeader);
        }
        catch (const OpExecutionInternalError& e)
        {
            if (auto invalid = op_detail::fcuInvalidIfUndecodable(e))
            {
                co_return *invalid;
            }
            throw;
        }

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
        auto culprit = executeError ? culpritTxHashFromError(*executeError) :
                                      std::optional<crypto::HashType>{};
        // A block-gas capacity fault means the tx is VALID but does not fit this
        // candidate (mempool seals by count only). OpRejectIsCapacity's contract is
        // "skip this build, do not evict": the tx must stay in the mempool for a later
        // block, so the eviction loop only excludes it from this candidate (finding AY).
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
            SchedulerType::computeTxRoot(op_detail::rawEnvelopes(payload)), parentBeaconBlockRoot);
    payload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*finalHeader);

    bcos::protocol::Block::Ptr finalBlock;
    try
    {
        finalBlock = buildOpBlock(payload, finalHeader);
    }
    catch (const OpExecutionInternalError& e)
    {
        if (auto invalid = op_detail::fcuInvalidIfUndecodable(e))
        {
            co_return *invalid;
        }
        throw;
    }
    m_delegate->reset([](bcos::Error::Ptr) {});
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
        auto guard = m_tracker.lockExclusive();
        publishBuiltPayload(guard, m_artifacts, payloadId,
            commonEntry->executionPayload.blockHash, std::move(commonEntry),
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
task::Task<PayloadStatus> OpEngineService<MemPoolType, GlobalStateStorageType,
    SchedulerType>::handleOpNewPayload(const NewPayloadRequest& request, std::uint32_t version)
{
    constexpr std::uint32_t c_opIsthmusPayloadVersion = 4;
    if (version != c_opIsthmusPayloadVersion)
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
task::Task<PayloadStatus> OpEngineService<MemPoolType, GlobalStateStorageType,
    SchedulerType>::runOpNewPayloadSteps(const NewPayloadRequest& request)
{
    {
        std::lock_guard lock(m_lastExecutedHeaderMutex);
        m_lastExecutedHeader.reset();
    }
    auto const& payload = request.executionPayload;

    if (auto validationError =
            engine_common::op::validateOpNewPayloadRequest(request, m_scheduler.isJovianActive());
        validationError.has_value())
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError);
    }

    const auto transactionsRoot = SchedulerType::computeTxRoot(op_detail::rawEnvelopes(payload));
    const auto ethHeader =
        engine_common::op::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(), payload,
            transactionsRoot, *request.parentBeaconBlockRoot);
    if (bcos::protocol::EthBlockHeader::computeHash(*ethHeader) != payload.blockHash)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("blockHash does not match the reconstructed block header"));
    }

    {
        bcos::protocol::BlockHeader::Ptr builtHeader;
        {
            auto shared = m_tracker.lockShared();
            builtHeader = op_detail::findBuiltHeader(shared, m_artifacts, payload.blockHash);
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
            m_delegate->commitBlock(
                builtHeader, [&](bcos::Error::Ptr error, bcos::ledger::LedgerConfig::Ptr) {
                    commitError = std::move(error);
                });
            if (commitError)
            {
                co_return mapDelegateError(*commitError, std::nullopt);
            }
            {
                std::lock_guard lock(m_lastExecutedHeaderMutex);
                m_lastExecutedHeader = builtHeader;
            }
            co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
        }
    }

    auto view = m_globalStateStorage.fork();
    auto parentBlockNumber =
        co_await bcos::ledger::getBlockNumber(view, payload.parentHash, bcos::ledger::fromStorage);
    if (!parentBlockNumber.has_value())
    {
        co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    }
    const auto latestValidHash = std::make_optional(payload.parentHash);

    if (payload.blockNumber != *parentBlockNumber + 1)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("blockNumber must be exactly one greater than the parent's"));
    }

    if (auto canonicalParent = co_await bcos::ledger::getBlockHash(
            view, *parentBlockNumber, bcos::ledger::fromStorage);
        !canonicalParent.has_value() || *canonicalParent != payload.parentHash)
    {
        co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    }

    const auto parentNumberStr = boost::lexical_cast<std::string>(*parentBlockNumber);
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
    bcos::protocol::BlockHeader::Ptr parentHeader;
    try
    {
        bcos::bytes parentHeaderBytes(storedHeader.begin(), storedHeader.end());
        parentHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader(parentHeaderBytes);
    }
    catch (const std::exception& e)
    {
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                  std::string("stored parent block header is undecodable: ") +
                                  e.what()});
    }
    if (parentHeader->number() != static_cast<int64_t>(*parentBlockNumber))
    {
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                  "stored parent block header height mismatch"});
    }
    if (static_cast<uint64_t>(payload.timestamp) <=
        static_cast<uint64_t>(parentHeader->timestamp()))
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("timestamp must be strictly greater than the parent's"));
    }
    {
        auto expectedBaseFee = calcOpBaseFee(*parentHeader, m_scheduler.isJovianActive());
        if (payload.baseFeePerGas != expectedBaseFee)
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("baseFeePerGas does not match the value computed "
                            "from the parent"));
        }
    }

    if (auto knownBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, payload.blockHash, bcos::ledger::fromStorage);
        knownBlockNumber.has_value())
    {
        co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
    }

    const auto childNumberStr = boost::lexical_cast<std::string>(payload.blockNumber);
    if (auto occupiedHeight = co_await storage2::readOne(
            view, executor_v1::StateKeyView{ledger::SYS_NUMBER_2_HASH, childNumberStr});
        occupiedHeight.has_value())
    {
        bool siblingOfTip = false;
        if (payload.blockNumber > 0)
        {
            auto currentNumber =
                co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
            auto canonicalParent = co_await bcos::ledger::getBlockHash(
                view, payload.blockNumber - 1, bcos::ledger::fromStorage);
            siblingOfTip = currentNumber == payload.blockNumber && canonicalParent.has_value() &&
                           *canonicalParent == payload.parentHash;
        }
        if (!siblingOfTip)
        {
            // Occupied height that is not a tip sibling: the forked view cannot
            // apply this payload. Engine API answers SYNCING (CL retries), not
            // -32603 OpExecutionInternalError. op-geth would InsertBlockWithoutSetHead
            // and return VALID; this node has no side-chain store.
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
    }

    requireDelegate();

    // Payload-content fault: an envelope the CL submitted cannot be decoded into a
    // transaction. op-geth answers INVALID at block construction for this class; it is
    // not a node-internal fault, so it must not surface as -32603 (finding AM). Internal
    // faults (storage, delegate) still throw and map to -32603 by the caller.
    bcos::protocol::Block::Ptr block;
    try
    {
        block = buildOpBlock(payload, ethHeader);
    }
    catch (const OpExecutionInternalError& e)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("undecodable payload transaction envelope: ") +
                boost::diagnostic_information(e));
    }

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

    {
        std::lock_guard lock(m_lastExecutedHeaderMutex);
        m_lastExecutedHeader = executedHeader;
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
    for (auto const& env : op_detail::rawEnvelopes(payload))
    {
        const auto txHash = hashImpl.hash(env);
        auto tarsTx = engine_common::op::opEnvelopeToTars(env, txHash);
        if (!tarsTx)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << OpPayloadUndecodable{true}
                                                             << bcos::errinfo_comment{
                                                                    "undecodable payload "
                                                                    "transaction envelope"});
        }
        tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
        auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
            [tars = std::move(*tarsTx)]() mutable { return &tars; });
        block->appendTransaction(std::move(tx));
    }
    return block;
}

}  // namespace bcos::engine
