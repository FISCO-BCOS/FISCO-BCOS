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
#include <bcos-utilities/Common.h>
#include <opstack-executor/OpCommitments.h>  // OpBlockCommitments / payloadBloomToH2048 / toBcosH256
#include <opstack-executor/OpCommon.h>  // toBlockInfo / narrowU256ToU64 / toEvmcBytes32 / OpBlockSeal
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>
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
/// semantics). Throws std::runtime_error on block-level errors.
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

/// Validate the Jovian L1-attributes block shape (selector/length + activation deposits-only).
/// No-op pre-Jovian. Throws std::runtime_error.
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

/// Compute the header commitments; messagePasserStorage = post-finalize MessagePasser snapshot.
[[nodiscard]] OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage);

/// Receipts-root leaf, byte-for-byte op-geth `Receipts.EncodeIndex` semantics:
/// deposit 0x7E || rlp([status, cumGas, bloom, logs, nonce, version]);
/// normal  typed prefix + rlp([status, cumGas, bloom, logs]).
[[nodiscard]] evmc::bytes encodeReceiptForRoot(
    const bcos::protocol::TransactionReceipt& r, uint8_t txType);
}  // namespace bcos::evm::opstack

// ---- block registration + execution ----

namespace bcos::evm::engine
{
// DepositTx is built from the block's tars Transaction objects (OpstackExecutor::
// depositFromTransaction, OpstackExecutor.h) — no raw-envelope RLP parse.

// Forward-declared; defined at the end of this block.
template <class RawTxRange>
[[nodiscard]] bcos::h256 computeOpTxRoot(RawTxRange const& rawTxBytes);

/// Block-level finalization shared between the injection loop and the shared scheduler path:
/// finalizeBlock (MessagePasser snapshot) → seal → stateRoot → txRoot. txTypes are rebuilt from
/// rawTxBytes[i][0] (the FISCO receipt has no tx-type slot; sealOpBlock's EncodeIndex receipts-root
/// leaf needs the EIP-2718 type byte — mirror of the per-tx loop's classification). hashErr is
/// checked here (poisoned block-hash lookup → OpStorageError). **cumulativeGasUsed backfill is NOT
/// in scope** (it stays in the per-tx loop / ExecuteContext::finish).
template <class Storage, class RawTxRange>
OpExecuteBlockResult finalizeOpBlockResult(bcos::executor_v1::opstack::OpstackExecutor& executor,
    Storage& view, bcos::protocol::BlockHeader const& header,
    bcos::ledger::LedgerConfig const& ledgerConfig, bcos::evm::opstack::OpForkConfig const& cfg,
    std::vector<bcos::protocol::TransactionReceipt::Ptr> const& receipts,
    RawTxRange const& rawTxBytes, int64_t cumulative, std::optional<std::string> const& hashErr)
{
    namespace op = bcos::evm::opstack;
    namespace detail = bcos::evm::engine::detail;

    bcos::task::syncWait(executor.finalizeBlock(view, header, ledgerConfig));

    // Rebuild txTypes via the shared classifyTxType helper (single home for the EIP-2718
    // classification so the deposit loop / this rebuild / processOpBlock can't drift).
    std::vector<uint8_t> txTypes;
    txTypes.reserve(rawTxBytes.size());
    for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
    {
        if (rawTxBytes[i].empty())  // defensive: the per-tx loop already rejects empty envelopes
            throw OpConsensusError("op block: empty envelope");
        txTypes.emplace_back(op::classifyTxType(rawTxBytes[i][0]));
    }

    op::OpBlockResult result;
    result.receipts = receipts;
    result.txTypes = std::move(txTypes);
    result.gasUsed = cumulative;
    if (hashErr.has_value())
        throw OpStorageError("block-hash lookup failed: " + *hashErr);

    // Commitments: MessagePasser snapshot → seal → stateRoot → txRoot.
    std::map<evmc::bytes32, evmc::bytes32> mpStorage;
    bcos::evm::evmstate::Storage2State<Storage> bridge(view, executor.sharedError());
    bridge.visitAccounts([&](auto const& acc) {
        if (acc.addr == op::OP_L2_TO_L1_MESSAGE_PASSER)
        {
            mpStorage = acc.storage;
            return false;
        }
        return true;
    });
    if (bridge.poisoned())
        throw OpStorageError("poisoned: " + std::string(bridge.firstError()));
    auto seal = op::sealOpBlock(result, cfg, mpStorage);
    auto root = bcos::evm::stateRootOf(bridge);
    if (bridge.poisoned())
        throw OpStorageError("poisoned after stateRootOf: " + std::string(bridge.firstError()));
    auto txRoot = computeOpTxRoot(rawTxBytes);
    return OpExecuteBlockResult{std::move(result.receipts), seal, detail::toBcosH256(root),
        static_cast<uint64_t>(cumulative), txRoot};
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
/// applyStateDiff → deposit-first content check + Jovian shape → DA footprint gas scalar. Outputs
/// hashes (emplaced in place — RecentBlockHashes holds a storage reference, not assignable, hence
/// the std::optional carrier), hashErr, and daFootprintGasScalar via reference params. Throws
/// OpConsensusError on shape/validation faults.
template <class Storage, class RawTxRange>
void preBlockOpSteps(Storage& view, bcos::protocol::BlockHeader const& header,
    bcos::evm::opstack::OpForkConfig const& cfg, RawTxRange const& rawTxBytes,
    std::vector<bcos::evm::opstack::DepositTx> const& deposits,
    bcos::executor_v1::opstack::OpstackExecutor& executor, bcos::crypto::Hash::Ptr const& hashImpl,
    std::optional<detail::RecentBlockHashes<Storage>>& hashes, std::optional<std::string>& hashErr,
    std::optional<uint16_t>& daFootprintGasScalar)
{
    namespace op = bcos::evm::opstack;
    namespace eth = bcos::executor_v1::eth;

    auto blk = detail::toBlockInfo(header);
    hashes.emplace(
        view, blk.number, detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);
    bcos::evm::evmstate::Storage2State<Storage> stateView(view, executor.sharedError());

    // (1) Pre-block system call.
    auto sysDiff =
        evmone::state::system_call_block_start(stateView, blk, *hashes, cfg.rev, executor.vm());
    bcos::task::syncWait(eth::applyStateDiff(
        view, bcos::evm::sanitizeStateDiff(stateView, sysDiff), cfg.rev, *hashImpl));

    // (2) deposit-first content check + Jovian shape (type-byte classification, no raw-tx parse).
    constexpr uint8_t kDepositTypeByte = 0x7e;
    if (rawTxBytes.empty())
        throw OpConsensusError("op block: missing L1 attributes deposit (empty block)");
    // Empty-envelope guard: the first envelope must be non-empty before its type byte is read
    // (and before raw.back()[0] below).
    if (rawTxBytes[0].empty() || rawTxBytes[0][0] != kDepositTypeByte || deposits.empty() ||
        !op::isL1AttributesTx(deposits[0]))
        throw OpConsensusError("op block: first tx is not the L1 attributes deposit");
    if (cfg.has_da_footprint)
    {
        auto const& data = deposits[0].data;
        if (data.size() == op::IsthmusL1AttributesLen)
        {
            // Empty-envelope guard before back()[0] access (trailing empty tx -> same reject path).
            if (rawTxBytes.back().empty() || rawTxBytes.back()[0] != kDepositTypeByte)
                throw OpConsensusError(
                    "op block: unexpected non-deposit transactions in Jovian activation block");
        }
        else
        {
            if (data.size() < op::JovianL1AttributesLen)
                throw OpConsensusError(
                    "op block: L1 attributes transaction data too short for DA footprint gas "
                    "scalar");
            if (!std::equal(op::JovianL1AttributesSelector.begin(),
                    op::JovianL1AttributesSelector.end(), data.begin()))
                throw OpConsensusError(
                    "op block: L1 attributes transaction data does not have Jovian selector");
        }
    }

    // (3) DA scalar (H1c): Jovian extracts big-endian uint16 from deposits[0].data[176:178]; the
    // 176B Isthmus activation attributes → 0.
    if (cfg.has_da_footprint)
    {
        auto const& attrData = deposits[0].data;
        if (attrData.size() == op::IsthmusL1AttributesLen)
            daFootprintGasScalar = 0;
        else if (attrData.size() >= op::JovianL1AttributesLen)
            daFootprintGasScalar = static_cast<uint16_t>(
                (static_cast<uint16_t>(attrData[op::JovianL1AttributesLen - 2]) << 8) |
                static_cast<uint16_t>(attrData[op::JovianL1AttributesLen - 1]));
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
