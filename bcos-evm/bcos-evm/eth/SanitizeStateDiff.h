#pragma once

#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <vector>

namespace bcos::evm
{
/// Strip deletion entries for accounts absent from the view (FINDING-1 ghost-delete fix): an
/// account fabricated by get_or_insert(erase_if_empty) never existed and must not become a ledger
/// deletion. Genuine EIP-161 touch-deletes (account present in the view) pass through unchanged.
/// The view-based predicate also strips EIP-6780 same-tx create+selfdestruct entries: an account
/// born and destroyed within one tx is absent from the view when the diff is built, so the
/// deletion (correctly) becomes a no-op instead of a ghost-delete tripwire.
///
/// Mandatory call-site rule: view must be the same StateView the diff was computed from, evaluated
/// before the diff is applyDiff'd.
///
/// View contract: get_account().has_value() must mean "exists in the ledger", never collapse
/// "exists but empty" into nullopt — otherwise a legitimate EIP-161 touch-delete is wrongly
/// stripped (a silent fork from op-geth's state commitment). TestState satisfies this; future
/// ledger bridges must obey it.
[[nodiscard]] inline evmone::state::StateDiff sanitizeStateDiff(
    const evmone::state::StateView& view, evmone::state::StateDiff diff)
{
    std::erase_if(diff.deleted_accounts,
        [&view](const auto& addr) { return !view.get_account(addr).has_value(); });
    return diff;
}
}  // namespace bcos::evm
