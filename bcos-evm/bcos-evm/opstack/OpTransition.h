#pragma once

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpReceiptMeta.h>
#include <bcos-evm/opstack/OpValidate.h>
#include <bcos-evm/eth/state/state.hpp>
#include <evmc/evmc.hpp>

namespace bcos::evm::opstack
{
class OpHost;

struct ExecOutcome
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
ExecOutcome runTxMessage(evmone::state::State& state, OpHost& host,
    const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase,
    int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund);

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
}  // namespace bcos::evm::opstack
