#pragma once

#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <test/state/state.hpp>

namespace bcos::evmref::opstack
{
/// OP block finalization: withdrawals are always empty, no ommers/block rewards; EIP-6110/7002/7251
/// requests are suppressed per cfg.disable_prague_requests — op-geth explicitly disables them for
/// OP Isthmus (state_processor.go:140-156), and this switch is always true for all OP forks; false
/// throws std::invalid_argument (configuration guardrail).
/// Scope note: op-geth still runs the EIP-4788/2935 **pre-execution** system calls under OP Isthmus
/// (state_processor.go:90-95) — those are a preceding step wired in when block-level orchestration
/// (§4.4) is integrated, and are not part of this finalization function.
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase);
}  // namespace bcos::evmref::opstack
