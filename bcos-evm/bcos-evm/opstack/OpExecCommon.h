#pragma once

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
/// evmone host.cpp:239).
ExecOutcome executeMessage(evmone::state::State& state, OpHost& host,
    const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase,
    int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund);
}  // namespace bcos::evm::opstack
