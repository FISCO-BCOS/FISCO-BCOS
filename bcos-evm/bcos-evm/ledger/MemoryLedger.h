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

namespace bcos::evm::ledger
{
/// In-memory ledger account representation.
///
/// Storage never holds zero-valued slots (contract ②): a slot write of 0 deletes the entry.
struct LedgerAccount
{
    uint64_t nonce{0};
    intx::uint256 balance;
    evmc::bytes code;
    std::map<evmc::bytes32, evmc::bytes32> storage;  // 不存零值(契约②)
};

/// Self-developed in-memory ledger backend (C-route step 1 replacement for
/// evmone::test::TestState).
///
/// KEEP semantics (design §3/§4.4): a present map key means the account exists, even when its
/// value is entirely default (empty account) — existence and "all fields default" are distinct,
/// unlike a design that folds an empty account into std::nullopt.
class MemoryLedger final : public evmone::state::StateView
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
        /// the code (e.g. debugging/export), not by state-root computation.
        [[nodiscard]] const evmc::bytes& code() const noexcept { return m_code; }

        const evmc::bytes& m_code;
    };

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override;
    evmc::bytes get_account_code(const evmc::address& addr) const noexcept override;
    evmc::bytes32 get_storage(
        const evmc::address& addr, const evmc::bytes32& key) const noexcept override;

    /// Applies a StateDiff. Single strict form (design §5): no "raw" variant is offered — a
    /// deleted_accounts entry absent from the ledger is a tripwire (std::runtime_error), and every
    /// modified_accounts entry unconditionally ensures the account exists (ensure-exists,
    /// including entries with no field actually changed — e.g. EIP-161 touch-only accounts).
    void applyDiff(const evmone::state::StateDiff& diff);

    /// Uniform query surface across ledger backends (design §6): MemoryLedger never poisons, so
    /// this is always false; Storage2Ledger provides the real poison-flag semantics.
    [[nodiscard]] bool poisoned() const noexcept { return false; }

    /// Visits every account in the ledger. The visitor is invoked with an AccountView and must
    /// return bool; returning false aborts the traversal early (short-circuit). noexcept because
    /// the in-memory backend cannot fail; consumers should still check poisoned() for backend
    /// symmetry with Storage2Ledger.
    template <class Visitor>
    bool visitAccounts(Visitor&& visitor) const
    {
        for (const auto& [addr, account] : m_accounts)
        {
            const AccountView view{.addr = addr,
                .nonce = account.nonce,
                .balance = account.balance,
                .codeHash = evmone::keccak256(account.code),
                .storage = account.storage,
                .m_code = account.code};
            if (!visitor(view))
                return false;
        }
        return true;
    }

    /// Direct access to the underlying account map, for test/seed callers.
    [[nodiscard]] std::map<evmc::address, LedgerAccount>& accounts() noexcept { return m_accounts; }

private:
    std::map<evmc::address, LedgerAccount> m_accounts;
};
}  // namespace bcos::evm::ledger
