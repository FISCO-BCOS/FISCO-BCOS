// Test-only strict StateDiff write-back helper (moved from bcos-evm/adapter/: all
#pragma once

#include <evmc/hex.hpp>
#include <bcos-evm/eth/state/state_diff.hpp>
// TODO(eth-utils-removal): TestState(eth/utils)→自研内存账本;applyStateDiff/
// applyStateDiffStrict 形参与下方三条写回契约(删除/清槽/条件覆写)原样保留。
#include <test/utils/test_state.hpp>
#include <stdexcept>

namespace bcos::evm
{
/// v1 write-back seam: apply an evmone StateDiff to the in-memory TestState.
/// Contract (a real-ledger write-back implementation must satisfy this; the block-level
/// seam-contract test suite that exercises it is out of this PR's tx-level scope):
///   1) deleted_accounts must be deleted (not always empty after Cancun: EIP-6780 same-tx
///      selfdestruct and EIP-161 empty-account erasure both produce deletion entries; the
///      "always empty" comment in state_diff.hpp is outdated);
///   2) a modified_storage value of 0 means erase the slot, not store zero;
///   3) code is overwritten only when has_value().
/// For a diff sanitized at its production site (see StateDiffSanitize.h), the deleted_accounts
/// entries are guaranteed to exist in the view.
inline void applyStateDiff(evmone::test::TestState& state, const evmone::state::StateDiff& diff)
{
    state.apply(diff);
}

/// Consumer-side tripwire (spec §3.4): delete-of-nonexistent is precisely the ghost signature
/// (in the test context the write-back target ≡ view and is applied synchronously; the EIP-6780
/// class is already stripped at the production site, and cross-tx duplicate deletes are stripped
/// by the post-prior-tx view sanitize — zero false positives). Any future exit that misses
/// sanitizing turns the write-back-type tests red immediately, without relying on corpus coverage.
/// Tests that verify the raw, un-sanitized behavior continue to use the raw applyStateDiff.
inline void applyStateDiffStrict(
    evmone::test::TestState& state, const evmone::state::StateDiff& diff)
{
    for (const auto& addr : diff.deleted_accounts)
        if (state.find(addr) == state.end())
            throw std::runtime_error(
                "ghost delete reached writeback: " + evmc::hex(evmc::bytes_view(addr)));
    applyStateDiff(state, diff);
}
}  // namespace bcos::evm
