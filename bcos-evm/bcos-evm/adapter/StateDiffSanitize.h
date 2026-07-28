#pragma once

#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <vector>

namespace bcos::evm
{
/// Strip deletion entries for accounts absent from the view out of the StateDiff (FINDING-1
/// ghost-delete fix, spec rev.2).
/// Retained: deletions of accounts that exist in the view (including empty ones) — genuine
/// EIP-161 touch-delete, aligned with op-geth (a purely access-list-listed pre-existing empty
/// account is deleted on neither side — evmone get_or_insert finds the existing account and
/// discards the erase_if_empty default arg, geth bookkeeping does not mark it dirty).
/// Two classes are stripped: ghosts (fabricated by get_or_insert(erase_if_empty)); EIP-6780
/// same-tx create+selfdestruct (the destructed branch emits it, the view is necessarily absent
/// — intentionally stripped, same-tx birth+death leaves zero trace in the ledger, op-geth also
/// does not persist it). All other fields pass through unchanged.
/// View-ordering rule (mandatory): view must be the same StateView that build_diff/finalize
/// relied on when computing this diff, and must be evaluated before the diff has been applyDiff'd
/// — call only at the production site.
/// op-geth semantic anchor: statedb.go:1485-1512 (access list is pure in-memory bookkeeping),
/// op-geth @ v1.101702.2.
/// **StateView adapter contract (KEEP direction, final-review pin-down)**: get_account(addr)
/// .has_value() must ⇔ the account exists in the ledger's committed representation — it must
/// **not** collapse "exists but empty" into nullopt. Otherwise a legitimate EIP-161 touch-delete
/// would be wrongly stripped by this function, leaving an empty-account record in the ledger →
/// a silent fork from op-geth's state commitment. TestState satisfies this contract (a record,
/// once present, has a value, including empty accounts); future production ledger views
/// (M3.5 Phase 2 bridge) must obey it when implemented.
[[nodiscard]] inline evmone::state::StateDiff sanitizeStateDiff(
    const evmone::state::StateView& view, evmone::state::StateDiff diff)
{
    std::erase_if(diff.deleted_accounts,
        [&view](const auto& addr) { return !view.get_account(addr).has_value(); });
    return diff;
}
}  // namespace bcos::evm
