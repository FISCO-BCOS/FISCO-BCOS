#pragma once

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <opstack-executor/OpCommitments.h>
// OpBlockCommitments / payloadBloomToH2048 / toBcosH256
#include <opstack-executor/OpCommon.h>
// toBlockInfo / narrowU256ToU64 / toEvmcBytes32 / OpBlockSeal
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>
#include <algorithm>
#include <array>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::evm::opstack
{
/// One transaction within a block: deposit or normal tx (a normal tx must carry a signed envelope
/// for L1 fee calculation).
struct OpBlockTx
{
    std::variant<DepositTx, evmone::state::Transaction> tx;
    evmc::bytes signedEnvelope;  // empty for deposit
};

/// Block execution result. txTypes[i] is the EIP-2718 type byte for receipts[i] (the FISCO
/// receipt has no tx-type slot; sealOpBlock's EncodeIndex leaf needs it).
struct OpBlockResult
{
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
    std::vector<uint8_t> txTypes;
    int64_t gasUsed = 0;
    evmone::state::StateDiff finalizeDiff;  // end-of-block finalize output
};

/// Execute a whole block (system_call → L1 deposit → fee → per-tx → finalize). **Discard-writes
/// contract**: on any throw the caller must discard all writes already applied (op-geth Process
/// semantics). Throws OpConsensusError on block-level errors, including errors normalized from
/// runDeposit/opTransition.
/// Every normal (non-deposit) transaction must carry a non-empty signedEnvelope: the
/// envelope↔mirror cross-check rejects an empty envelope as a hard whole-block rejection
/// (OpConsensusError), mirroring the per-tx path.
OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff);


// ---- Jovian L1-attributes block shape ----
// The L1-attributes deposit's calldata is 176B on the Jovian activation block, 178B with the
// Jovian selector thereafter.
inline constexpr std::size_t IsthmusL1AttributesLen = 176;
inline constexpr std::size_t JovianL1AttributesLen = 178;
inline constexpr std::array<uint8_t, 4> JovianL1AttributesSelector = {0x3d, 0xb6, 0xbe, 0x2b};

/// Shared Jovian L1-attributes shape (selector/length + activation deposits-only).
/// `lastTxIsDeposit` is the path-specific last-tx probe: processOpBlock uses the DepositTx
/// variant; preBlockOpSteps uses the raw envelope type byte. No-op pre-Jovian.
inline void validateJovianL1AttributesShape(
    std::span<uint8_t const> data, bool lastTxIsDeposit, OpForkConfig const& cfg)
{
    if (!cfg.has_da_footprint)
        return;
    if (data.size() == IsthmusL1AttributesLen)
    {
        // Jovian activation block: Isthmus-length attributes, must be deposits-only (op-geth
        // rollup_cost.go:568-576). Checking the last tx suffices (deposits always precede
        // non-deposits).
        if (!lastTxIsDeposit)
            throw OpConsensusError(
                "op block: unexpected non-deposit transactions in Jovian activation block");
        return;
    }
    if (data.size() < JovianL1AttributesLen)
        throw OpConsensusError(
            "op block: L1 attributes transaction data too short for DA footprint gas scalar");
    if (!std::equal(
            JovianL1AttributesSelector.begin(), JovianL1AttributesSelector.end(), data.begin()))
        throw OpConsensusError(
            "op block: L1 attributes transaction data does not have Jovian selector");
}

/// DA footprint gas scalar from L1-attributes calldata: Isthmus 176B → 0; Jovian ≥178B →
/// big-endian uint16 at [176:178]. nullopt when the length is neither (shape validation should
/// already have rejected that case on a Jovian block).
[[nodiscard]] inline std::optional<uint16_t> jovianDaFootprintGasScalar(
    std::span<uint8_t const> attrData)
{
    if (attrData.size() == IsthmusL1AttributesLen)
        return uint16_t{0};
    if (attrData.size() >= JovianL1AttributesLen)
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(attrData[JovianL1AttributesLen - 2]) << 8) |
            static_cast<uint16_t>(attrData[JovianL1AttributesLen - 1]));
    return std::nullopt;
}

/// Validate the Jovian L1-attributes block shape (selector/length + activation deposits-only).
/// No-op pre-Jovian. Throws OpConsensusError. Public wrapper around
/// validateJovianL1AttributesShape for the processOpBlock data shape (`span<OpBlockTx>`).
void validateJovianBlockShape(std::span<const OpBlockTx> txs, const OpForkConfig& cfg);

// ---- shared per-receipt helpers (one implementation shared with the per-tx loop) ----


/// Stricter-than-spec content check for the L1 attributes deposit (to==OP_L1_BLOCK &&
/// from==OP_DEPOSITOR); rejects hand-crafted payloads.
[[nodiscard]] inline bool isL1AttributesTx(const DepositTx& dep) noexcept
{
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}

/// Block finalize: no ommers / block reward; Prague requests suppressed (false throws).
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase);

// ---- seal: header commitment functions (OpBlockSeal struct lives in OpCommon.h) ----
using evmc::literals::operator""_bytes32;

/// Isthmus+ requestsHash = sha256("").
inline constexpr auto OP_EMPTY_REQUESTS_HASH =
    0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855_bytes32;

/// Single-account storage root (secure trie: key = keccak256(slot), value = rlp(trimmed)).
[[nodiscard]] evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// Compute the header commitments; messagePasserStorage is the complete, post-finalize live
/// MessagePasser slot map (not only this block's modified slots).
[[nodiscard]] OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage);

/// Receipts-root leaf, byte-for-byte op-geth `Receipts.EncodeIndex` semantics:
/// deposit 0x7E || rlp([status, cumGas, bloom, logs, nonce, version]);
/// normal  typed prefix + rlp([status, cumGas, bloom, logs]).
[[nodiscard]] bcos::bytes encodeReceiptForRoot(
    const bcos::protocol::TransactionReceipt& r, uint8_t txType);
}  // namespace bcos::evm::opstack

// ---- block registration + execution ----

namespace bcos::evm::engine
{
// DepositTx is built from the block's 0x7e envelope (OpstackExecutor::depositFromTransaction
// → decodeDepositEnvelope(extraTransactionBytes())). Never from tars mint/value mirrors.

// Forward-declared; defined at the end of this block.
template <class RawTxRange>
[[nodiscard]] bcos::h256 computeOpTxRoot(RawTxRange const& rawTxBytes);

/// Block-level finalization shared between the injection loop and the shared scheduler path:
/// finalizeBlock (MessagePasser snapshot) → seal → stateRoot → txRoot. txTypes are rebuilt from
/// rawTxBytes[i][0] (the FISCO receipt has no tx-type slot; sealOpBlock's EncodeIndex receipts-root
/// leaf needs the EIP-2718 type byte — mirror of the per-tx loop's classification). hashErr is
/// checked here (poisoned block-hash lookup → OpStorageError). **cumulativeGasUsed backfill is NOT
/// in scope** (it stays in the per-tx loop / ExecuteContext::finish).
///
/// @p skipStateRootBuild (①a incremental MPT): when true, the full two-layer rebuild is
/// skipped and the result's stateRoot is left EMPTY — the caller replaces it with the
/// incremental buildAndCollect root over the block delta (OpScheduler::execute). Only legal
/// when the caller's view top mutable layer is exactly this block's delta over a committed
/// parent. When false the root is computed ROOT-ONLY (nodes never retained — node persistence
/// lives in buildAndCollect / the genesis import, not here).
template <class Storage, class RawTxRange>
OpExecuteBlockResult finalizeOpBlockResult(bcos::executor_v1::opstack::OpstackExecutor& executor,
    Storage& view, bcos::protocol::BlockHeader const& header,
    bcos::ledger::LedgerConfig const& ledgerConfig, bcos::evm::opstack::OpForkConfig const& cfg,
    std::vector<bcos::protocol::TransactionReceipt::Ptr> const& receipts,
    RawTxRange const& rawTxBytes, int64_t cumulative, std::optional<std::string> const& hashErr,
    bool skipStateRootBuild = false)
{
    namespace op = bcos::evm::opstack;
    namespace detail = bcos::evm::engine::detail;

    bcos::task::syncWait(executor.finalizeBlock(view, header, ledgerConfig));

    // Rebuild txTypes via the shared classifyTxType helper (single home for the EIP-2718
    // classification so the deposit loop / this rebuild / processOpBlock can't drift).
    // Length guard: sealOpBlock iterates result.receipts and indexes txTypes[i] — a caller
    // passing mismatched lengths would read out of bounds (rawTxBytes and receipts are
    // independent parameters; lockstep callers are unaffected). Internal-invariant guard: a
    // length mismatch is a caller programming error, not a block-content rejection, so it is
    // classified as std::logic_error rather than OpConsensusError (INVALID).
    if (rawTxBytes.size() != receipts.size())
        throw std::logic_error("op block: receipts/rawTxBytes length mismatch (caller bug)");
    std::vector<uint8_t> txTypes;
    txTypes.reserve(rawTxBytes.size());
    for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
    {
        if (rawTxBytes[i].empty())  // defensive: the per-tx loop already rejects empty envelopes
            throw std::logic_error("op block: empty envelope (caller bug)");
        txTypes.emplace_back(op::classifyTxType(rawTxBytes[i][0]));
    }

    op::OpBlockResult result;
    result.receipts = receipts;
    result.txTypes = std::move(txTypes);
    result.gasUsed = cumulative;
    if (hashErr.has_value())
        throw OpStorageError("block-hash lookup failed: " + *hashErr);

    // Commitments: MessagePasser snapshot → seal → stateRoot → txRoot. accountStorage
    // returns the complete, tombstone-filtered live slot map for one address (same
    // fetchAllStorage used by visitAccounts). visitAccounts would still fetchAllStorage
    // every preceding /apps/ account before the visitor could stop at MessagePasser.
    std::map<evmc::bytes32, evmc::bytes32> mpStorage;
    bcos::evm::evmstate::Storage2State<Storage> bridge(view, executor.sharedError());
    mpStorage = bridge.accountStorage(op::OP_L2_TO_L1_MESSAGE_PASSER);
    if (bridge.poisoned())
        throw OpStorageError("poisoned: " + std::string(bridge.firstError()));
    auto seal = op::sealOpBlock(result, cfg, mpStorage);
    bcos::h256 stateRoot;
    if (!skipStateRootBuild)
    {
        auto const root = bcos::evm::stateRootOf(bridge);  // root-only; nodes not needed here
        if (bridge.poisoned())
            throw OpStorageError("poisoned after stateRootOf: " + std::string(bridge.firstError()));
        stateRoot = detail::toBcosH256(root);
    }
    auto txRoot = computeOpTxRoot(rawTxBytes);
    return OpExecuteBlockResult{
        std::move(result.receipts), seal, stateRoot, static_cast<uint64_t>(cumulative), txRoot};
}

// ---- block execution: block-pre steps + per-transaction execution ----
//
// runOpBlockInjection (the linear per-tx injection loop) was retired in Task 5 — its three
// responsibilities now live in: preBlockOpSteps (below, block-pre) +
// SchedulerSerialImpl(serial=true) (per-tx loop, driven by OpScheduler::execute) +
// finalizeOpBlockResult (block-post). executeDeposit survives on OpstackExecutor (eth_call uses it
// directly).

/// Block-pre steps shared by the OpScheduler execution path (and previously the retired
/// runOpBlockInjection): recent-block-hashes construction → system_call_block_start +
/// write-back → deposit-first content check + Jovian shape → DA footprint gas scalar. Outputs

/// hashes (emplaced in place — RecentBlockHashes holds a storage reference, not assignable, hence
/// the std::optional carrier), hashErr, and daFootprintGasScalar via reference params. Throws
/// OpConsensusError on shape/validation faults.
template <class Storage, class RawTxRange>
void preBlockOpSteps(Storage& view, bcos::protocol::BlockHeader const& header,
    bcos::evm::opstack::OpForkConfig const& cfg, RawTxRange const& rawTxBytes,
    std::vector<bcos::evm::opstack::DepositTx> const& deposits,
    bcos::executor_v1::opstack::OpstackExecutor& executor,
    std::optional<detail::RecentBlockHashes<Storage>>& hashes, std::optional<std::string>& hashErr,
    std::optional<uint16_t>& daFootprintGasScalar)
{
    namespace op = bcos::evm::opstack;

    auto blk = detail::toBlockInfo(header);
    hashes.emplace(
        view, blk.number, detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);
    bcos::evm::evmstate::Storage2State<Storage> stateView(view, executor.sharedError());

    // (1) Pre-block system call; write back through the bridge (the diff is sanitized first —
    // applyDiff's precondition). applyDiff poisons AND rethrows raw; a write-back failure is a
    // local storage fault, so it leaves as OpStorageError, never a bare runtime_error.
    auto sysDiff =
        evmone::state::system_call_block_start(stateView, blk, *hashes, cfg.rev, executor.vm());
    try
    {
        stateView.applyDiff(bcos::evm::sanitizeStateDiff(stateView, std::move(sysDiff)));
    }
    catch (const std::exception& e)
    {
        throw OpStorageError(std::string("pre-block system-call write-back failed: ") + e.what());
    }
    catch (...)
    {
        throw OpStorageError("pre-block system-call write-back failed: unknown exception");
    }
    // Read-path poison from the system call with applyDiff returning normally — same check as
    // the deposit/tx write-back paths in OpstackExecutor; without it a storage fault here would
    // silently execute as zero-value reads and surface later as a stateRoot mismatch.
    if (stateView.poisoned())
        throw OpStorageError("pre-block system-call poisoned: " + stateView.firstError());

    // (2) deposit-first content check + Jovian shape (type-byte classification, no raw-tx parse).
    constexpr uint8_t kDepositTypeByte = 0x7e;
    if (rawTxBytes.empty())
        throw OpConsensusError("op block: missing L1 attributes deposit (empty block)");
    // Empty-envelope guard: the first envelope must be non-empty before its type byte is read
    // (and before raw.back()[0] below). A block with NO deposit at all stays a hard reject: the
    // L1-attributes deposit seeds the block's fee/DA context and deposits[0] is read below.
    if (rawTxBytes[0].empty() || rawTxBytes[0][0] != kDepositTypeByte || deposits.empty())
        throw OpConsensusError("op block: no deposit transaction to seed the block");
    // First deposit is not L1 attributes: warn only. op-geth/op-reth accept this at validation.
    if (!op::isL1AttributesTx(deposits[0]))
        BCOS_LOG(WARNING) << LOG_BADGE("OP_BLOCK_EXEC")
                          << "op block: first tx is a deposit but not the L1 attributes tx — "
                             "accepted";
    if (cfg.has_da_footprint)
    {
        auto const& data = deposits[0].data;
        // Last-tx-only deposits-only check matches op-geth CalcDAFootprint
        // (core/types/rollup_cost.go:563-577): iterating every envelope would be stricter than
        // the reference client. Empty trailing envelope is treated as non-deposit.
        bool const lastTxIsDeposit =
            !rawTxBytes.back().empty() && rawTxBytes.back()[0] == kDepositTypeByte;
        op::validateJovianL1AttributesShape(
            std::span<uint8_t const>{data.data(), data.size()}, lastTxIsDeposit, cfg);
        if (auto scalar =
                op::jovianDaFootprintGasScalar(std::span<uint8_t const>{data.data(), data.size()}))
            daFootprintGasScalar = *scalar;
    }
}

/// Project the payload/header announced commitments into OpBlockCommitments (the "announced" side
/// of mismatchedFieldOf).
inline OpBlockCommitments announcedCommitmentsOf(const bcos::engine::ExecutionPayload& payload,
    const bcos::h256& transactionsRoot, const bcos::protocol::BlockHeader& ethHeader)
{
    // Isthmus+ payloads always carry withdrawalsRoot (upstream invariant), but if that ever lapses
    // the unconditional deref below would throw bad_optional_access (-> UnknownError). Guard it
    // into a clean consensus-level rejection naming the field (symmetric to mismatchedFieldOf's
    // report).
    if (!payload.withdrawalsRoot.has_value())
        throw OpConsensusError("op block: payload missing withdrawalsRoot");
    OpBlockCommitments out{
        .receiptsRoot = payload.receiptsRoot,
        .logsBloom = payloadBloomToH2048(payload.logsBloom),
        .withdrawalsRoot = *payload.withdrawalsRoot,
        .stateRoot = payload.stateRoot,
        .gasUsed = payload.gasUsed,
        .txRoot = transactionsRoot,
        .blobGasUsed = payload.blobGasUsed.has_value() ?
                           std::optional<uint64_t>(bcos::evm::engine::detail::narrowU256ToU64(
                               *payload.blobGasUsed, "ExecutionPayload.blobGasUsed")) :
                           std::nullopt,
        .requestsHash = ethHeader.requestsHash(),
    };
    return out;
}

/// transactionsRoot over raw EIP-2718 envelopes (trie key = rlp(index), value = raw wire bytes).
/// Matches op-geth's DeriveSha because the raw-tx decoders reject non-canonical encodings
/// (assertCanonicalRoundTrip fails closed if that lapses). Two call sites: the engine's
/// pre-execution blockHash check and finalizeOpBlockResult's txRoot.
/// Values are copied into owned bytes: computeTrieRootVarKey takes
/// span<pair<bytes, bytes>>, not a non-owning bytesConstRef. A non-owning overload
/// would drop this copy; not rewritten in this slice.
template <class RawTxRange>
[[nodiscard]] bcos::h256 computeOpTxRoot(RawTxRange const& rawTxBytes)
{
    std::vector<std::pair<bcos::bytes, bcos::bytes>> entries;
    entries.reserve(rawTxBytes.size());
    uint64_t index = 0;
    for (auto const& rawItem : rawTxBytes)
    {
        bcos::bytes key;
        bcos::codec::rlp::encode(key, index);
        entries.emplace_back(std::move(key), bcos::bytes(std::begin(rawItem), std::end(rawItem)));
        ++index;
    }
    return bcos::ledger::mpt::computeTrieRootVarKey(entries).root;
}
}  // namespace bcos::evm::engine
