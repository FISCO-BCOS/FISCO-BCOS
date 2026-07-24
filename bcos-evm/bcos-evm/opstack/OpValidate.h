#pragma once

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/eth/state/state.hpp>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <system_error>
#include <variant>

namespace bcos::evmref::opstack
{
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
}  // namespace bcos::evmref::opstack
