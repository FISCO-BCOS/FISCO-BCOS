#pragma once

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <bcos-framework/engine/Constants.h>
#include <bcos-framework/engine/OpTime.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptNormalize.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>
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
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff,
    OpForkSchedule const* schedule, uint64_t parentTsSec);


// ---- Jovian L1-attributes block shape ----
// Lengths/selectors live in OpTransition.h (already included). Duplicating them here
// redefines IsthmusL1AttributesLen / JovianL1AttributesLen / JovianL1AttributesSelector.

/// Q5: any Jovian-or-later activation that is live at `blockTsSec` but not at `parentTsSec`.
/// Timestamps are Unix seconds (convert header millis with `unixSecondsFromInternalMillis`).
///
/// KNOWN DIVERGENCE from op-geth (deliberate, spec-strict): upstream has no timestamp-window
/// rule - its only deposits-only probe is length-keyed and inspects the LAST transaction
/// (core/types/rollup_cost.go:571-576), so a block ordered [non-deposit..., deposit] on an
/// activation timestamp is accepted by op-geth and rejected here. The corpus generator only
/// produces deposits-first blocks, so differential replay cannot cover the shape.
inline bool isNoUserTxActivationBlock(
    OpForkSchedule const& schedule, uint64_t parentTsSec, uint64_t blockTsSec)
{
    for (auto const& act : schedule.jovianAndLaterActivations())
    {
        if (blockTsSec >= act.timestamp && parentTsSec < act.timestamp)
            return true;
    }
    return false;
}

/// Q5 envelope probe: empty or non-0x7e is a non-deposit. Used to scan every envelope
/// on activation blocks. The 176B DA-footprint path is length/shape only.
template <class Envelope>
[[nodiscard]] inline bool envelopeIsDeposit(Envelope const& env) noexcept
{
    return !env.empty() && env[0] == static_cast<uint8_t>(kDepositTxType);
}

template <class RawTxRange>
[[nodiscard]] inline bool hasNonDepositEnvelope(RawTxRange const& rawTxBytes)
{
    for (auto const& env : rawTxBytes)
    {
        if (!envelopeIsDeposit(env))
            return true;
    }
    return false;
}

/// Q5 probe for `processOpBlock`. Same 0x7e rule as `hasNonDepositEnvelope` when
/// an envelope is present. Empty envelope keeps the `DepositTx` convention
/// (`OpBlockTx::signedEnvelope` is empty for deposits). Either side saying
/// non-deposit fails closed (variant user, or typed/empty-mismatch envelope).
[[nodiscard]] inline bool hasNonDepositTx(std::span<const OpBlockTx> txs) noexcept
{
    for (auto const& btx : txs)
    {
        if (!std::holds_alternative<DepositTx>(btx.tx))
            return true;
        if (!btx.signedEnvelope.empty() && !envelopeIsDeposit(btx.signedEnvelope))
            return true;
    }
    return false;
}

/// op-geth's form of the Jovian activation deposits-only rule: it inspects the LAST
/// transaction only ("sufficient to check last transaction because deposits precede
/// non-deposit txs"). Scanning every transaction instead would reject a block op-geth accepts.
[[nodiscard]] inline bool lastTxIsDeposit(std::span<const OpBlockTx> txs) noexcept
{
    if (txs.empty())
        return false;
    auto const& last = txs.back();
    if (!std::holds_alternative<DepositTx>(last.tx))
        return false;
    return last.signedEnvelope.empty() || envelopeIsDeposit(last.signedEnvelope);
}

/// Same last-transaction form, for callers that only have the raw envelopes.
template <class RawTxRange>
[[nodiscard]] inline bool lastEnvelopeIsDeposit(RawTxRange const& rawTxBytes)
{
    if (rawTxBytes.empty())
        return false;
    return envelopeIsDeposit(rawTxBytes.back());
}

/// Jovian L1-attributes shape (selector/length) plus op-geth's length-keyed deposits-only
/// rule. No-op pre-Jovian.
///
/// op-geth CalcDAFootprint: the Isthmus-length (176B) attributes form means the DA-footprint
/// gas scalar is not set yet, which is only legal for a deposits-only block. That rule keys on
/// the attributes LENGTH and the LAST transaction — never on a timestamp window. A separate
/// timestamp-window deposits-only rule exists as Q5 (`isNoUserTxActivationBlock`); neither
/// subsumes the other.
///
/// @param lastTxIsDeposit whether the block's last transaction is a deposit
inline void validateJovianL1AttributesShape(
    std::span<uint8_t const> data, OpForkConfig const& cfg, bool lastTxIsDeposit)
{
    if (!cfg.has_da_footprint)
        return;
    if (data.size() == IsthmusL1AttributesLen)
    {
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

/// Why daFootprintOfEnvelopes returned nullopt. Both classes are "fail closed" for consensus;
/// the distinction exists so the engine can report the actual cause.
enum class DaFootprintError : uint8_t
{
    None = 0,
    /// No leading L1-attributes deposit, or its calldata is malformed (bad length/selector).
    Malformed,
    /// The Σ would exceed uint64; op-geth's Go uint64 accumulator would wrap instead.
    Overflow,
};

/// Block-level DA footprint Σ (op-geth CalcDAFootprint, core/types/rollup_cost.go): the
/// scalar is read from the L1-attributes deposit (the first envelope), a 176-byte Isthmus-length
/// attributes payload is the Jovian activation form and contributes 0, and every non-deposit
/// envelope adds estimatedDaSizeFromFlz(flzCompressLen(env)) × scalar. Primitives are reused
/// (envelopeIsDeposit / decodeDepositEnvelope / jovianDaFootprintGasScalar / flzCompressLen /
/// estimatedDaSizeFromFlz) — no constant or offset is re-derived here. Header-inline so callers
/// (the engine's Jovian blobGasUsed equality gate) add no link dependency to their archive; the
/// inlined primitive chain in RollupCost.h is likewise inline for the same reason.
///
/// @return the Σ, or nullopt when it cannot be derived (the caller fails closed). The two
/// failure classes differ only in diagnostics — see DaFootprintError and @p error.
/// @param error optional; when nullopt is returned it is set to the failure class, so a caller
///        that wants to distinguish malformed input from a uint64 overflow can. Callers that do
///        not care may omit it (default nullptr) and treat every nullopt alike.
[[nodiscard]] inline std::optional<uint64_t> daFootprintOfEnvelopes(
    std::span<const bcos::bytesConstRef> envelopes, DaFootprintError* error = nullptr)
{
    auto fail = [error](DaFootprintError kind) -> std::optional<uint64_t> {
        if (error != nullptr)
        {
            *error = kind;
        }
        return std::nullopt;
    };
    // op-geth CalcDAFootprint requires the first transaction to be the L1-attributes deposit:
    // "missing deposit transaction" otherwise, and ExtractDAFootprintGasScalar rejects a bad
    // length/selector. All of those become Malformed.
    if (envelopes.empty() || !envelopeIsDeposit(envelopes.front()))
        return fail(DaFootprintError::Malformed);

    DepositTx dep;
    try
    {
        dep = bcos::executor_v1::opstack::decodeDepositEnvelope(envelopes.front());
    }
    catch (...)
    {
        return fail(DaFootprintError::Malformed);
    }
    auto const attr = std::span<uint8_t const>{dep.data.data(), dep.data.size()};
    // Isthmus-length attributes: the DA-footprint gas scalar is not set yet. On a Jovian block
    // that is only legal for a (deposits-only) activation block — op-geth returns 0 here; the
    // last-tx-is-deposit rule is enforced by validateJovianL1AttributesShape on the executor path.
    if (attr.size() == IsthmusL1AttributesLen)
        return uint64_t{0};
    if (attr.size() < JovianL1AttributesLen || !std::equal(JovianL1AttributesSelector.begin(),
                                                   JovianL1AttributesSelector.end(), attr.begin()))
        return fail(DaFootprintError::Malformed);
    auto const scalar = jovianDaFootprintGasScalar(attr);
    if (!scalar.has_value())
        return fail(DaFootprintError::Malformed);

    uint64_t sum = 0;
    for (auto const& env : envelopes)
    {
        if (envelopeIsDeposit(env))
            continue;
        auto const term =
            estimatedDaSizeFromFlz(flzCompressLen(evmc::bytes_view{env.data(), env.size()})) *
            static_cast<uint64_t>(*scalar);
        // KNOWN DIVERGENCE from op-geth (deliberate, fail-closed): upstream accumulates
        // into a Go uint64 and wraps. Wrapping would let a crafted block clear the equality
        // gate; this side rejects instead (no legitimate block overflows).
        if (sum > std::numeric_limits<uint64_t>::max() - term)
            return fail(DaFootprintError::Overflow);
        sum += term;
    }
    return sum;
}

/// Validate the Jovian L1-attributes block shape (selector/length). No-op pre-Jovian.
/// Throws OpConsensusError. Public wrapper around validateJovianL1AttributesShape
/// for the processOpBlock data shape (`span<OpBlockTx>`).
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
inline const evmc::bytes32 OP_EMPTY_REQUESTS_HASH = [] {
    // sha256("") — single-sourced from bcos::engine::c_emptyRequestsHashHex (framework):
    // the same value the engine header builders stamp (EngineServiceCommon.h
    // c_emptyRequestsHash), cast here into the seal's native type. Both are
    // keccak256(rlp(header))-critical, so the hex must have exactly one home.
    // 0x + 64 hex digits; a wrong-length edit must fail at compile time.
    static_assert(bcos::engine::c_emptyRequestsHashHex.size() == 66,
        "c_emptyRequestsHashHex must be 0x plus 32 bytes of hex");
    auto const raw = bcos::fromHex(std::string{bcos::engine::c_emptyRequestsHashHex});
    evmc::bytes32 hash{};
    if (raw.size() != sizeof(hash.bytes))
    {
        // Unreachable: the static_assert above pins the hex length, so fromHex always
        // yields 32 bytes. Kept as std::logic_error to match the seal's other
        // internal-invariant guards (see the length/empty-envelope checks below): a
        // builder bug, not a block-content rejection (which would be OpConsensusError).
        throw std::logic_error("c_emptyRequestsHashHex must decode to exactly 32 bytes");
    }
    std::copy(raw.begin(), raw.end(), hash.bytes);
    return hash;
}();

/// Single-account storage root (secure trie: key = keccak256(slot), value = rlp(trimmed)).
[[nodiscard]] evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// Compute the header commitments; messagePasserStorage is the complete, post-finalize live
/// MessagePasser slot map (not only this block's modified slots).
[[nodiscard]] OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage);

/// Receipts-root leaf, byte-for-byte op-geth `Receipts.EncodeIndex` semantics:
/// deposit 0x7E || rlp([status, cumGas, bloom, logs(, nonce, version)]); the version
/// word gates the leaf shape — Canyon+ appends nonce+version, while the pre-Canyon
/// (Regolith) receipt hash inadvertently omitted the nonce too. Meta presence must
/// agree with the fork in both directions: cfg decides, a mismatch is a consensus
/// error, never a silently different leaf.
[[nodiscard]] bcos::bytes encodeReceiptForRoot(
    const bcos::protocol::TransactionReceipt& r, uint8_t txType, const OpForkConfig& cfg);
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
/// checked here (poisoned block-hash lookup → OpStorageError). cumulativeGasUsed is set upstream in
/// ExecuteContext::finish and kept; normalizeReceipts below fills it only when absent.
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

    // One receipt-field policy for both receipts-root producers (the engine
    // buildHeaderCommitments and this OP seal): transactionIndex / logIndex are written, logsBloom
    // is recomputed unconditionally from logEntries, and cumulativeGasUsed is filled when empty
    // (the OP running prefix is set upstream in ExecuteContext::finish and kept). Closes the OP
    // eth_getLogs logIndex gap and the leaf-bloom provenance divergence (#5582).
    bcos::protocol::normalizeReceipts(result.receipts);

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

// ────────────────────────────────────────────────────────────────────────────
// create2Deployer (Canyon)
//
// op-geth deploys the create2Deployer contract at the Canyon activation block:
//   core/state_processor.go:81  misc.EnsureCreate2Deployer(config, block.Time(), statedb)
//   consensus/misc/create2deployer.go:34-40
//     if (!c.IsOptimism() || c.CanyonTime == nil || *c.CanyonTime != timestamp) return
//     db.SetCode(create2DeployerAddress, create2DeployerCode, ...)   // unconditional
//
// The gate is timestamp EQUALITY with CanyonTime, never `fork >= Canyon`; the write is
// unconditional (not "only if absent"). The three constants below are op-geth protocol
// constants (create2deployer.go:21-25): the embedded create2deployer.bin is 1584 B and its
// keccak256 is the declared hash. Mirroring the write is DEFENSIVE CONSISTENCY with upstream
// for a chain whose CanyonTime is later than genesis (a shape our op_fork_schedule supports);
// for the corpus/op-deployer all-forks-at-genesis shape Canyon activates at genesis, where this
// pre-block hook never runs, so no live chain is currently observed to diverge here.
// ────────────────────────────────────────────────────────────────────────────
// clang-format off
inline constexpr std::array<uint8_t, 20> c_create2DeployerAddressBytes = {
    0x13, 0xb0, 0xd8, 0x5c, 0xcb, 0x8b, 0xf8, 0x60, 0xb6, 0xb7,
    0x9a, 0xf3, 0x02, 0x9f, 0xca, 0x08, 0x1a, 0xe9, 0xbe, 0xf2};
inline constexpr std::array<uint8_t, 32> c_create2DeployerCodeHashBytes = {
    0xb0, 0x55, 0x0b, 0x5b, 0x43, 0x1e, 0x30, 0xd3, 0x80, 0x00, 0xef, 0xb7, 0x10, 0x7a, 0xaa, 0x0a,
    0xde, 0x03, 0xd4, 0x8a, 0x71, 0x98, 0xa1, 0x40, 0xed, 0xda, 0x9d, 0x27, 0x13, 0x44, 0x68, 0xb2};

inline evmc::address create2DeployerAddress()
{
    evmc::address addr{};
    std::memcpy(addr.bytes, c_create2DeployerAddressBytes.data(), sizeof(addr.bytes));
    return addr;
}

inline evmc::bytes32 create2DeployerCodeHash()
{
    evmc::bytes32 hash{};
    std::memcpy(hash.bytes, c_create2DeployerCodeHashBytes.data(), sizeof(hash.bytes));
    return hash;
}

inline constexpr std::array<uint8_t, 1584> c_create2DeployerCode = {
    0x60, 0x80, 0x60, 0x40, 0x52, 0x60, 0x04, 0x36, 0x10, 0x61, 0x00, 0x43, 0x57, 0x60, 0x00, 0x35,
    0x60, 0xe0, 0x1c, 0x80, 0x63, 0x07, 0x6c, 0x37, 0xb2, 0x14, 0x61, 0x00, 0x4f, 0x57, 0x80, 0x63,
    0x48, 0x12, 0x86, 0xe6, 0x14, 0x61, 0x00, 0x71, 0x57, 0x80, 0x63, 0x56, 0x29, 0x94, 0x81, 0x14,
    0x61, 0x00, 0xba, 0x57, 0x80, 0x63, 0x66, 0xcf, 0xa0, 0x57, 0x14, 0x61, 0x00, 0xda, 0x57, 0x60,
    0x00, 0x80, 0xfd, 0x5b, 0x36, 0x61, 0x00, 0x4a, 0x57, 0x00, 0x5b, 0x60, 0x00, 0x80, 0xfd, 0x5b,
    0x34, 0x80, 0x15, 0x61, 0x00, 0x5b, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x50, 0x61, 0x00, 0x6f,
    0x61, 0x00, 0x6a, 0x36, 0x60, 0x04, 0x61, 0x03, 0x27, 0x56, 0x5b, 0x61, 0x00, 0xfa, 0x56, 0x5b,
    0x00, 0x5b, 0x34, 0x80, 0x15, 0x61, 0x00, 0x7d, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x50, 0x61,
    0x00, 0x91, 0x61, 0x00, 0x8c, 0x36, 0x60, 0x04, 0x61, 0x03, 0x27, 0x56, 0x5b, 0x61, 0x01, 0x4a,
    0x56, 0x5b, 0x60, 0x40, 0x51, 0x73, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x90, 0x91, 0x16, 0x81, 0x52, 0x60,
    0x20, 0x01, 0x60, 0x40, 0x51, 0x80, 0x91, 0x03, 0x90, 0xf3, 0x5b, 0x34, 0x80, 0x15, 0x61, 0x00,
    0xc6, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x50, 0x61, 0x00, 0x91, 0x61, 0x00, 0xd5, 0x36, 0x60,
    0x04, 0x61, 0x03, 0x49, 0x56, 0x5b, 0x61, 0x01, 0x5d, 0x56, 0x5b, 0x34, 0x80, 0x15, 0x61, 0x00,
    0xe6, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x50, 0x61, 0x00, 0x6f, 0x61, 0x00, 0xf5, 0x36, 0x60,
    0x04, 0x61, 0x03, 0xca, 0x56, 0x5b, 0x61, 0x01, 0x72, 0x56, 0x5b, 0x61, 0x01, 0x45, 0x82, 0x82,
    0x60, 0x40, 0x51, 0x80, 0x60, 0x20, 0x01, 0x61, 0x01, 0x0f, 0x90, 0x61, 0x03, 0x1a, 0x56, 0x5b,
    0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xe0, 0x82, 0x82, 0x03, 0x81, 0x01, 0x83, 0x52, 0x60, 0x1f, 0x90, 0x91, 0x01, 0x16, 0x60, 0x40,
    0x52, 0x61, 0x01, 0x83, 0x56, 0x5b, 0x50, 0x50, 0x50, 0x56, 0x5b, 0x60, 0x00, 0x61, 0x01, 0x56,
    0x83, 0x83, 0x61, 0x02, 0xe7, 0x56, 0x5b, 0x93, 0x92, 0x50, 0x50, 0x50, 0x56, 0x5b, 0x60, 0x00,
    0x61, 0x01, 0x6a, 0x84, 0x84, 0x84, 0x61, 0x02, 0xf0, 0x56, 0x5b, 0x94, 0x93, 0x50, 0x50, 0x50,
    0x50, 0x56, 0x5b, 0x61, 0x01, 0x7d, 0x83, 0x83, 0x83, 0x61, 0x01, 0x83, 0x56, 0x5b, 0x50, 0x50,
    0x50, 0x50, 0x56, 0x5b, 0x60, 0x00, 0x83, 0x47, 0x10, 0x15, 0x61, 0x01, 0xf4, 0x57, 0x60, 0x40,
    0x51, 0x7f, 0x08, 0xc3, 0x79, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x81, 0x52, 0x60, 0x20, 0x60, 0x04, 0x82, 0x01, 0x52, 0x60, 0x1d, 0x60, 0x24, 0x82,
    0x01, 0x52, 0x7f, 0x43, 0x72, 0x65, 0x61, 0x74, 0x65, 0x32, 0x3a, 0x20, 0x69, 0x6e, 0x73, 0x75,
    0x66, 0x66, 0x69, 0x63, 0x69, 0x65, 0x6e, 0x74, 0x20, 0x62, 0x61, 0x6c, 0x61, 0x6e, 0x63, 0x65,
    0x00, 0x00, 0x00, 0x60, 0x44, 0x82, 0x01, 0x52, 0x60, 0x64, 0x01, 0x5b, 0x60, 0x40, 0x51, 0x80,
    0x91, 0x03, 0x90, 0xfd, 0x5b, 0x81, 0x51, 0x60, 0x00, 0x03, 0x61, 0x02, 0x5f, 0x57, 0x60, 0x40,
    0x51, 0x7f, 0x08, 0xc3, 0x79, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x81, 0x52, 0x60, 0x20, 0x60, 0x04, 0x82, 0x01, 0x81, 0x90, 0x52, 0x60, 0x24, 0x82,
    0x01, 0x52, 0x7f, 0x43, 0x72, 0x65, 0x61, 0x74, 0x65, 0x32, 0x3a, 0x20, 0x62, 0x79, 0x74, 0x65,
    0x63, 0x6f, 0x64, 0x65, 0x20, 0x6c, 0x65, 0x6e, 0x67, 0x74, 0x68, 0x20, 0x69, 0x73, 0x20, 0x7a,
    0x65, 0x72, 0x6f, 0x60, 0x44, 0x82, 0x01, 0x52, 0x60, 0x64, 0x01, 0x61, 0x01, 0xeb, 0x56, 0x5b,
    0x82, 0x82, 0x51, 0x60, 0x20, 0x84, 0x01, 0x86, 0xf5, 0x90, 0x50, 0x73, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x81, 0x16, 0x61, 0x01, 0x56, 0x57, 0x60, 0x40, 0x51, 0x7f, 0x08, 0xc3, 0x79, 0xa0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x52, 0x60, 0x20, 0x60, 0x04,
    0x82, 0x01, 0x52, 0x60, 0x19, 0x60, 0x24, 0x82, 0x01, 0x52, 0x7f, 0x43, 0x72, 0x65, 0x61, 0x74,
    0x65, 0x32, 0x3a, 0x20, 0x46, 0x61, 0x69, 0x6c, 0x65, 0x64, 0x20, 0x6f, 0x6e, 0x20, 0x64, 0x65,
    0x70, 0x6c, 0x6f, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x44, 0x82, 0x01, 0x52,
    0x60, 0x64, 0x01, 0x61, 0x01, 0xeb, 0x56, 0x5b, 0x60, 0x00, 0x61, 0x01, 0x56, 0x83, 0x83, 0x30,
    0x5b, 0x60, 0x00, 0x60, 0x40, 0x51, 0x83, 0x60, 0x40, 0x82, 0x01, 0x52, 0x84, 0x60, 0x20, 0x82,
    0x01, 0x52, 0x82, 0x81, 0x52, 0x60, 0x0b, 0x81, 0x01, 0x90, 0x50, 0x60, 0xff, 0x81, 0x53, 0x60,
    0x55, 0x90, 0x20, 0x94, 0x93, 0x50, 0x50, 0x50, 0x50, 0x56, 0x5b, 0x61, 0x01, 0x4e, 0x80, 0x61,
    0x04, 0xad, 0x83, 0x39, 0x01, 0x90, 0x56, 0x5b, 0x60, 0x00, 0x80, 0x60, 0x40, 0x83, 0x85, 0x03,
    0x12, 0x15, 0x61, 0x03, 0x3a, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x50, 0x50, 0x80, 0x35, 0x92,
    0x60, 0x20, 0x90, 0x91, 0x01, 0x35, 0x91, 0x50, 0x56, 0x5b, 0x60, 0x00, 0x80, 0x60, 0x00, 0x60,
    0x60, 0x84, 0x86, 0x03, 0x12, 0x15, 0x61, 0x03, 0x5e, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x83,
    0x35, 0x92, 0x50, 0x60, 0x20, 0x84, 0x01, 0x35, 0x91, 0x50, 0x60, 0x40, 0x84, 0x01, 0x35, 0x73,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x81, 0x16, 0x81, 0x14, 0x61, 0x03, 0x90, 0x57, 0x60, 0x00, 0x80, 0xfd,
    0x5b, 0x80, 0x91, 0x50, 0x50, 0x92, 0x50, 0x92, 0x50, 0x92, 0x56, 0x5b, 0x7f, 0x4e, 0x48, 0x7b,
    0x71, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x52,
    0x60, 0x41, 0x60, 0x04, 0x52, 0x60, 0x24, 0x60, 0x00, 0xfd, 0x5b, 0x60, 0x00, 0x80, 0x60, 0x00,
    0x60, 0x60, 0x84, 0x86, 0x03, 0x12, 0x15, 0x61, 0x03, 0xdf, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b,
    0x83, 0x35, 0x92, 0x50, 0x60, 0x20, 0x84, 0x01, 0x35, 0x91, 0x50, 0x60, 0x40, 0x84, 0x01, 0x35,
    0x67, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x82, 0x11, 0x15, 0x61, 0x04, 0x05,
    0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x81, 0x86, 0x01, 0x91, 0x50, 0x86, 0x60, 0x1f, 0x83, 0x01,
    0x12, 0x61, 0x04, 0x19, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x81, 0x35, 0x81, 0x81, 0x11, 0x15,
    0x61, 0x04, 0x2b, 0x57, 0x61, 0x04, 0x2b, 0x61, 0x03, 0x9b, 0x56, 0x5b, 0x60, 0x40, 0x51, 0x60,
    0x1f, 0x82, 0x01, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xe0, 0x90, 0x81, 0x16, 0x60, 0x3f, 0x01, 0x16, 0x81, 0x01, 0x90, 0x83, 0x82,
    0x11, 0x81, 0x83, 0x10, 0x17, 0x15, 0x61, 0x04, 0x71, 0x57, 0x61, 0x04, 0x71, 0x61, 0x03, 0x9b,
    0x56, 0x5b, 0x81, 0x60, 0x40, 0x52, 0x82, 0x81, 0x52, 0x89, 0x60, 0x20, 0x84, 0x87, 0x01, 0x01,
    0x11, 0x15, 0x61, 0x04, 0x8a, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x82, 0x60, 0x20, 0x86, 0x01,
    0x60, 0x20, 0x83, 0x01, 0x37, 0x60, 0x00, 0x60, 0x20, 0x84, 0x83, 0x01, 0x01, 0x52, 0x80, 0x95,
    0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x92, 0x50, 0x92, 0x50, 0x92, 0x56, 0xfe, 0x60, 0x80, 0x60,
    0x40, 0x52, 0x34, 0x80, 0x15, 0x61, 0x00, 0x10, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x50, 0x61,
    0x01, 0x2e, 0x80, 0x61, 0x00, 0x20, 0x60, 0x00, 0x39, 0x60, 0x00, 0xf3, 0xfe, 0x60, 0x80, 0x60,
    0x40, 0x52, 0x34, 0x80, 0x15, 0x60, 0x0f, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x50, 0x60, 0x04,
    0x36, 0x10, 0x60, 0x28, 0x57, 0x60, 0x00, 0x35, 0x60, 0xe0, 0x1c, 0x80, 0x63, 0x24, 0x9c, 0xb3,
    0xfa, 0x14, 0x60, 0x2d, 0x57, 0x5b, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x60, 0x3c, 0x60, 0x38, 0x36,
    0x60, 0x04, 0x60, 0xb1, 0x56, 0x5b, 0x60, 0x4e, 0x56, 0x5b, 0x60, 0x40, 0x51, 0x90, 0x81, 0x52,
    0x60, 0x20, 0x01, 0x60, 0x40, 0x51, 0x80, 0x91, 0x03, 0x90, 0xf3, 0x5b, 0x60, 0x00, 0x82, 0x81,
    0x52, 0x60, 0x20, 0x81, 0x81, 0x52, 0x60, 0x40, 0x80, 0x83, 0x20, 0x73, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x85, 0x16, 0x84, 0x52, 0x90, 0x91, 0x52, 0x81, 0x20, 0x54, 0x60, 0xff, 0x16, 0x60, 0x88, 0x57,
    0x60, 0x00, 0x60, 0xaa, 0x56, 0x5b, 0x7f, 0xa2, 0xef, 0x46, 0x00, 0xd7, 0x42, 0x02, 0x2d, 0x53,
    0x2d, 0x47, 0x47, 0xcb, 0x35, 0x47, 0x47, 0x46, 0x67, 0xd6, 0xf1, 0x38, 0x04, 0x90, 0x25, 0x13,
    0xb2, 0xec, 0x01, 0xc8, 0x48, 0xf4, 0xb4, 0x5b, 0x93, 0x92, 0x50, 0x50, 0x50, 0x56, 0x5b, 0x60,
    0x00, 0x80, 0x60, 0x40, 0x83, 0x85, 0x03, 0x12, 0x15, 0x60, 0xc3, 0x57, 0x60, 0x00, 0x80, 0xfd,
    0x5b, 0x82, 0x35, 0x91, 0x50, 0x60, 0x20, 0x83, 0x01, 0x35, 0x73, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x81,
    0x16, 0x81, 0x14, 0x60, 0xed, 0x57, 0x60, 0x00, 0x80, 0xfd, 0x5b, 0x80, 0x91, 0x50, 0x50, 0x92,
    0x50, 0x92, 0x90, 0x50, 0x56, 0xfe, 0xa2, 0x64, 0x69, 0x70, 0x66, 0x73, 0x58, 0x22, 0x12, 0x20,
    0x5f, 0xfd, 0x4e, 0x6c, 0xed, 0xe7, 0xd0, 0x6a, 0x5d, 0xaf, 0x93, 0xd4, 0x8d, 0x05, 0x41, 0xfc,
    0x68, 0x18, 0x9e, 0xeb, 0x16, 0x60, 0x8c, 0x19, 0x99, 0xa8, 0x20, 0x63, 0xb6, 0x66, 0xeb, 0x11,
    0x64, 0x73, 0x6f, 0x6c, 0x63, 0x43, 0x00, 0x08, 0x13, 0x00, 0x33, 0xa2, 0x64, 0x69, 0x70, 0x66,
    0x73, 0x58, 0x22, 0x12, 0x20, 0xfd, 0xc4, 0xa0, 0xfe, 0x96, 0xe3, 0xb2, 0x1c, 0x10, 0x8c, 0xa1,
    0x55, 0x43, 0x8d, 0x37, 0xc9, 0x14, 0x3f, 0xb0, 0x12, 0x78, 0xa3, 0xc1, 0xd2, 0x74, 0x94, 0x8b,
    0xad, 0x89, 0xc5, 0x64, 0xba, 0x64, 0x73, 0x6f, 0x6c, 0x63, 0x43, 0x00, 0x08, 0x13, 0x00, 0x33,
};
// clang-format on

/// op-geth's exact Canyon gate (`*c.CanyonTime == timestamp`): true iff blockTsSec IS the
/// Canyon activation timestamp — never `fork >= Canyon`. OpForkSchedule exposes no per-fork
/// activation accessor, so equality is derived from the public boundary: the schedule reports
/// Canyon at blockTsSec but was before Canyon at blockTsSec-1 (activations are strictly
/// increasing and contiguous). The timestamp-0 activation (Canyon is the baseline) has no
/// predecessor and is handled via baselineTimestamp().
inline bool isCanyonCreate2DeployerActivation(
    bcos::evm::opstack::OpForkSchedule const& schedule, uint64_t blockTsSec)
{
    if (schedule.forkAt(blockTsSec) != bcos::evm::opstack::OpFork::Canyon)
        return false;
    if (blockTsSec == 0)
        return schedule.baselineTimestamp() == 0;
    return schedule.forkAt(blockTsSec - 1) < bcos::evm::opstack::OpFork::Canyon;
}

/// Mirror op-geth core/state_processor.go:81 (EnsureCreate2Deployer), which runs before the
/// pre-execution system calls: at the Canyon activation block, write the create2Deployer code
/// unconditionally at the canonical address. The write goes through the block StateView as a
/// one-entry StateDiff so it shares the ledger write path (and the read-cache coherence) with
/// every other state write; any pre-existing nonce/balance is preserved, matching SetCode's
/// code-only semantics.
template <class Storage>
void ensureCreate2Deployer(bcos::evm::evmstate::Storage2State<Storage>& stateView,
    bcos::evm::opstack::OpForkSchedule const& schedule, uint64_t blockTsSec)
{
    if (!isCanyonCreate2DeployerActivation(schedule, blockTsSec))
        return;

    auto const addr = create2DeployerAddress();
    auto existing = stateView.get_account(addr);
    if (stateView.poisoned())
        throw OpStorageError("create2Deployer account read poisoned: " + stateView.firstError());

    evmone::state::StateDiff diff;
    auto& entry = diff.modified_accounts.emplace_back();
    entry.addr = addr;
    entry.nonce = existing.has_value() ? existing->nonce : 0;
    entry.balance = existing.has_value() ? existing->balance : intx::uint256{0};
    entry.code = evmc::bytes(std::begin(c_create2DeployerCode), std::end(c_create2DeployerCode));

    try
    {
        stateView.applyDiff(bcos::evm::sanitizeStateDiff(stateView, std::move(diff)));
    }
    catch (const std::exception& e)
    {
        throw OpStorageError(std::string("create2Deployer write-back failed: ") + e.what());
    }
    catch (...)
    {
        throw OpStorageError("create2Deployer write-back failed: unknown exception");
    }
    if (stateView.poisoned())
        throw OpStorageError("create2Deployer write-back poisoned: " + stateView.firstError());
}

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
    std::optional<uint16_t>& daFootprintGasScalar,
    bcos::evm::opstack::OpForkSchedule const* schedule, uint64_t parentTsSec)
{
    namespace op = bcos::evm::opstack;

    if (schedule == nullptr)
    {
        throw std::invalid_argument("preBlockOpSteps: OpForkSchedule is required");
    }

    // The Cancun/Ecotone beacon root and blob pair exist only from Ecotone on; a pre-Ecotone
    // header must not be required to carry them.
    auto blk = detail::toBlockInfo(header, std::nullopt, /*lenientOptionals=*/false,
        /*requireEcotoneHeaderFields=*/cfg.fork >= op::OpFork::Ecotone);
    hashes.emplace(
        view, blk.number, detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);
    bcos::evm::evmstate::Storage2State<Storage> stateView(view, executor.sharedError());

    auto const blockTsSec =
        bcos::engine::unixSecondsFromInternalMillis(static_cast<uint64_t>(header.timestamp()));

    // (0) create2Deployer at the Canyon activation timestamp — op-geth runs
    // misc.EnsureCreate2Deployer (core/state_processor.go:81) BEFORE the pre-execution system
    // calls, so mirror that ordering here. Defensive consistency with upstream (see the
    // constant block above): unreachable for the all-forks-at-genesis corpus shape, live for a
    // schedule whose CanyonTime is later than genesis.
    ensureCreate2Deployer(stateView, *schedule, blockTsSec);

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
    if (rawTxBytes.empty())
        throw OpConsensusError("op block: missing L1 attributes deposit (empty block)");
    // Empty-envelope guard: the first envelope must be a deposit before deposits[0] is read.
    // A block with NO deposit at all stays a hard reject: the L1-attributes deposit seeds
    // the block's fee/DA context.
    if (!op::envelopeIsDeposit(rawTxBytes[0]) || deposits.empty())
        throw OpConsensusError("op block: no deposit transaction to seed the block");
    // First deposit is not L1 attributes: warn only. op-geth/op-reth accept this at validation.
    if (!op::isL1AttributesTx(deposits[0]))
        BCOS_LOG(WARNING) << LOG_BADGE("OP_BLOCK_EXEC")
                          << "op block: first tx is a deposit but not the L1 attributes tx — "
                             "accepted";
    // Q5: Jovian+ activation blocks are deposits-only (timestamp schedule).
    if (op::isNoUserTxActivationBlock(*schedule, parentTsSec, blockTsSec) &&
        op::hasNonDepositEnvelope(rawTxBytes))
    {
        throw OpConsensusError(
            "op block: unexpected non-deposit transactions in fork activation block");
    }
    if (cfg.has_da_footprint)
    {
        auto const& data = deposits[0].data;
        // Shape (176B → scalar 0; ≥178B → selector + length) AND the length-keyed
        // deposits-only rule on the 176B form (op-geth CalcDAFootprint).
        op::validateJovianL1AttributesShape(std::span<uint8_t const>{data.data(), data.size()}, cfg,
            op::lastEnvelopeIsDeposit(rawTxBytes));
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
/// Shares one construction with the engine's other two transactionsRoot producers
/// (EngineServiceCommon.cpp transactionsRootFromPayload, EngineStorageCommit.h
/// buildHeaderCommitments) — the three MUST agree or newPayload rejects this node's own
/// payloads. Values stay views: computeIndexedTrieRoot reads the caller's bytes, so the
/// owned-bytes marshalling this needed while it went through computeTrieRootVarKey is gone.
template <class RawTxRange>
[[nodiscard]] bcos::h256 computeOpTxRoot(RawTxRange const& rawTxBytes)
{
    std::vector<bcos::bytesConstRef> rawEnvelopes;
    rawEnvelopes.reserve(rawTxBytes.size());
    for (auto const& rawItem : rawTxBytes)
    {
        // .data()/.size() rather than bcos::ref: the range's element is `bytes` at one
        // call site and already a RefDataContainer at another, and bcos::ref would double-wrap
        // the latter.
        rawEnvelopes.emplace_back(rawItem.data(), rawItem.size());
    }
    return bcos::ledger::mpt::calculateTransactionsRoot(rawEnvelopes);
}
}  // namespace bcos::evm::engine
