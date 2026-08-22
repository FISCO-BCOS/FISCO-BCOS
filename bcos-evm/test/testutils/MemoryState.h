// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <map>
#include <utility>

namespace bcos::evm::evmstate
{
/// In-memory ledger account representation.
///
/// Storage never holds zero-valued slots (contract ②): a slot write of 0 deletes the entry.
struct StateAccount
{
    uint64_t nonce{0};
    intx::uint256 balance;
    evmc::bytes code;
    std::map<evmc::bytes32, evmc::bytes32> storage;  // zero-valued slots are not stored (contract
                                                     // ②)
};

/// Self-developed in-memory ledger backend (C-route step 1 replacement for
/// evmone::test::TestState).
///
/// KEEP semantics: a present map key means the account exists, even when its value is entirely
/// default (empty account) — existence and "all fields default" are distinct, unlike a design
/// that folds an empty account into std::nullopt.
class MemoryState final : public evmone::state::StateView
{
public:
    /// AccountView payload handed to the visitAccounts callback: state-root-relevant fields
    /// plus a lazy code getter (root computation only needs codeHash, not the code bytes) and a
    /// reference to the account's storage map for slot iteration.
    struct AccountView
    {
        const evmc::address& addr;
        uint64_t nonce;
        const intx::uint256& balance;
        evmc::bytes32 codeHash;
        const std::map<evmc::bytes32, evmc::bytes32>& storage;

        /// Lazily returns the account's code bytes; only invoked by visitors that actually need
        /// the code (e.g. debugging/export), not by state-root computation. Returns by value to
        /// mirror Storage2State::AccountView::code() — a generic visitor must work against
        /// either backend unchanged.
        [[nodiscard]] evmc::bytes code() const noexcept { return m_code; }

        const evmc::bytes& m_code;
    };

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override;
    evmc::bytes get_account_code(const evmc::address& addr) const noexcept override;
    evmc::bytes32 get_storage(
        const evmc::address& addr, const evmc::bytes32& key) const noexcept override;

    /// Applies a StateDiff. Single strict form: no "raw" variant is offered — a deleted_accounts
    /// entry absent from the ledger is a tripwire (std::runtime_error), and every
    /// modified_accounts entry unconditionally ensures the account exists (ensure-exists,
    /// including entries with no field actually changed — e.g. EIP-161 touch-only accounts).
    void applyDiff(const evmone::state::StateDiff& diff, bool seeding = false);

    /// Uniform query surface across ledger backends: MemoryState never poisons, so this is always
    /// false; Storage2State provides the real poison-flag semantics.
    [[nodiscard]] bool poisoned() const noexcept { return false; }

    /// Visits every account in the ledger (AccountVisitor contract, same shape as
    /// Storage2State::visitAccounts — noexcept + returns bool). The visitor is invoked with an
    /// AccountView and must itself return bool; returning false aborts the traversal early
    /// (short-circuit). Actually (not just nominally) noexcept: the in-memory backend has no
    /// failure path to catch-and-poison — poisoned() is a hardcoded false for this backend (the
    /// abstraction uniformly provides the query; backend asymmetry converges here), unlike
    /// Storage2State's catch-then-poison noexcept. Consumers should still check poisoned() for
    /// backend symmetry with Storage2State (a generic caller shouldn't need to know which
    /// backend it has).
    template <class Visitor>
    bool visitAccounts(Visitor&& visitor) const noexcept
    {
        for (const auto& [addr, account] : m_accounts)
        {
            // Zero-valued slots are normalized away (zero ≡ nonexistent, matching
            // Storage2State's read path) even when seeded raw via accounts() — otherwise the two
            // backends would disagree on the same logical state.
            std::map<evmc::bytes32, evmc::bytes32> liveStorage;
            for (const auto& [key, value] : account.storage)
            {
                if (!evmc::is_zero(value))
                    liveStorage.emplace(key, value);
            }
            const AccountView view{.addr = addr,
                .nonce = account.nonce,
                .balance = account.balance,
                .codeHash = evmone::keccak256(account.code),
                .storage = liveStorage,
                .m_code = account.code};
            if (!visitor(view))
                return false;
        }
        return true;
    }

    /// Direct access to the underlying account map, for test/seed callers. Contract ② still
    /// applies on the read side: a zero-valued slot seeded here is normalized away by
    /// get_account's has_storage computation and by visitAccounts (zero ≡ nonexistent, as in
    /// Storage2State), so seeding zero slots is pointless but harmless.
    [[nodiscard]] std::map<evmc::address, StateAccount>& accounts() noexcept { return m_accounts; }

private:
    std::map<evmc::address, StateAccount> m_accounts;
};
}  // namespace bcos::evm::evmstate
