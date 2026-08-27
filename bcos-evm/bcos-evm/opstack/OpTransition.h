#pragma once

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-evm/eth/state/state.hpp>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <limits>
#include <optional>
#include <system_error>
#include <variant>

namespace bcos::evm::opstack
{
class OpHost;

// OP Stack block-transition types and helpers.
// Split out of OpSchedulerSeam.h so dependent layers can reference types without
// depending on the class template.

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
    // NOTE: three adjacent bools (has_operator_fee / jovian_operator_formula /
    // has_da_footprint) — callers can swap two and still compile cleanly. Keep the order
    // stable; prefer a bitmask or struct if the surface grows.
    bool has_operator_fee = false;
    bool jovian_operator_formula = false;
    // Likewise for the receipt's DA-footprint fields: they must describe the fork the transaction
    // was PRICED under, not whichever cfg reaches opTransition. Validate under Ecotone (flz_len
    // forced to 0) and transition under Jovian would otherwise report a da_footprint_gas_scalar
    // for a transaction the Ecotone L1 formula priced, with da_footprint computed from flz_len 0.
    bool has_da_footprint = false;
    // calldataGasUsed under the Ecotone formula (= zeroes*4 + ones*16); not filled under Fjord+
    // (flz_len drives the Fjord formula). Snapshot at validate time (the envelope is available
    // here); read by deriveOpReceiptMeta at transition -- preserving the no-cfg invariant.
    std::optional<uint64_t> ecotone_calldata_gas_used = std::nullopt;
    // The fully-built evmone state::Transaction, carried from m_prepare (validate) to m_execute
    // (transition) so the hot path builds it once per tx instead of twice (calldata copy +
    // to-address hex decode + access_list/blob/auth allocation each time). Filled by
    // OpstackExecutor::m_prepare after opValidate returns; read (const&) by m_execute.
    evmone::state::Transaction evm_tx{};  // default member init: keeps the positional aggregate
                                          // init in opValidate (OpTransition.cpp) warning-free
};

/// Reuses evmone validate_transaction then applies OP checks: reject blob tx; balance cap
/// = gasLimit*maxGasPrice + value + l1Cost + operatorCost(gasLimit) (gasFeeCap pricing).
/// The 512-bit cap always runs. eth_call/estimateGas must wrap the view with
/// CallSimulationView so the comparison sees a funded sender — do not skip it.
[[nodiscard]] std::variant<OpTxProperties, std::error_code> opValidate(
    const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::Transaction& tx, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg, const OpFeeParams& fee, int64_t blockGasLeft);

/// eth_call/estimateGas view mask: the simulated sender reports uint256::max() balance so
/// validate_transaction's INSUFFICIENT_FUNDS check and opValidate's 512-bit cap both pass
/// without skipping the comparison. Unlike DepositValidationView this does not blank code
/// (EIP-3607 still applies; a contract-sender call must execute real bytecode).
/// Pass the same wrapper to opValidate and opTransition so the unchecked uint256
/// subtractions in opTransition cannot wrap. Writes are discarded with the call overlay.
/// The mask is deliberately visible to the EVM: the same State is what the VM executes
/// against, so during a simulation BALANCE(sender) and SELFBALANCE report 2^256-1, a CALL
/// transferring more than the sender's real balance succeeds, and EXTCODEHASH(sender) for a
/// never-used address returns the empty-code hash (the account is materialised). An additive
/// credit is not an alternative: tx_max_cost = gasLimit * max_gas_price is unbounded above, so
/// saturating at max() is the only form that cannot reintroduce the underflow. eth_call's
/// post-state is never read (status/gasUsed/output are on the receipt), so the fabricated
/// balance must also never be written back — OpstackExecutor::m_finish skips applyDiff for
/// call=true (OpstackExecutor.h).
class CallSimulationView final : public evmone::state::StateView
{
public:
    CallSimulationView(const StateView& base, const evmc::address& sender) noexcept
      : m_base{base}, m_sender{sender}
    {}

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override
    {
        auto acc = m_base.get_account(addr);
        if (addr != m_sender)
            return acc;
        if (!acc.has_value())
        {
            acc.emplace();
            acc->code_hash = evmone::state::Account::EMPTY_CODE_HASH;
        }
        acc->balance = std::numeric_limits<intx::uint256>::max();
        return acc;
    }

    evmone::state::bytes get_account_code(const evmc::address& addr) const noexcept override
    {
        return m_base.get_account_code(addr);
    }

    evmone::state::bytes32 get_storage(
        const evmc::address& addr, const evmone::state::bytes32& key) const noexcept override
    {
        return m_base.get_storage(addr, key);
    }

private:
    const StateView& m_base;
    evmc::address m_sender;
};

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

// ---- non-consensus receipt metadata (formerly OpReceiptMeta.h) ----

/// The execution-layer OP receipt metadata, before it is projected into the framework's
/// bcos::protocol::OpStackReceiptMeta (the typed view over the tars opStackMeta hex-string
/// fields) via setOpStackMeta. Deliberately mirrors the op-geth receipt extension fields.
struct OpReceiptMeta
{
    // L1 passthrough (op-geth: L1GasPrice / L1BlobBaseFee / L1BaseFeeScalar / L1BlobBaseFeeScalar /
    // L1Fee)
    std::optional<intx::uint256> l1_gas_price;      // = fee.l1_base_fee
    std::optional<intx::uint256> l1_blob_base_fee;  // = fee.blob_base_fee
    std::optional<uint32_t> l1_base_fee_scalar;
    std::optional<uint32_t> l1_blob_base_fee_scalar;
    std::optional<intx::uint256> l1_fee;  // = l1_cost
    std::optional<uint64_t> l1_gas_used;  // Fjord+; wire index 11
    // operator (Isthmus+)
    std::optional<intx::uint256> operator_fee;    // FISCO extension: actually-charged value
                                                  // (op-geth receipt has no such field)
    std::optional<uint32_t> operator_fee_scalar;  // filled only when (scalar != 0 || constant != 0)
    std::optional<uint64_t> operator_fee_constant;
    // DA footprint (Jovian+; op-geth receipt BlobGasUsed semantics)
    std::optional<uint64_t> da_footprint_gas_scalar;
    std::optional<uint64_t> da_footprint;
    // Effective gas price (base_fee + priority_gas_price) is deliberately NOT here: it is carried
    // on the tars effectiveGasPrice base field instead (op-geth api.go:1775, RPC top-level output).
};

/// Build the non-consensus receipt metadata. Deliberately takes NO OpForkConfig: the receipt has
/// to describe what the transaction was actually priced and charged under, and those decisions
/// are frozen in the validate-time snapshot (OpTxProperties). Passing a cfg here would give this
/// function a second, independent source of truth that can disagree with the charge whenever
/// validate and transition straddle a fork boundary — which is exactly the bug this signature
/// now makes unrepresentable.
///
/// Pass the snapshot, not loose bools. Three adjacent bool parameters would let a caller swap
/// has_operator_fee and has_da_footprint with a clean compile; OpTxProperties freezes that
/// pairing. fill_operator_scalars is caller policy.
OpReceiptMeta deriveOpReceiptMeta(const OpTxProperties& props, intx::uint256 operator_fee_at_used,
    bool fill_operator_scalars) noexcept;

// ---- normal-tx transition ----

/// Fork evmone::state::transition (evmone state.cpp:561-649): buyGas adds l1Cost +
/// operatorCost(gasLimit); Host replaced with OpHost; tail routes base/l1/operator fees to vaults.
/// Produces a bcos::protocol::TransactionReceipt directly (option A phase 2): status/gasUsed/logs
/// are projected onto the FISCO receipt, the OP metadata (l1/operator/DA fields) is carried via
/// setOpStackMeta, and the effective gas price lands on the receipt's top-level effectiveGasPrice.
/// The state diff is returned through `outStateDiff` (the FISCO receipt interface has no field
/// for it); the caller applies it via applyDiff. Does not write back otherwise.
/// All fee inputs come from props (the snapshot opValidate produced) — l1_cost, flz_len, fee and
/// the operator-fee formula flags — so validate and transition price the tx identically. The
/// signed envelope is NOT taken here: L1/DA cost was already derived from it in opValidate.
bcos::protocol::TransactionReceipt::Ptr opTransition(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    evmone::state::StateDiff& outStateDiff);

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
/// Returns a bcos::protocol::TransactionReceipt::Ptr with the deposit_nonce/receipt_version
/// carried via setOpStackMeta; the state diff is returned through `outStateDiff`.
bcos::protocol::TransactionReceipt::Ptr runDeposit(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const DepositTx& dep, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    int64_t blockGasLeft, const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    evmone::state::StateDiff& outStateDiff);
}  // namespace bcos::evm::opstack
