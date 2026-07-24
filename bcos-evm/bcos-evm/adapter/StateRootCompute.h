// bcos-evm-ref/bcos-evm-ref/adapter/StateRootCompute.h
#pragma once

#include <bcos-evm/eth/state/hash_utils.hpp>

namespace evmone::test
{
class TestState;
}

namespace bcos::evmref
{
/// Full-state MPT root (correctness version: rebuilds the entire root every time; scalable
/// incremental building is TA-1d, a non-goal here).
/// Engine = evmone mpt_hash(TestState) — the same-source implementation t8n uses to compute
/// stateRoot (secure-trie account tree + per-account storage trie); this function is zero
/// custom logic, just wiring.
/// Empty state → EMPTY_MPT_HASH.
///
/// Timing contract (the block-header production call sequence, anchored to the same point as
/// OpBlockSeal.h's messagePasserStorage snapshot, aligned with op-geth consensus.go:416-427
/// IntermediateRoot ordering):
///   processOpBlock → applyDiff per-transaction write-back → block-tail finalize → this function
///   → sealOpBlock.
/// Anti-circularity invariant (OP spec Isthmus exec-engine): during execution this block's
/// stateRoot does not yet exist and is structurally impossible to expose to the EVM — a
/// documented invariant, no runtime defense.
[[nodiscard]] evmone::hash256 stateRootOf(const evmone::test::TestState& state);
}  // namespace bcos::evmref
