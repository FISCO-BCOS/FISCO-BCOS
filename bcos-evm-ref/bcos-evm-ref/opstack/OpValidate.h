#pragma once

#include <bcos-evm-ref/opstack/OpFeeParams.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <system_error>
#include <test/state/state.hpp>
#include <variant>

namespace bcos::evmref::opstack
{
struct OpTxProperties
{
    evmone::state::TransactionProperties props;
    intx::uint256 l1_cost;
    intx::uint256 operator_cost_at_gas_limit;
    // The OpFeeParams snapshot used when opValidate computed l1_cost/operator_cost_at_gas_limit;
    // opTransition reuses it directly rather than accepting a separate fee argument, avoiding the
    // validate/transition pair being fed different OpFeeParams across the two calls, which would
    // cause operator_cost_at_gas_limit - opAtUsed to underflow (both must come from the same
    // fee-rate read).
    OpFeeParams fee;
    uint32_t flz_len = 0;  // Fjord+ single FastLZ result; 0 for Ecotone
};

/// Reuses evmone validate_transaction then applies OP checks: reject blob tx; balance cap
/// = gasLimit*maxGasPrice + value + l1Cost + operatorCost(gasLimit) (gasFeeCap pricing).
[[nodiscard]] std::variant<OpTxProperties, std::error_code> opValidate(
    const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::Transaction& tx, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg, const OpFeeParams& fee, int64_t blockGasLeft);

/// Pairing constraint: the *FromState functions must be used as a pair; they must not be mixed with
/// the injection-style ones (opValidate/opTransition).
/// Reads the OP_L1_BLOCK fee parameters from the view, then delegates to opValidate.
[[nodiscard]] std::variant<OpTxProperties, std::error_code> opValidateFromState(
    const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::Transaction& tx, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg, int64_t blockGasLeft);
}  // namespace bcos::evmref::opstack
