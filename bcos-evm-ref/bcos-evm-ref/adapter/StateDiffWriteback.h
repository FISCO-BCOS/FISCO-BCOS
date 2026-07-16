// bcos-evm-ref/include/bcos-evm-ref/adapter/StateDiffWriteback.h
#pragma once

#include <test/state/state_diff.hpp>
#include <test/utils/test_state.hpp>

namespace bcos::evmref
{
/// v1 writeback seam: applies an evmone StateDiff to the in-memory TestState.
/// Contract (a real-ledger writeback implementation must satisfy this; see the StateDiffWritebackTest
/// seam-contract cases):
///   1) deleted_accounts must be deleted (not always empty after Cancun: EIP-6780 same-transaction
///      self-destruct and EIP-161 empty-account erasure both produce deletion entries; the
///      "always empty" comment in state_diff.hpp is outdated);
///   2) a modified_storage value of 0 means the slot is deleted (erase), not stored as zero;
///   3) code is overwritten only when has_value().
inline void applyStateDiff(evmone::test::TestState& state, const evmone::state::StateDiff& diff)
{
    state.apply(diff);
}
}  // namespace bcos::evmref
