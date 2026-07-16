#pragma once

#include <evmc/evmc.hpp>
#include <test/state/state.hpp>

namespace bcos::evmref::opstack
{
class OpHost;

struct ExecOutcome
{
    evmc::Result result;
    int64_t gas_used;  // EIP-3529 refund and EIP-7623 floor already settled
};

/// Verbatim copy of the middle section of the baseline transition() (evmone
/// test/state/state.cpp:600-637): warm-up (sender/to/access_list/coinbase@Shanghai+) ->
/// build_message + EIP-7702 delegation resolution -> host.call -> refund =
/// min(delegation+result.gas_refund, used/quotient) -> 7623 floor. Preconditions: sender has been
/// get_or_insert'd and the nonce already incremented (CREATE address derivation uses nonce-1,
/// evmone host.cpp:239).
ExecOutcome executeMessage(evmone::state::State& state, OpHost& host,
    const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase,
    int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund);
}  // namespace bcos::evmref::opstack
