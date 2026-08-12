#pragma once

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/Common.h>
#include <array>
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
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
}  // namespace bcos::evm::opstack
