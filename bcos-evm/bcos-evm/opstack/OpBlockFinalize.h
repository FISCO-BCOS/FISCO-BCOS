#pragma once

#include <bcos-evm/eth/state/state.hpp>
#include <bcos-evm/opstack/OpForkSchedule.h>

namespace bcos::evm::opstack
{
/// OP block finalize: withdrawals are always empty, no ommers / block reward; EIP-6110/7002/7251
/// requests are suppressed per cfg.disable_prague_requests — op-geth explicitly disables them for
/// OP Isthmus (state_processor.go:140-156), and this switch is always true for every OP fork; false
/// throws std::invalid_argument (configuration guardrail). Scope note: on OP Isthmus op-geth still
/// runs the EIP-4788/2935 **pre-execution** system call (state_processor.go:90-95) — that is a
/// precondition step wired in during block-level orchestration (§4.4), not part of this finalize
/// function.
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase);
}  // namespace bcos::evm::opstack
