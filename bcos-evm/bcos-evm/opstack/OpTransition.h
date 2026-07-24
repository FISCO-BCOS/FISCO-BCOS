#pragma once

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpReceiptMeta.h>
#include <bcos-evm/opstack/OpValidate.h>
#include <bcos-evm/eth/state/state.hpp>
#include <evmc/evmc.hpp>

namespace bcos::evmref::opstack
{
/// Fork evmone::state::transition (evmone state.cpp:561-649): buyGas adds l1Cost +
/// operatorCost(gasLimit); Host replaced with OpHost; tail routes base/l1/operator fees to vaults.
/// Does not write back; caller applies applyStateDiff(receipt.state_diff).
/// Uses props.fee (the OpFeeParams snapshot taken when opValidate computed props) rather than
/// receiving a separate fee parameter — this prevents the validate/transition calls from being
/// fed different OpFeeParams across the two invocations, which would underflow the refund
/// computation (see OpTxProperties::fee).
OpTxReceipt opTransition(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId, evmc::bytes_view signedTxEnvelope);

/// Pairing constraint: the *FromState functions must be used as a pair; they must not be
/// interleaved with the injection-style ones (opValidate/opTransition). props.fee was already
/// read and cached by opValidateFromState, so it is passed through directly to opTransition here
/// without re-reading the view.
OpTxReceipt opTransitionFromState(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId, evmc::bytes_view signedTxEnvelope);
}  // namespace bcos::evmref::opstack
