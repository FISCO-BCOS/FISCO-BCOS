// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// LedgerSeed -- unified seeding: synthesizes a genesis StateDiff from the vector `pre`
// (evmone::test::TestState) and lands it through the same applyDiff path. Works for both
// MemoryLedger and Storage2Ledger (applyDiff is their common write-back interface), so seeding
// logic is not written per backend -- serialization/ensure-exists/contracts ②③ are delegated to
// each backend's applyDiff; this file only reshapes TestState's map into a StateDiff.
//
// Field mapping:
//   - nonce/balance copied directly;
//   - code: an empty pre-account code leaves StateDiff::Entry::code as std::nullopt (contract ③
//     "only overwrite when has_value()"; an empty code does not write an empty overwrite, which
//     naturally matches the default "account exists but has no code" state); non-empty code is
//     carried explicitly.
//   - storage: TestState's storage map contains no zero-valued slots (the loader strips them at
//     parse time per the trie "0 ≡ absent" convention, see T8nReplayHarness.h's pre-parse
//     comment), so each pair is copied as-is into modified_storage; after landing, applyDiff's
//     contract ② handles it (non-zero writes only, never triggers the delete-slot branch).
//   - deleted_accounts always empty: seeding only creates, never deletes.
//
// Completely empty accounts (EIP-161 touch-delete vector preconditions): an Entry with
// nonce=0/balance=0/code=nullopt/empty modified_storage still enters modified_accounts, and
// applyDiff's ensure-exists contract guarantees it is unconditionally landed, rather than being
// skipped by a wrong "no fields to write" optimization.
//
// `ledger.applyDiff(diff, /*seeding=*/true)` explicitly goes through seeding mode.
// Storage2Ledger's guard treats "creating an EIP-161 empty account on the ledger" as a protocol
// violation (block-execution path would turn red -> -32603), but a completely empty account in
// `pre` is a legitimate part of a genesis snapshot (the three backends share the KEEP contract)
// and must be exempted -- this file declares "this applyDiff is seeding, not block execution"
// via seeding=true. MemoryLedger has no such guard; the parameter is ignored.

#include <bcos-evm/eth/state/state_diff.hpp>
#include <test/utils/test_state.hpp>

namespace bcos::evm::ledger
{

template <class Ledger>
void seedFromTestState(Ledger& ledger, const evmone::test::TestState& pre)
{
    evmone::state::StateDiff diff;
    diff.modified_accounts.reserve(pre.size());
    for (const auto& [addr, account] : pre)
    {
        evmone::state::StateDiff::Entry entry;
        entry.addr = addr;
        entry.nonce = account.nonce;
        entry.balance = account.balance;
        if (!account.code.empty())
            entry.code = account.code;
        entry.modified_storage.reserve(account.storage.size());
        for (const auto& [key, value] : account.storage)
            entry.modified_storage.emplace_back(key, value);
        diff.modified_accounts.push_back(std::move(entry));
    }
    ledger.applyDiff(diff, /*seeding=*/true);
}

}  // namespace bcos::evm::ledger
