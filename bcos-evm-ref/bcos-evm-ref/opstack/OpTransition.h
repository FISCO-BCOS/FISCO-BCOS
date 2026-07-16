#pragma once

#include <bcos-evm-ref/opstack/OpFeeParams.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpReceiptMeta.h>
#include <bcos-evm-ref/opstack/OpValidate.h>
#include <evmc/evmc.hpp>
#include <test/state/state.hpp>

namespace bcos::evmref::opstack
{
/// Fork evmone::state::transition (evmone state.cpp:561-649): buyGas adds l1Cost +
/// operatorCost(gasLimit); Host replaced with OpHost; tail routes base/l1/operator fees to vaults.
/// Does not write back; caller applies applyStateDiff(receipt.state_diff).
/// Uses props.fee (the OpFeeParams snapshot taken when opValidate computed props) rather than
/// accepting a separate fee argument — this prevents the validate/transition pair from being fed
/// different OpFeeParams across the two calls, which would cause the refund computation to
/// underflow (see OpTxProperties::fee).
OpTxReceipt opTransition(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId, evmc::bytes_view signedTxEnvelope);

/// Pairing constraint: the *FromState functions must be used as a pair; they must not be mixed with
/// the injection-style ones (opValidate/opTransition).
/// props.fee has already been read and cached by opValidateFromState; here it is passed straight
/// through to opTransition without re-reading the view.
OpTxReceipt opTransitionFromState(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId, evmc::bytes_view signedTxEnvelope);
}  // namespace bcos::evmref::opstack
