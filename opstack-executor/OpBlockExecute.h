#pragma once

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/Common.h>
#include <array>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
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

/// Block execution result. receipts keep their original in-block order (the receipts-root /
/// block-level bloom depend on this order; cumulative_gas_used is already filled in interleaved
/// order). Each receipt is a bcos::protocol::TransactionReceipt directly produced by the execution
/// layer (plan A phase 2) — the OP metadata (l1/operator/DA, or deposit_nonce/version) rides in
/// its opStackMeta, so no evmone receipt wrapper survives here. txTypes[i] carries the EIP-2718
/// type byte that produced receipts[i] (kDepositTxType for deposits, else the Transaction::Type
/// value): the FISCO receipt interface has no tx-type slot, and sealOpBlock's EncodeIndex
/// receipts-root leaf needs the typed prefix (op-geth Receipts.EncodeIndex semantics).
struct OpBlockResult
{
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
    std::vector<uint8_t> txTypes;           // one EIP-2718 type byte per receipt, same order
    int64_t gasUsed = 0;                    // = last tx's cumulative
    evmone::state::StateDiff finalizeDiff;  // end-of-block finalize output (already delivered via
                                            // applyDiff)
};

/// Execute a whole block (execution ordering): system_call_block_start → first L1 attributes
/// deposit → loadOpFeeParams → per-transaction (gas pool / cumulative / per-transaction write-back)
/// → finalizeOpBlock. Write-back callback: invoked immediately after each diff segment is produced;
/// the view read by the next step must already reflect it.
/// **discard-writes contract**: after any throw from this function, the caller must discard the
/// entire write set already applied via applyDiff within this block (same semantics as op-geth
/// Process discarding the whole statedb on error, state_processor.go:109-113). Throws
/// std::runtime_error (block-level error): txs empty or first tx is not the L1 attributes deposit
/// (to==OP_L1_BLOCK && from==OP_DEPOSITOR, stricter-than-spec); a deposit appears after a
/// non-deposit (stricter-than-spec); any tx gasLimit exceeds the remaining block gas; is_system_tx;
/// any validate error on a normal tx (op-geth has no failed-receipt mechanism for normal txs,
/// state_processor.go:109-113).
OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff);

// ---- Jovian L1-attributes block shape ----
// op-geth pins these in `core/types/rollup_cost.go`: the first (L1 attributes) deposit's calldata
// is `IsthmusL1AttributesLen` (176) bytes on the Jovian *activation* block (the DA-footprint gas
// scalar is not set yet) and `JovianL1AttributesLen` (178) bytes with `JovianL1AttributesSelector`
// (0x3db6be2b) thereafter (`rollup_cost.go:46-47/:65`).
inline constexpr std::size_t IsthmusL1AttributesLen = 176;
inline constexpr std::size_t JovianL1AttributesLen = 178;
inline constexpr std::array<uint8_t, 4> JovianL1AttributesSelector = {0x3d, 0xb6, 0xbe, 0x2b};

/// Validate the Jovian L1-attributes block shape (C-3 selector/length + C-4 activation
/// deposits-only). No-op for pre-Jovian configs (`cfg.has_da_footprint == false`) and for the
/// degenerate cases `processOpBlock` already rejects (empty block / non-deposit first tx), so it
/// is safe to call unconditionally at the top of `processOpBlock`. Mirrors the validation half of
/// op-geth `core/types/rollup_cost.go`'s `CalcDAFootprint` (`:563-591`); the footprint *sum* stays
/// on the seal side (OpBlockSeal.cpp). Throws `std::runtime_error` (block-level error) on a shape
/// violation, the same channel as `processOpBlock`'s sibling structural checks.
void validateJovianBlockShape(std::span<const OpBlockTx> txs, const OpForkConfig& cfg);

// ---- shared per-receipt helpers (R3 export) ----
// `narrowGasUsed` / `hexCumulative` / `isL1AttributesTx` were promoted out of OpBlockExecute.cpp's
// anonymous namespace (OpBlockExecute.cpp:16-49) so the per-transaction injector loop
// (OpBlockInjector.h runOpBlockInjection) and processOpBlock share ONE implementation — the
// "no copy drift" guard of the route-B harness (spec §7.0, review R3). Pure refactor: these three
// are only used inside OpBlockExecute.cpp (verified by grep), and the anonymous-namespace copies
// are deleted alongside, so no TU sees two definitions.

/// Stricter-than-spec: validate the L1 attributes deposit by content. op-geth's EL does not
/// perform this validation (pushed down to the CL layer); op-node always prepends a deposit
/// that satisfies both conditions, so a divergent verdict is only reachable via a hand-crafted
/// payload fed directly to engine_newPayload. Keep the check: it rejects malformed blocks
/// op-geth accepts, at zero cost on legitimate payloads.
[[nodiscard]] inline bool isL1AttributesTx(const DepositTx& dep) noexcept
{
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}

/// Narrow the FISCO receipt's gasUsed (u256) back to the int64 the gas pool/cumulative accounting
/// uses. The execution layer only ever stores a small positive gas_used, but the "widen -> check ->
/// narrow" discipline (Storage2State.h precedent) applies: a corrupt receipt must not silently
/// wrap blockGasLeft/cumulative.
[[nodiscard]] inline int64_t narrowGasUsed(const bcos::u256& gasUsed)
{
    static const bcos::u256 kMaxInt64(std::numeric_limits<int64_t>::max());
    if (gasUsed > kMaxInt64)
        throw std::runtime_error("op block: receipt gasUsed exceeds int64_t range");
    return static_cast<int64_t>(gasUsed);
}

/// "0x" + lowercase hex, no leading zeros (op-geth hexutil.Uint64 convention). Stored on the
/// receipt's cumulativeGasUsed string field; encodeReceiptForRoot parses it back to the exact
/// uint64 for the EncodeIndex leaf (see OpBlockSeal.cpp).
[[nodiscard]] inline std::string hexCumulative(uint64_t cumulative)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << cumulative;
    return oss.str();
}

/// OP block finalize: withdrawals are always empty, no ommers / block reward; EIP-6110/7002/7251
/// requests are suppressed per cfg.disable_prague_requests — op-geth explicitly disables them for
/// OP Isthmus (state_processor.go:140-156), and this switch is always true for every OP fork; false
/// throws std::runtime_error (block-level rejection, not a local fault). Scope note: on OP Isthmus
/// op-geth still runs the EIP-4788/2935 **pre-execution** system call (state_processor.go:90-95) —
/// that is a precondition step wired in during block-level orchestration, not part of this finalize
/// function.
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase);

// ---- block-header commitment fields + seal (merged from OpBlockSeal.h) ----
using evmc::literals::operator""_bytes32;

/// OP Isthmus+ block-header requestsHash is a fixed value = sha256("") (op-geth EmptyRequestsHash,
/// hashes.go:43-44; on the build side worker.go:283-290 calls CalcRequestsHash on an empty list, on
/// the validation side block_validator.go:177-184 always matches Process's nil requests).
inline constexpr auto OP_EMPTY_REQUESTS_HASH =
    0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855_bytes32;

/// Block-header commitment fields (M-B2 scope). Jovian BlobGasUsed (the DA footprint header field)
/// was reclaimed into this struct in M-B3+M6 Task 1 (the give-back of the original M-B2 decision
/// record 2).
struct OpBlockSeal
{
    evmone::hash256 receiptsRoot;
    evmone::state::BloomFilter logsBloom;
    evmone::hash256 withdrawalsRoot;
    std::optional<evmone::hash256> requestsHash;  // Isthmus+ has a value; CANCUN-family fork
                                                  // headers lack this field
    /// Jovian block-header BlobGasUsed reuse slot = DA footprint (CalcDAFootprint,
    /// rollup_cost.go:563-591: only non-deposit txs accumulate, each tx = EstimatedDASize ×
    /// scalar). This implementation = Σ of the meta.da_footprint over non-deposit receipts; a
    /// deposits-only block sums no terms and is always 0 ≡ op-geth's first-Jovian-block special
    /// case (the equivalence reason being that there are no terms). op-geth's validation-side
    /// "reject footprint > gasLimit" (block_validator.go:131) is a validation responsibility; this
    /// function only produces the value. When has_da_footprint is false there is always no value.
    std::optional<uint64_t> blobGasUsed;
};

/// Single-account storage root (secure-trie: key = keccak256(slot), value = rlp(trim(value)),
/// zero-value slots skipped — aligned with the private helper in evmone mpt_hash.cpp:13-24; the
/// upstream does not export it, so it is reproduced here as an exported piece and registered in the
/// upstream-diff manifest to watch for drift).
[[nodiscard]] evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// Compute the block-header commitment fields from the block execution result.
/// messagePasserStorage: the storage snapshot of OP_L2_TO_L1_MESSAGE_PASSER (0x4200…0016,
/// protocol_params.go:31) **after end-of-block finalize** (on the op-geth build side
/// consensus.go:416-427 takes it after IntermediateRoot — the timing point is a documented
/// contract; decision point 3 ruling (a): the caller provides it, keeping the StateView narrow
/// interface). pre-Isthmus forks ignore it (withdrawalsRoot = empty-list root EMPTY_MPT_HASH,
/// Canyon+ withdrawals list is always empty). Precondition: result.receipts is non-empty
/// (guaranteed by processOpBlock's first-attributes invariant; an empty sequence produces
/// EMPTY_MPT_HASH rather than an error).
[[nodiscard]] OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage);

/// receipts-root leaf encoding rebuilt from a bcos::protocol::TransactionReceipt (plan A phase 2 —
/// replaces the former OpDepositReceipt/OpTxReceipt-based encoders). `txType` is the EIP-2718
/// type byte that produced the receipt (kDepositTxType for deposits, else the Transaction::Type
/// value) — the FISCO receipt interface has no tx-type slot, so the caller threads it through
/// OpBlockResult::txTypes. Byte-for-byte op-geth `Receipts.EncodeIndex` semantics
/// (receipt.go:568-592 — note this is NOT MarshalBinary :279-288; the two deliberately differ for
/// a receipt that "has nonce, has no version", and the function-header comment :564-567
/// explicitly forbids changing that):
///   deposit: 0x7E || rlp([status, cumulativeGasUsed, logsBloom, logs, depositNonce,
///   depositReceiptVersion]) — nonce/version read from opStackMeta (depositReceiptRLP :136-148).
///   normal tx: typed raw-byte prefix (empty for legacy) + rlp([status, cumGas, bloom, logs]),
///   byte-identical to EncodeIndex for type 0/1/2/4.
/// status is projected as bool (FISCO 0 == success); cumulativeGasUsed() is parsed from its hex
/// string; logsBloom() is the 256-byte bloom; logEntries() re-encode the evmone Log shape.
[[nodiscard]] evmc::bytes encodeReceiptForRoot(
    const bcos::protocol::TransactionReceipt& r, uint8_t txType);
}  // namespace bcos::evm::opstack
