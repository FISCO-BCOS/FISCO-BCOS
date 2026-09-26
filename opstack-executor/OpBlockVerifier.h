// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

/**
 * @file OpBlockVerifier.h
 * @brief OP-Stack block verifier for the devp2p sync path: executes a downloaded block
 *        against the local state, compares EVERY announced header commitment against the
 *        execution output, and commits block + state + ledger rows atomically — the OP
 *        semantic counterpart of the L1 EthereumBlockVerifier
 *        (transaction-scheduler/bcos-transaction-scheduler/EthereumBlockVerifier.h).
 *
 * Differences from the L1 verifier, all sourced from op-geth (optimism branch):
 *  - NO block-start EIP-4788/EIP-2935 handling here and NO block-end EIP-7002/7251 system
 *    calls: OP chains gate those inside the fork-aware execution path itself
 *    (preBlockOpSteps' system_call_block_start is revision-gated; Prague requests are
 *    suppressed by finalizeOpBlock).
 *  - NO withdrawals application and NO PoW rewards: OP blocks carry no withdrawals list
 *    (the Canyon..Holocene withdrawalsRoot is the constant empty-list hash; Isthmus+ it is
 *    the L2ToL1MessagePasser storage root) and pay no coinbase reward.
 *  - The first transaction of every block MUST be the L1-attributes deposit (0x7e);
 *    blob (0x03) and 0x7d type bytes are consensus-rejected.
 *  - Commitment surface is the six-way OpBlockCommitments comparison
 *    (receiptsRoot/logsBloom/withdrawalsRoot/stateRoot/gasUsed/transactionsRoot) plus the
 *    two seal-only outputs (blobGasUsed from Ecotone, requestsHash from Isthmus), with
 *    fork-gated field PRESENCE compared bidirectionally (mismatchedFieldOf).
 *
 * Why not drive OpScheduler::executeBlock/commitBlock: the scheduler's public entry is the
 * engine lane's two-phase execute→commit protocol (callback Error::Ptr surface, one pending
 * slot, probe/adopt). The sync verifier needs a single-shot atomic verify+commit, typed
 * exceptions carrying the mismatching field AND both values, and a faithful pre-Ecotone
 * header projection (OpScheduler's announced-hash identity via canonicalBlockHash only
 * covers withdrawalsRoot-bearing headers). The verifier therefore assembles the SAME shared
 * stages OpScheduler::execute runs — preBlockOpSteps → SchedulerSerialImpl(serial=true) →
 * finalizeOpBlockResult — plus the ledger MPT increment (ledger::mpt::computeMptStateDelta,
 * the L1 mechanism) and the FIB-104 commit sequence (pushView → prewriteBlockToBuffer →
 * mergeBackStorage), reusing the helpers rather than copying their logic.
 */

#include <opstack-executor/OpBlockExecute.h>  // preBlockOpSteps / finalizeOpBlockResult
#include <opstack-executor/OpCommitments.h>   // OpBlockCommitments / commitmentsOf / mismatchedFieldOf
#include <opstack-executor/OpCommon.h>        // OpConsensusError / forkTimestampSec / OpBlockSeal
#include <opstack-executor/OpstackExecutor.h> // OpstackExecutor / OpBlockExecutionContext

#include <bcos-devp2p/sync/Block.h>  // devp2p::sync::Block
#include <bcos-evm/adapter/RecentBlockHashes.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/FeaturesStorage.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/CommitObserver.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/MPTDeltaLayer.h>
#include <bcos-ledger/mpt/StateRoots.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Task.h>
#include <bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h>  // prepareMPTPruneRows
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/IOServicePool.h>
#include <fmt/format.h>
#include <boost/throw_exception.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bcos::executor_v1::opstack
{
#define OP_VERIFIER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("OP_BLOCK_VERIFIER")

/// The block to verify is not the direct child of the ledger head. A wrong-height block
/// reaching the verifier is sync-loop bookkeeping gone wrong, never a peer-supplied invalid
/// block, so the type lets the sync state machine classify it as a deterministic failure
/// (no retry against another peer can fix it). Same semantics — deliberately a distinct
/// type — as the L1 scheduler_v1::StaleOrOutOfOrderBlock (EthereumBlockVerifier.h).
struct OpStaleOrOutOfOrderBlock : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// A deterministic commitment mismatch between the executed block and the announced p2p
/// header: carries the mismatching field name and BOTH values so the sync state machine (and
/// the operator log) can see exactly which commitment diverged. Classified as an
/// OpConsensusError (INVALID) — the block re-fails identically for every peer.
struct OpBlockVerificationFailed : public bcos::evm::OpConsensusError
{
    std::string field;
    std::string computedValue;
    std::string announcedValue;

    OpBlockVerificationFailed(
        std::string field_, std::string computed_, std::string announced_)
      : bcos::evm::OpConsensusError(fmt::format(
            "OpBlockVerifier: commitment mismatch on field {} (computed={}, announced={})",
            field_, computed_, announced_)),
        field(std::move(field_)),
        computedValue(std::move(computed_)),
        announcedValue(std::move(announced_))
    {}
};

/// Successful verify+commit output. commitments are the EXECUTED block's commitments
/// (equal to the announced ones — the block would not have committed otherwise).
struct OpBlockVerificationResult
{
    protocol::BlockHeader::Ptr header;  // the committed (projected, verified) header
    std::vector<protocol::Transaction::Ptr> transactions;
    std::vector<protocol::TransactionReceipt::Ptr> receipts;
    bcos::evm::engine::OpBlockCommitments commitments;
    bcos::crypto::HashType blockHash;  // keccak256(rlp(header)) — the p2p identity
};

namespace detail
{
/// seconds -> milliseconds with an overflow guard (the internal BlockHeader stores ms; the
/// EthBlockHeader(BlockHeader) bridge back divides by 1000 and rejects sub-second values, so
/// the ×1000 here is the exact inverse — OpCommon.h's forkTimestampSec comment).
inline int64_t p2pTimestampToInternalMs(int64_t timestampSec)
{
    if (timestampSec < 0 || timestampSec > std::numeric_limits<int64_t>::max() / 1000)
    {
        BOOST_THROW_EXCEPTION(bcos::evm::OpConsensusError(
            "OpBlockVerifier: header timestamp out of the representable millisecond range"));
    }
    return timestampSec * 1000;
}

/// Project the devp2p-parsed OP header onto a FISCO BlockHeader. Every field is mirrored
/// verbatim; the fork-gated optionals (withdrawalsRoot Canyon+, blobGasUsed/excessBlobGas/
/// parentBeaconBlockRoot Ecotone+, requestsHash Isthmus+) follow the p2p header's PRESENCE —
/// never invent a value the fork's header shape lacks — so the projection re-encodes to the
/// identical RLP (the committed ledger header round-trips to the same block hash, and
/// EthBlockHeader::computeHash on the projection reproduces the announced hash). Isthmus+
/// withdrawalsRoot is the MessagePasser storage root and is passed through like any other
/// announced commitment; the six-way comparison decides whether execution reproduced it.
///
/// baseFee is mandatory on every OP header (Bedrock is post-London from genesis); a missing
/// baseFee is a consensus-level rejection, matching OpHeaderValidator's rule.
inline protocol::BlockHeader::Ptr projectOpP2pHeader(
    protocol::EthBlockHeaderData const& ethHeader, protocol::BlockFactory& blockFactory)
{
    if (!ethHeader.baseFee.has_value())
    {
        BOOST_THROW_EXCEPTION(bcos::evm::OpConsensusError(
            "OpBlockVerifier: OP header is missing baseFee (post-Bedrock chains are London+)"));
    }
    auto header = blockFactory.blockHeaderFactory()->createBlockHeader();
    header->setNumber(ethHeader.number);
    header->setTimestamp(p2pTimestampToInternalMs(ethHeader.timestamp));
    header->setParentInfo(protocol::ParentInfo{
        .blockNumber = ethHeader.number - 1, .blockHash = ethHeader.parentInfo.blockHash});
    header->setCoinbase(ethHeader.coinbase);
    // The p2p mixHash field IS prevRandao on every OP fork (post-merge from genesis).
    header->setPrevRandao(ethHeader.prevRandao);
    header->setGasLimit(ethHeader.gasLimit);
    header->setGasUsed(ethHeader.gasUsed);
    header->setStateRoot(ethHeader.stateRoot);
    header->setTxsRoot(ethHeader.txsRoot);
    header->setReceiptsRoot(ethHeader.receiptsRoot);
    header->setLogsBloom(
        bcos::bytesConstRef(ethHeader.logsBloom.data(), ethHeader.logsBloom.size()));
    header->setExtraData(ethHeader.extraData);
    // OP PoS constants: uncleHash is the empty-ommers hash, difficulty/nonce are zero
    // (OpHeaderValidator enforces the values; the projection mirrors them verbatim so the
    // RLP identity holds).
    header->setUncleHash(ethHeader.uncleHash);
    header->setDifficulty(ethHeader.difficulty);
    header->setNonce(ethHeader.nonce);
    header->setBaseFee(*ethHeader.baseFee);
    if (ethHeader.withdrawalsHash)
    {
        header->setWithdrawalsRoot(*ethHeader.withdrawalsHash);
    }
    if (ethHeader.blobGasUsed)
    {
        header->setBlobGasUsed(*ethHeader.blobGasUsed);
    }
    if (ethHeader.excessBlobGas)
    {
        header->setExcessBlobGas(*ethHeader.excessBlobGas);
    }
    if (ethHeader.parentBeaconRoot)
    {
        header->setParentBeaconBlockRoot(*ethHeader.parentBeaconRoot);
    }
    if (ethHeader.requestsHash)
    {
        header->setRequestsHash(*ethHeader.requestsHash);
    }
    return header;
}

/// Wrap one raw EIP-2718 envelope from the p2p block body into the executable tars
/// transaction the OP execution path consumes: the mirror fields come from the consensus
/// decode (Web3Transaction → takeToTarsTransaction, the same decode the engine lane's
/// opEnvelopeToTars runs), then extraTransactionBytes is overwritten with the EXACT wire
/// envelope (the executor reads the envelope for the type-byte classification, the deposit
/// decode, the L1-fee calldata cost and the envelope↔mirror binding — the same carrier
/// contract as the engine's decodedTransactionFromEnvelope).
///
/// The OP-admissible type-byte set is enforced here (op-geth rejects blob txs on OP chains):
/// legacy (>= 0xc0), 0x01/0x02/0x04 and the 0x7e deposit pass; 0x03/0x7d and any other typed
/// byte are a deterministic OpConsensusError. A decode failure (malformed RLP, non-canonical
/// integer, bad signature recovery) is likewise OpConsensusError, tagged with the envelope
/// hash.
inline protocol::Transaction::Ptr wrapOpP2pEnvelope(
    bcos::bytes const& raw, bcos::crypto::Hash const& hashImpl)
{
    auto const txHash = hashImpl.hash(bcos::bytesConstRef(raw.data(), raw.size()));
    if (raw.empty())
    {
        BOOST_THROW_EXCEPTION(bcos::evm::OpConsensusError(
            "OpBlockVerifier: empty transaction envelope", txHash));
    }
    auto const typeByte = static_cast<uint8_t>(raw[0]);
    constexpr uint8_t kRlpListBase = 0xc0;     // legacy RLP list prefix
    constexpr uint8_t kDepositTypeByte = 0x7e; // kDepositTxType (OpTransition.h)
    if (typeByte < kRlpListBase && typeByte != 0x01 && typeByte != 0x02 && typeByte != 0x04 &&
        typeByte != kDepositTypeByte)
    {
        // 0x03 (EIP-4844 blob) and 0x7d land here: not admissible on any OP fork.
        BOOST_THROW_EXCEPTION(bcos::evm::OpConsensusError(
            fmt::format("OpBlockVerifier: unsupported tx type byte 0x{:02x}",
                static_cast<unsigned>(typeByte)),
            txHash));
    }

    bcos::rpc::Web3Transaction web3Tx;
    bcos::bytesRef input(const_cast<bcos::byte*>(raw.data()), raw.size());
    try
    {
        if (!web3Tx.tryDecode(input) || !input.empty())
        {
            BOOST_THROW_EXCEPTION(bcos::evm::OpConsensusError(
                "OpBlockVerifier: undecodable transaction envelope (malformed RLP or "
                "trailing bytes)",
                txHash));
        }
    }
    catch (bcos::evm::OpConsensusError const&)
    {
        throw;
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(bcos::evm::OpConsensusError(
            std::string("OpBlockVerifier: transaction envelope decode failed: ") + e.what(),
            txHash));
    }

    auto tarsTx = web3Tx.takeToTarsTransaction();
    tarsTx.extraTransactionBytes.assign(raw.begin(), raw.end());
    tarsTx.extraTransactionHash.assign(txHash.begin(), txHash.end());
    if (tarsTx.sender.empty())
    {
        // Non-deposit arm: recover the sender from the signature so the executor can
        // validate nonce/balance without a separate recovery pass (deposits carry
        // sender == deposit.from from takeToTarsTransaction already).
        try
        {
            auto senderHex = web3Tx.sender();
            auto sender = bcos::fromHex(
                senderHex.rfind("0x", 0) == 0 ? senderHex.substr(2) : senderHex);
            tarsTx.sender.assign(sender.begin(), sender.end());
        }
        catch (std::exception const& e)
        {
            BOOST_THROW_EXCEPTION(bcos::evm::OpConsensusError(
                std::string("OpBlockVerifier: sender recovery failed: ") + e.what(), txHash));
        }
    }
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [m_tx = std::move(tarsTx)]() mutable { return &m_tx; });
}

/// The announced side of the six-way comparison, projected straight from the p2p header
/// (presence AND value for the fork-gated fields — mismatchedFieldOf treats presence
/// asymmetry as a first-class mismatch).
inline bcos::evm::engine::OpBlockCommitments announcedCommitmentsOf(
    protocol::EthBlockHeaderData const& ethHeader)
{
    std::optional<uint64_t> blobGasUsed;
    if (ethHeader.blobGasUsed.has_value())
    {
        if (!bcos::u256FitsUint64(*ethHeader.blobGasUsed))
        {
            BOOST_THROW_EXCEPTION(bcos::evm::OpConsensusError(
                "OpBlockVerifier: header blobGasUsed exceeds the uint64 range"));
        }
        blobGasUsed = static_cast<uint64_t>(*ethHeader.blobGasUsed);
    }
    return bcos::evm::engine::OpBlockCommitments{
        .receiptsRoot = ethHeader.receiptsRoot,
        .logsBloom = bcos::h2048(ethHeader.logsBloom.data(), ethHeader.logsBloom.size()),
        .withdrawalsRoot = ethHeader.withdrawalsHash,
        .stateRoot = ethHeader.stateRoot,
        .gasUsed = ethHeader.gasUsed,
        .txRoot = ethHeader.txsRoot,
        .blobGasUsed = blobGasUsed,
        .requestsHash = ethHeader.requestsHash,
    };
}

/// Render one commitment field for the OpBlockVerificationFailed diagnostic.
inline std::string renderCommitmentField(
    std::string_view field, bcos::evm::engine::OpBlockCommitments const& c)
{
    auto optHash = [](std::optional<bcos::h256> const& v) {
        return v ? v->hexPrefixed() : std::string("<absent>");
    };
    if (field == "receiptsRoot")
        return c.receiptsRoot.hexPrefixed();
    if (field == "logsBloom")
        return bcos::toHex(c.logsBloom).substr(0, 64);
    if (field == "withdrawalsRoot")
        return optHash(c.withdrawalsRoot);
    if (field == "stateRoot")
        return c.stateRoot.hexPrefixed();
    if (field == "gasUsed")
        return c.gasUsed.str();
    if (field == "transactionsRoot")
        return c.txRoot.hexPrefixed();
    if (field == "blobGasUsed")
        return c.blobGasUsed ? std::to_string(*c.blobGasUsed) : std::string("<absent>");
    if (field == "requestsHash")
        return optHash(c.requestsHash);
    return {};
}
}  // namespace detail

/// Executes + verifies + commits one OP block from devp2p sync. Blocks must be fed strictly
/// in order (the incremental MPT build resolves the parent block's trie nodes through the
/// executed view, persisted by the previous block's commit) — the height guard enforces it.
///
/// The class is a template over the MultiLayerStorage type (the same storage the engine-lane
/// OpScheduler serves); commit is serialized on m_commitMutex and follows the FIB-104
/// pattern: pushView → prewriteBlockToBuffer(+MPT prune rows) → mergeBackStorage, with
/// popFrontStorage rollback if anything after the push throws. The CommitObserver (MPT
/// pruning seam) matches the L1 verifier: null keeps the Noop.
template <class MultiLayerStorage>
class OpBlockVerifier
{
public:
    using ViewType = typename MultiLayerStorage::ViewType;

    OpBlockVerifier(bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory,
        bcos::crypto::Hash::Ptr hashImpl, uint64_t chainId,
        bcos::ledger::OpForkSchedule forkSchedule,
        bcos::protocol::BlockFactory::Ptr blockFactory, MultiLayerStorage& multiLayerStorage,
        bcos::ledger::LedgerInterface::Ptr ledger, bcos::IOServicePool::Ptr ioServicePool,
        std::shared_ptr<bcos::ledger::mpt::CommitObserver> commitObserver = nullptr)
      : m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_chainId(chainId),
        m_forkSchedule(forkSchedule),
        m_blockFactory(std::move(blockFactory)),
        m_multiLayerStorage(&multiLayerStorage),
        m_ledger(std::move(ledger)),
        m_ioServicePool(std::move(ioServicePool)),
        m_commitObserver(commitObserver ? std::move(commitObserver) :
                                          std::make_shared<bcos::ledger::mpt::NoopCommitObserver>())
    {}
    OpBlockVerifier(const OpBlockVerifier&) = delete;
    OpBlockVerifier& operator=(const OpBlockVerifier&) = delete;

    /// Execute @p block against the committed parent state, verify every announced header
    /// commitment, and commit atomically. Throws:
    ///  - OpStaleOrOutOfOrderBlock  — number != ledger head + 1 (sync-loop bookkeeping fault)
    ///  - OpBlockVerificationFailed — commitment mismatch (OpConsensusError subclass, carries
    ///    field name + both values)
    ///  - bcos::evm::OpConsensusError — inadmissible tx type byte, undecodable envelope,
    ///    block-shape violations (missing L1-attributes deposit, gas-pool overrun, ...)
    ///  - bcos::evm::engine::OpStorageError — storage/IO and MPT-trie faults
    /// On any throw nothing is committed (the executed view is discarded / the pushed view
    /// is rolled back).
    task::Task<OpBlockVerificationResult> verifyAndCommit(bcos::devp2p::sync::Block const& block)
    {
        namespace op = bcos::evm::opstack;
        namespace engine = bcos::evm::engine;
        namespace edetail = bcos::evm::engine::detail;

        auto const& ethHeader = block.header;
        auto const number = ethHeader.number;

        // Block identity: the downloader's hash must be keccak256(rlp(header)). A mismatch
        // is a wiring fault in the devp2p assembly (M4b), not peer-supplied block content —
        // the header RLP is the only input to both sides.
        auto const blockHash = bcos::protocol::ethHeaderHash(ethHeader);
        if (blockHash != block.hash)
        {
            throw std::logic_error(
                "OpBlockVerifier: block.hash does not match keccak256(rlp(header)) — "
                "devp2p Block assembly must fill hash from the header RLP");
        }

        // 1. Fork the execution view over the COMMITTED state (an engine-lane pending layer
        //    must not leak into sync execution — OpScheduler::coExecuteBlock forks committed
        //    for the same reason), then the height guard: the block must be the direct child
        //    of the ledger head this call commits on top of. The view is purely local until
        //    step 8's pushView, so throwing here discards it with zero state pollution.
        auto view = m_multiLayerStorage->forkCommitted();
        view.newMutable();
        auto const currentNumber =
            co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
        if (number != currentNumber + 1)
        {
            BOOST_THROW_EXCEPTION(OpStaleOrOutOfOrderBlock{
                fmt::format("OpBlockVerifier: block number {} is not the ledger head + 1 "
                            "(head {}): refusing to execute a stale or out-of-order block",
                    number, currentNumber)});
        }

        // 2. Fork resolution: the p2p header timestamp is SECONDS (EthBlockHeaderData keeps
        //    the Ethereum RLP domain); op-node keys forks on the L2 block's own timestamp, so
        //    no unit conversion is needed here — the internal millisecond conversion happens
        //    once, in the header projection below.
        const auto& cfg = op::configAt(m_forkSchedule, static_cast<uint64_t>(ethHeader.timestamp));

        // 3. Header projection (seconds -> ms inside), then a fidelity self-check: the
        //    projection must re-encode to the announced block hash on EVERY fork (the
        //    committed ledger header round-trips through EthBlockHeader::computeHash on
        //    resume / hash-keyed reads). A divergence is a verifier bug, not block content.
        auto header = detail::projectOpP2pHeader(ethHeader, *m_blockFactory);
        if (bcos::protocol::EthBlockHeader::computeHash(*header) != blockHash)
        {
            throw std::logic_error(
                "OpBlockVerifier: header projection is not RLP-faithful (re-encoded hash "
                "differs from the announced block hash)");
        }

        //    Feature set + the execution LedgerConfig (revision from the fork config;
        //    executor_version pinned to the OP lane — this verifier only runs on
        //    executor_version >= OPSTACK_EXECUTOR_VERSION chains, and computeMptStateDelta's
        //    l2Mode plus the parent-root rule branch on it).
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, number);
        bcos::ledger::LedgerConfig execLedgerConfig;
        execLedgerConfig.setBlockNumber(number);
        execLedgerConfig.setExecutorVersion(bcos::ledger::OPSTACK_EXECUTOR_VERSION);
        execLedgerConfig.setEVMCRevision(cfg.rev);
        execLedgerConfig.setFeatures(features);

        // 4. Wrap every raw envelope into the executable tars carrier (0x03/0x7d and
        //    malformed envelopes are deterministic OpConsensusError here), and split the
        //    deposits for the block-pre steps (depositFromTransaction decodes the 0x7e
        //    envelope; a malformed deposit is consensus-rejected, same as OpScheduler).
        OpBlockVerificationResult result;
        result.transactions.reserve(block.transactions.size());
        std::vector<bcos::bytesConstRef> rawTxBytes;
        rawTxBytes.reserve(block.transactions.size());
        std::vector<op::DepositTx> deposits;
        for (auto const& raw : block.transactions)
        {
            auto tx = detail::wrapOpP2pEnvelope(raw, *m_hashImpl);
            if (tx->isDepositTx())
            {
                try
                {
                    deposits.push_back(OpstackExecutor::depositFromTransaction(*tx));
                }
                catch (const OpTxValidationFailed& e)
                {
                    BOOST_THROW_EXCEPTION(bcos::evm::OpConsensusError(
                        std::string("OpBlockVerifier: malformed deposit: ") + e.what(),
                        tx->hash()));
                }
            }
            rawTxBytes.emplace_back(raw.data(), raw.size());
            result.transactions.push_back(std::move(tx));
        }

        // 5. Execute: the same shared stages OpScheduler::execute runs — preBlockOpSteps
        //    (recent-block-hashes → block-start system call → deposit-first/Jovian shape) →
        //    SchedulerSerialImpl(serial=true) per-tx loop → finalizeOpBlockResult with the
        //    state-root build deferred to the incremental MPT below.
        std::shared_ptr<SharedErrorSlot> sharedError;
        auto rethrowStorageFaultIfPoisoned = [&sharedError]() {
            if (!sharedError)
                return;
            std::lock_guard lock(sharedError->mutex);
            if (!sharedError->message.empty())
                throw engine::OpStorageError(
                    "OpBlockVerifier: block state read fault (poisoned): " + sharedError->message);
        };
        engine::OpExecuteBlockResult opResult;
        try
        {
            sharedError = std::make_shared<SharedErrorSlot>();
            OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg, sharedError);

            std::optional<std::string> hashErr;
            std::optional<uint16_t> daFootprintGasScalar;
            std::optional<edetail::RecentBlockHashes<ViewType>> hashes;
            engine::preBlockOpSteps(view, *header, cfg, rawTxBytes, deposits, executor, hashes,
                hashErr, daFootprintGasScalar);

            OpBlockExecutionContext ctx{.fee = {},
                .blockGasLeft =
                    edetail::narrowU256ToI64(header->gasLimit(), "OpBlockVerifier blockGasLeft"),
                .blockHashes = &*hashes,
                .chainId = m_chainId,
                .daFootprintGasScalar = daFootprintGasScalar};

            // Linear per-tx loop (serial=true, chunk size 1) — deposit order, blockGasLeft
            // and state-diff visibility forbid a parallel scheduler.
            bcos::scheduler_v1::SchedulerSerialImpl serialScheduler(
                m_ioServicePool, /*chunkSize=*/1, /*serial=*/true);
            auto transactionsRefs =
                result.transactions |
                ::ranges::views::transform(
                    [](protocol::Transaction::Ptr const& ptr) -> protocol::Transaction const& {
                        return *ptr;
                    });
            auto receipts = co_await serialScheduler.executeBlock(
                view, executor, *header, transactionsRefs, execLedgerConfig, ctx);

            // skipStateRootBuild=true: the root comes from the incremental MPT delta below
            // (the same view whose top mutable layer is exactly this block's delta), not from
            // finalize's full two-layer rebuild.
            opResult = engine::finalizeOpBlockResult(executor, view, *header, execLedgerConfig,
                cfg, receipts, rawTxBytes, ctx.cumulativeGasUsed, hashErr,
                /*skipStateRootBuild=*/true);
        }
        catch (const bcos::evm::OpConsensusError&)
        {
            // A poisoned slot is a storage fault even when validation wrapped it as consensus
            // (Storage2State reads are noexcept and swallow the fault into the shared slot) —
            // the same check OpScheduler::execute runs for every exception type.
            rethrowStorageFaultIfPoisoned();
            throw;
        }
        catch (const engine::OpStorageError&)
        {
            throw;
        }
        catch (const std::exception&)
        {
            rethrowStorageFaultIfPoisoned();
            throw;
        }
        catch (...)
        {
            rethrowStorageFaultIfPoisoned();
            throw bcos::evm::OpConsensusError(
                "OpBlockVerifier: execute threw an unrecognized (non-std::exception) object");
        }
        result.receipts = opResult.receipts;

        // 6. State root: the incremental world-state MPT over the executed view, from the
        //    parent block's committed state root (computeMptStateDelta — the L1 mechanism;
        //    l2Mode follows the executor_version pinned on execLedgerConfig above). The new
        //    trie nodes land in the view's top mutable layer and commit WITH the block, so
        //    the next block's incremental build resolves its parent nodes. Missing parent
        //    nodes are a storage fault (MPTInvariantViolation), never a silent empty-trie
        //    rebuild.
        auto const parentStateRoot = co_await ledger::mpt::parentStateRootFor(
            view, bcos::ledger::OPSTACK_EXECUTOR_VERSION, features, number, *m_blockFactory);
        ledger::mpt::MPTDeltaLayer mptDelta;
        try
        {
            mptDelta = co_await ledger::mpt::computeMptStateDelta(
                view, parentStateRoot, execLedgerConfig, m_commitObserver->needsRefCountDeltas());
        }
        catch (const ledger::mpt::MPTInvariantViolation& e)
        {
            throw engine::OpStorageError(fmt::format(
                "OpBlockVerifier: incremental MPT build at block {} failed — parent block {}'s "
                "state root {} has no persisted trie nodes (the OP lane builds the complete MPT "
                "from genesis): {}",
                number, number - 1, parentStateRoot.hex(), e.what()));
        }
        catch (const ledger::mpt::MPTDecodeError& e)
        {
            throw engine::OpStorageError(fmt::format(
                "OpBlockVerifier: MPT node decode failed at block {}: {}", number, e.what()));
        }
        opResult.stateRoot = mptDelta.stateRoot;

        // 7. Commitment comparison: executed vs announced, all six fields plus the two
        //    seal-only outputs, presence compared bidirectionally. The mismatch error names
        //    the field and carries both values.
        auto computed = engine::commitmentsOf(
            opResult.seal, opResult.stateRoot, opResult.gasUsed, opResult.txRoot);
        auto announced = detail::announcedCommitmentsOf(ethHeader);
        if (auto mismatch = engine::mismatchedFieldOf(computed, announced))
        {
            BOOST_THROW_EXCEPTION((OpBlockVerificationFailed{*mismatch,
                detail::renderCommitmentField(*mismatch, computed),
                detail::renderCommitmentField(*mismatch, announced)}));
        }
        result.commitments = computed;
        result.blockHash = blockHash;

        // 8. Commit: FIB-104 pattern — push the executed view, then merge the ledger prewrite
        //    buffer atomically; on any failure after the push, roll the pushed view back
        //    (popFrontStorage) so a dirty layer never stays on the storage stack. The
        //    [prepareMPTPruneRows -> merge -> onCommit] triple holds m_commitMutex (the
        //    BaselineSchedulerMPTHelpers.h serialization contract); onCommit fires only after
        //    the WriteBatch landed, outside the rollback scope.
        std::unique_lock commitLock(m_commitMutex);
        m_multiLayerStorage->pushView(std::move(view));
        try
        {
            typename MultiLayerStorage::MutableStorage prewriteStorage;
            auto outBlock = m_blockFactory->createBlock();
            // The p2p block hash is keccak256(rlp(header)); inject it so the ledger metadata
            // (SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER) and any header->hash() reader see the
            // chain's real hash. BlockImpl::setBlockHeader COPIES the header, so the RLP hash
            // must be set first (EthereumBlockVerifier's commit step does the same).
            header->setRLPHash(blockHash);
            outBlock->setBlockHeader(header);
            for (auto const& tx : result.transactions)
            {
                // Same as OpScheduler::commitPersist: the stored copy carries no inner tars
                // object; the raw envelope (extraTransactionBytes) is the persisted form.
                tx->setStoreToBackend(false);
                outBlock->appendTransaction(tx);
            }
            for (auto const& receipt : result.receipts)
            {
                outBlock->appendReceipt(receipt);
            }
            auto blockTxs = std::make_shared<protocol::ConstTransactions>(
                result.transactions |
                ::ranges::views::transform([](auto const& tx) {
                    return protocol::Transaction::ConstPtr(tx);
                }) |
                ::ranges::to<std::vector>());
            // blockHashOverride keys the hash rows by the p2p identity; writeNonces=false
            // matches the OP commit path (OpScheduler::commitPersist).
            co_await ledger::prewriteBlockToBuffer(*m_ledger, blockTxs, outBlock,
                prewriteStorage, blockHash, /*writeNonces=*/false);
            // The deletions of expired "/mpt/" node rows land in the SAME WriteBatch as the
            // block data (crash-atomicity contract, CommitObserver.h); a Noop observer
            // returns an empty batch.
            co_await scheduler_v1::prepareMPTPruneRows(
                *m_commitObserver, number, mptDelta, prewriteStorage);
            co_await m_multiLayerStorage->mergeBackStorage(prewriteStorage);
        }
        catch (...)
        {
            m_multiLayerStorage->popFrontStorage();
            throw;
        }
        // CommitObserver timing contract: AFTER the block's WriteBatch landed (Noop and
        // MPTPruner never throw here).
        m_commitObserver->onCommit(number, mptDelta);

        result.header = std::move(header);
        co_return result;
    }

private:
    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    uint64_t m_chainId;
    bcos::ledger::OpForkSchedule m_forkSchedule;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    MultiLayerStorage* m_multiLayerStorage;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::IOServicePool::Ptr m_ioServicePool;
    std::shared_ptr<bcos::ledger::mpt::CommitObserver> m_commitObserver;
    // Serializes the commit section's [prepareMPTPruneRows -> merge -> onCommit] triple
    // against every other commit feeding the same observer (the L1 verifier keeps the same
    // mutex for the same reason). The sync loop commits strictly sequentially; the mutex
    // keeps the contract independent of that caller property.
    std::mutex m_commitMutex;
};

}  // namespace bcos::executor_v1::opstack
