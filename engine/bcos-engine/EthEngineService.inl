/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/indirect.hpp>
#include <range/v3/view/transform.hpp>

#include <bcos-ledger/mpt/Constants.h>
#include <optional>

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
    if (payloadAttributes != nullptr)
    {
        if (auto validationError =
                engine_common::validatePayloadAttributes(*payloadAttributes, version);
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

    std::vector<protocol::Transaction::Ptr> sealedTxs;
    view.newMutable();
    if (!payloadAttributes->noTxPool.value_or(false))
    {
        m_memPool.remove(view);
        m_memPool.seal(m_blockTxCountLimit, view, std::back_inserter(sealedTxs));
    }

    // Payload ID: deterministic derive from attributes + parent (op-geth-aligned). Do not
    // switch back to a process-local sequence counter — that was release EngineServiceImpl
    // only and is not the EthEngineService cutover contract (option B).
    auto payloadIdOpt =
        engine_common::derivePayloadId(*payloadAttributes, forkchoiceState.headBlockHash, version);
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
    auto built = co_await buildPayload(forkchoiceState, *payloadAttributes, payloadId, version,
        nextBlockNumber, std::move(sealedTxs), view);

    auto commonEntry = std::make_shared<BuiltPayload>();
    commonEntry->version = engine_common::payloadShapeVersion(version);
    commonEntry->executionPayload = std::move(built.executionPayload);
    commonEntry->blockValue = 0;
    commonEntry->blobsBundle = std::nullopt;
    commonEntry->shouldOverrideBuilder = false;
    commonEntry->parentBeaconBlockRoot = payloadAttributes->parentBeaconBlockRoot;
    if (version == static_cast<std::uint32_t>(ApiVersion::V3))
    {
        commonEntry->blobsBundle = BlobsBundleV1{};
    }

    auto stagedArtifact = EthPayloadArtifacts<ViewType>{
        .view = std::make_shared<ViewType>(std::move(view)),
        .header = std::move(built.header),
        .receipts = std::move(built.receipts),
    };

    {
        auto guard = m_tracker.lockExclusive();
        publishBuiltPayload(guard, m_artifacts, payloadId,
            commonEntry->executionPayload.blockHash, std::move(commonEntry),
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
    if (auto validationError = detail::validateExecutionPayload(request.executionPayload, version);
        validationError.has_value())
    {
        auto status = validationError->find("blockHash") != std::string::npos ?
                          PayloadValidationStatus::InvalidBlockHash :
                          PayloadValidationStatus::Invalid;
        co_return engine_common::makeStatus(status, std::nullopt, validationError);
    }
    if (version <= 2 && request.parentBeaconBlockRoot.has_value())
    {
        co_return engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("parentBeaconBlockRoot is only valid for newPayloadV3 and later"));
    }
    if (version >= 3 && !request.parentBeaconBlockRoot.has_value())
    {
        co_return engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("parentBeaconBlockRoot must be a 32-byte hash for newPayloadV3 and "
                        "later"));
    }
    if (version >= 3 && !request.expectedBlobVersionedHashes.empty())
    {
        co_return engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("expectedBlobVersionedHashes must be empty (L2 forbids blob "
                        "transactions)"));
    }
    if (version >= 4)
    {
        if (!request.executionRequests.has_value() || !request.executionRequests->empty())
        {
            co_return engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string("executionRequests must be a present-but-empty list on this "
                            "chain"));
        }
    }

    BuiltPayloadPtr cached;
    PayloadID payloadId;
    std::optional<EthPayloadArtifacts<ViewType>> localArtifact;
    {
        auto guard = m_tracker.lockExclusive();
        auto const& forkchoiceState = guard.forkchoiceState();
        auto parentKnown = request.executionPayload.parentHash == forkchoiceState.headBlockHash ||
                           guard.payloadIdForHash(request.executionPayload.parentHash).has_value();
        if (!parentKnown)
        {
            co_return engine_common::makeStatus(
                PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }

        auto existingId = guard.payloadIdForHash(request.executionPayload.blockHash);
        if (!existingId)
        {
            // #5468 / finding E: op-geth executes (InsertBlockWithoutSetHead) before VALID.
            // An external payload this node did not build is not executed here yet.
            co_return engine_common::makeStatus(
                PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
        payloadId = *existingId;
        cached = guard.findPayload(payloadId);
        if (!cached)
        {
            co_return engine_common::makeStatus(
                PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
        if (auto mismatch =
                detail::compareWithBuiltPayload(request.executionPayload, cached->executionPayload))
        {
            co_return engine_common::makeStatus(
                PayloadValidationStatus::InvalidBlockHash, std::nullopt, mismatch);
        }
        auto artifactIt = m_artifacts.find(payloadId);
        if (artifactIt != m_artifacts.end() && artifactIt->second.view)
        {
            localArtifact = std::move(artifactIt->second);
            m_artifacts.erase(artifactIt);
        }
    }

    if (localArtifact && localArtifact->view)
    {
        m_globalStateStorage.pushView(std::move(*localArtifact->view));
        if (m_ledger && localArtifact->header)
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
            co_await m_globalStateStorage.mergeBackStorage(prewriteStorage);
        }
        else
        {
            co_await m_globalStateStorage.mergeBackStorage();
        }
    }

    // Fail-closed guard (finding AI): if no local artifact was available above, either
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
            co_return engine_common::makeStatus(
                PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
    }

    {
        auto guard = m_tracker.lockExclusive();
        // Keep the locally built body (executed at FCU). Do not rewrite from the CL request.
        eth_detail::commitRetainedPayload(
            guard, m_artifacts, payloadId, cached->executionPayload.blockHash, cached);
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
task::Task<typename EthEngineService<MemPoolType, GlobalStateStorageType, ExecutorType,
    SchedulerType>::BuildPayloadResult>
EthEngineService<MemPoolType, GlobalStateStorageType, ExecutorType, SchedulerType>::buildPayload(
    const ForkchoiceState& forkchoiceState, const PayloadAttributes& payloadAttributes,
    const PayloadID& payloadId, std::uint32_t version, bcos::protocol::BlockNumber nextBlockNumber,
    std::vector<protocol::Transaction::Ptr> sealedTxs, ViewType& view) const
{
    std::vector<EngineTransaction> engineTransactions;
    engineTransactions.reserve(
        payloadAttributes.transactions.value_or(std::vector<std::string>{}).size() +
        sealedTxs.size());
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
        emptyHeader->setStateRoot(co_await calculateStateRoot(view, emptyHeader->version()));
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
            .receipts = {}};
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

    auto executableTransactions =
        executionPayload.transactions | ::ranges::views::filter([](auto const& transaction) {
            return transaction.decoded != nullptr;
        }) |
        ::ranges::views::transform([](auto const& transaction) { return transaction.decoded; }) |
        ::ranges::to<std::vector>();
    auto receipts = co_await m_scheduler.executeBlock(view, m_executor, *blockHeader,
        executableTransactions | ::ranges::views::indirect, ledgerConfig);

    h256 txRoot = bcos::ledger::mpt::emptyRootHash();
    {
        auto& hashImpl = *m_blockFactory->cryptoSuite()->hashImpl();
        auto hasher = hashImpl.hasher();
        crypto::merkle::Merkle<std::remove_reference_t<decltype(hasher)>> merkle(hasher.clone());
        if (!executionPayload.transactions.empty())
        {
            auto txHashes =
                executionPayload.transactions | ::ranges::views::transform([](auto& tx) {
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

    h256 receiptRoot = bcos::ledger::mpt::emptyRootHash();
    {
        if (::ranges::any_of(receipts, [](auto& r) { return !r; }))
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"Null receipt returned by scheduler"});
        }
        auto& hashImpl = *m_blockFactory->cryptoSuite()->hashImpl();
        auto hasher = hashImpl.hasher();
        crypto::merkle::Merkle<std::remove_reference_t<decltype(hasher)>> merkle(hasher.clone());
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

    u256 totalGasUsed;
    Bloom logsBloom{};
    for (auto& receipt : receipts)
    {
        if (!receipt)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"Null receipt returned by scheduler"});
        }
        totalGasUsed += receipt->gasUsed();
        if (!receipt->logsBloom().empty())
        {
            orBloom(logsBloom, receipt->logsBloom());
        }
    }

    h256 stateRoot = co_await calculateStateRoot(view, blockHeader->version());
    blockHeader->setStateRoot(stateRoot);
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
        .receipts = std::move(receipts)};
}

template <class MemPoolType, class GlobalStateStorageType, class ExecutorType, class SchedulerType>
    requires executor_v1::TransactionExecutor<ExecutorType,
                 typename GlobalStateStorageType::ViewType> &&
             scheduler_v1::TransactionScheduler<SchedulerType,
                 typename GlobalStateStorageType::ViewType, ExecutorType,
                 std::vector<protocol::Transaction::Ptr>>
task::Task<h256> EthEngineService<MemPoolType, GlobalStateStorageType, ExecutorType,
    SchedulerType>::calculateStateRoot(ViewType& view, uint32_t blockVersion) const
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

}  // namespace bcos::engine
