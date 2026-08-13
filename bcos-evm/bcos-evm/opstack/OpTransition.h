#pragma once

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpReceipt.h>
#include <bcos-evm/eth/state/state.hpp>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <optional>
#include <system_error>
#include <variant>

namespace bcos::evm::opstack
{
class OpHost;

// ---- tx validation (formerly OpValidate.h) ----

struct OpTxProperties
{
    evmone::state::TransactionProperties props;
    intx::uint256 l1_cost;
    intx::uint256 operator_cost_at_gas_limit;
    // OpFeeParams snapshot used by opValidate when computing l1_cost/operator_cost_at_gas_limit;
    // opTransition reuses it directly rather than receiving a separate fee parameter, avoiding the
    // validate/transition calls being fed different OpFeeParams across the two invocations, which
    // would underflow operator_cost_at_gas_limit - opAtUsed (both must come from the same fee
    // read).
    OpFeeParams fee;
    uint32_t flz_len = 0;  // Fjord+ single FastLZ result; 0 for Ecotone
    // Operator-fee formula selection captured at validate time, so opTransition charges the vault
    // with the SAME formula that priced the sender's pre-charge (operator_cost_at_gas_limit).
    // Without this, a cfg mismatch across validate/transition (fork boundary) would credit the
    // vault by a different formula than the sender was charged → non-conservation / mint.
    bool has_operator_fee = false;
    bool jovian_operator_formula = false;
    // Likewise for the receipt's DA-footprint fields: they must describe the fork the transaction
    // was PRICED under, not whichever cfg reaches opTransition. Validate under Ecotone (flz_len
    // forced to 0) and transition under Jovian would otherwise report a da_footprint_gas_scalar
    // for a transaction the Ecotone L1 formula priced, with da_footprint computed from flz_len 0.
    bool has_da_footprint = false;
};

/// Reuses evmone validate_transaction then applies OP checks: reject blob tx; balance cap
/// = gasLimit*maxGasPrice + value + l1Cost + operatorCost(gasLimit) (gasFeeCap pricing).
[[nodiscard]] std::variant<OpTxProperties, std::error_code> opValidate(
    const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::Transaction& tx, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg, const OpFeeParams& fee, int64_t blockGasLeft);

/// Pairing constraint: the *FromState functions must be used as a pair; they must not be
/// interleaved with the injection-style ones (opValidate/opTransition).
/// Reads the OP_L1_BLOCK fee parameters from the view, then delegates to opValidate.
[[nodiscard]] std::variant<OpTxProperties, std::error_code> opValidateFromState(
    const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::Transaction& tx, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg, int64_t blockGasLeft);

// ---- shared execution core ----

struct RunTxResult
{
    evmc::Result result;
    int64_t gas_used;  // EIP-3529 refund and the EIP-7623 floor already settled
};

/// The verbatim copy of the middle section of the baseline transition() (evmone
/// test/state/state.cpp:600-637): warm access (sender/to/access_list/coinbase@Shanghai+) →
/// build_message + EIP-7702 delegation resolution → host.call → refund = min(delegation +
/// result.gas_refund, used/quotient) → the EIP-7623 floor. Precondition: sender has already been
/// get_or_insert'd and its nonce already incremented (CREATE address derivation uses nonce-1,
/// evmone host.cpp:239). Shared execution core: used by opTransition (normal txs) and
/// runDeposit (0x7E deposits, which skip buy-gas but share this middle).
RunTxResult runTxMessage(evmone::state::State& state, OpHost& host,
    const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase,
    int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund);

// ---- normal-tx transition ----

/// Fork evmone::state::transition (evmone state.cpp:561-649): buyGas adds l1Cost +
/// operatorCost(gasLimit); Host replaced with OpHost; tail routes base/l1/operator fees to vaults.
/// Does not write back; caller applies applyStateDiff(receipt.state_diff).
/// All fee inputs come from props (the snapshot opValidate produced) — l1_cost, flz_len, fee and
/// the operator-fee formula flags — so validate and transition price the tx identically. The
/// signed envelope is NOT taken here: L1/DA cost was already derived from it in opValidate.
OpTxReceipt opTransition(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId);

// ---- 0x7E deposit tx (formerly OpDepositTx.h) ----

/// 0x7E deposit tx (not an evmone Transaction). When mint has a value it is added unconditionally
/// to the from balance (nullopt = do not add); value is transferred normally within the call —
/// two independent fields. is_system_tx must be false after Regolith.
struct DepositTx
{
    evmc::bytes32 source_hash;
    evmc::address from;
    std::optional<evmc::address> to;    // nullopt = contract creation (address derived from
                                        // from + pre-execution nonce)
    std::optional<intx::uint256> mint;  // nullopt = no mint (matches op-geth *big.Int nil)
    intx::uint256 value;
    int64_t gas_limit;
    bool is_system_tx;
    evmc::bytes data;
};

/// OP 0x7E deposit transaction/receipt type (EIP-2718 typed envelope prefix).
constexpr auto kDepositTxType = static_cast<evmone::state::Transaction::Type>(0x7e);

/// Execute one 0x7E deposit: skip buyGas; add balance when mint has a value; still deduct
/// intrinsic + the EIP-7623 floor; both failure paths retain the mint and force-increment the
/// nonce; is_system_tx==true throws std::runtime_error (block-level error). gas_limit exceeding
/// blockGasLeft throws std::runtime_error (op-geth ErrGasLimitReached, block-level error).
/// Deposits are exempt from the EIP-7825 per-tx cap (Karst meters deposit gas on L1 — the
/// OptimismPortal caps deposits at 20M gas total per L1 block; the EL performs no deposit
/// gas check of its own).
OpDepositReceipt runDeposit(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const DepositTx& dep, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    int64_t blockGasLeft);
}  // namespace bcos::evm::opstack
