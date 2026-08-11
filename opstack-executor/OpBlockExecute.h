#pragma once

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <array>
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
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

/// Block execution result. receipts keep their original in-block order (M-B2's receipts-root /
/// block-level bloom depend on this order; cumulative_gas_used is already filled in interleaved
/// order). Each receipt is a bcos::protocol::TransactionReceipt directly produced by the execution
/// layer (方案 A 阶段 2) — the OP metadata (l1/operator/DA, or deposit_nonce/version) rides in its
/// opStackMeta, so no evmone receipt wrapper survives here. txTypes[i] carries the EIP-2718 type
/// byte that produced receipts[i] (kDepositTxType for deposits, else the Transaction::Type
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

/// Execute a whole block (spec §4.1 ordering): system_call_block_start → first L1 attributes
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

// ---- Jovian L1-attributes block shape (batch C, spec §6.4) ----
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
}  // namespace bcos::evm::opstack
