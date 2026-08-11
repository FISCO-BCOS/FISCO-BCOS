// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#include <bcos-evm/ledger/MemoryLedger.h>

#include <stdexcept>

namespace bcos::evm::ledger
{
std::optional<MemoryLedger::Account> MemoryLedger::get_account(
    const evmc::address& addr) const noexcept
{
    const auto it = m_accounts.find(addr);
    if (it == m_accounts.end())
        return std::nullopt;

    const auto& account = it->second;
    // KEEP: a present-but-empty account still returns a value; has_storage's dynamic semantics
    // align with TestState.
    return Account{
        account.nonce, account.balance, evmone::keccak256(account.code), !account.storage.empty()};
}

evmc::bytes MemoryLedger::get_account_code(const evmc::address& addr) const noexcept
{
    const auto it = m_accounts.find(addr);
    if (it == m_accounts.end())
        return {};

    return it->second.code;
}

evmc::bytes32 MemoryLedger::get_storage(
    const evmc::address& addr, const evmc::bytes32& key) const noexcept
{
    const auto ait = m_accounts.find(addr);
    if (ait == m_accounts.end())
        return evmc::bytes32{};

    const auto& storage = ait->second.storage;
    const auto it = storage.find(key);
    return (it != storage.end()) ? it->second : evmc::bytes32{};
}

void MemoryLedger::applyDiff(const evmone::state::StateDiff& diff, bool seeding)
{
    // `seeding` is accepted only to keep the interface uniform with Storage2Ledger::applyDiff
    // (seedFromTestState calls `ledger.applyDiff(diff, true)` for both ledger types); MemoryLedger
    // has no empty-account guard, so the parameter has no behavior and is ignored.
    (void)seeding;

    for (const auto& m : diff.modified_accounts)
    {
        // ensure-exists: unconditionally ensure the entry exists, even when no field actually
        // changes (EIP-161 touch-only accounts / fully-empty account seeding) -- must not optimize
        // to "skip when no field is writable".
        auto& account = m_accounts[m.addr];
        account.nonce = m.nonce;
        account.balance = m.balance;
        if (m.code.has_value())  // contract ③: a missing code value does not overwrite
            account.code = *m.code;
        for (const auto& [key, value] : m.modified_storage)
        {
            if (value)  // contract ②: a slot value of 0 deletes the slot; zero values are not written
                account.storage.insert_or_assign(key, value);
            else
                account.storage.erase(key);
        }
    }

    for (const auto& addr : diff.deleted_accounts)
    {
        // Single strict form: tripwire built in, no raw variant -- an absent view entry is a
        // usage error.
        const auto it = m_accounts.find(addr);
        if (it == m_accounts.end())
            throw std::runtime_error(
                "MemoryLedger::applyDiff: deleted_accounts entry not found in ledger (ghost "
                "delete, strict tripwire)");
        m_accounts.erase(it);
    }
}
}  // namespace bcos::evm::ledger
