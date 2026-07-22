/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file State.cpp
 */

#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"

namespace bcos::evm::state
{
State::State(EvmStateReader const& baseEvmStateReader) : m_baseStateView(&baseEvmStateReader) {}

std::optional<Account> State::find(const evmc_address& address) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        return it->second;
    }
    return m_baseStateView->get_account(address);
}

std::optional<Account> State::get_account(const evmc_address& address) const
{
    return find(address);
}

bcos::u256 State::get_balance(const evmc_address& address) const
{
    auto const account = find(address);
    return account.has_value() ? account->balance : bcos::u256{0};
}

uint64_t State::get_nonce(const evmc_address& address) const
{
    auto const account = find(address);
    return account.has_value() ? account->nonce : 0;
}

bcos::bytes State::get_code(const evmc_address& address) const
{
    auto const account = find(address);
    return account.has_value() ? account->code : bcos::bytes{};
}

evmc_bytes32 State::get_code_hash(const evmc_address& address) const
{
    auto const account = find(address);
    if (!account.has_value())
    {
        return {};
    }
    if (account->code.empty())
    {
        return emptyCodeHash();
    }
    if (!isZeroBytes32(account->codeHash))
    {
        return account->codeHash;
    }
    return keccak256Code(bcos::bytesConstRef{account->code.data(), account->code.size()});
}

evmc_bytes32 State::get_storage(const evmc_address& address, const evmc_bytes32& key) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        if (auto storageIt = it->second.storage.find(key); storageIt != it->second.storage.end())
        {
            return storageIt->second;
        }
    }
    return m_baseStateView->get_storage(address, key);
}

bool State::has_checkpoint() const noexcept
{
    return !m_checkpoints.empty();
}

void State::checkpoint()
{
    m_checkpoints.push_back({m_journal.size(), m_gasRefund, {}});
}

void State::commit()
{
    if (m_checkpoints.empty())
    {
        return;
    }

    if (m_checkpoints.size() >= 2)
    {
        auto top = std::move(m_checkpoints.back());
        m_checkpoints.pop_back();
        auto& parent = m_checkpoints.back();
        for (auto const& address : top.touchedAccounts)
        {
            parent.touchedAccounts.insert(address);
        }
        return;
    }

    m_checkpoints.pop_back();
}

void State::revert()
{
    if (m_checkpoints.empty())
    {
        return;
    }

    auto const checkpoint = std::move(m_checkpoints.back());
    m_checkpoints.pop_back();

    while (m_journal.size() > checkpoint.journalSize)
    {
        auto const entry = std::move(m_journal.back());
        m_journal.pop_back();
        switch (entry.type)
        {
        case JournalType::AccountSnapshot:
            if (entry.previousAccount.has_value())
            {
                m_accounts[entry.address] = *entry.previousAccount;
            }
            else
            {
                m_accounts.erase(entry.address);
            }
            break;
        case JournalType::WarmAddressInsert:
            if (!m_pinnedWarmAccounts.contains(entry.address))
            {
                m_warmAccounts.erase(entry.address);
            }
            break;
        case JournalType::WarmStorageInsert:
            m_warmStorage.erase({entry.address, entry.key});
            break;
        case JournalType::CreateWarmPinInsert:
            m_pinnedWarmAccounts.erase(entry.address);
            if (entry.pinInsertedWarm)
            {
                m_warmAccounts.erase(entry.address);
            }
            break;
        }
    }

    m_gasRefund = checkpoint.gasRefund;
}

Account& State::mutable_account(const evmc_address& address)
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        return it->second;
    }

    auto account = m_baseStateView->get_account(address);
    auto [it, _] = m_accounts.emplace(address, account.value_or(Account{}));
    return it->second;
}

void State::push_journal_account(const evmc_address& address, std::optional<Account> previous)
{
    m_journal.push_back(
        JournalEntry{JournalType::AccountSnapshot, address, evmc_bytes32{}, std::move(previous)});
}

void State::push_journal_warm_address(const evmc_address& address)
{
    m_journal.push_back(
        JournalEntry{JournalType::WarmAddressInsert, address, evmc_bytes32{}, std::nullopt});
}

void State::push_journal_warm_storage(const evmc_address& address, const evmc_bytes32& key)
{
    m_journal.push_back(JournalEntry{JournalType::WarmStorageInsert, address, key, std::nullopt});
}

void State::push_journal_create_warm_pin(const evmc_address& address, bool insertedWarm)
{
    JournalEntry entry{};
    entry.type = JournalType::CreateWarmPinInsert;
    entry.address = address;
    entry.pinInsertedWarm = insertedWarm;
    m_journal.push_back(std::move(entry));
}

void State::journal_account_once(const evmc_address& address)
{
    if (m_checkpoints.empty())
    {
        return;
    }

    auto& checkpoint = m_checkpoints.back();
    if (!checkpoint.touchedAccounts.insert(address).second)
    {
        return;
    }

    push_journal_account(address, find(address));
}

void State::set_balance(const evmc_address& address, const bcos::u256& balance)
{
    journal_account_once(address);
    auto& account = mutable_account(address);
    account.balance = balance;
    account.balanceDirty = true;
}

void State::set_nonce(const evmc_address& address, uint64_t nonce)
{
    journal_account_once(address);
    auto& account = mutable_account(address);
    account.nonce = nonce;
    account.nonceDirty = true;
}

void State::set_code(const evmc_address& address, bcos::bytes code, evmc_bytes32 codeHash)
{
    journal_account_once(address);
    auto& account = mutable_account(address);
    account.code = std::move(code);
    account.codeHash = codeHash;
    account.codeDirty = true;
}

void State::set_storage(
    const evmc_address& address, const evmc_bytes32& key, const evmc_bytes32& value)
{
    auto const prior = get_storage(address, key);
    if (Bytes32Equal{}(prior, value))
    {
        return;
    }

    journal_account_once(address);
    mutable_account(address).storage[key] = value;
}

void State::clear_storage(const evmc_address& address)
{
    journal_account_once(address);
    mutable_account(address).storage.clear();
}

void State::set_transient_storage(
    const evmc_address& address, const evmc_bytes32& key, const evmc_bytes32& value)
{
    journal_account_once(address);
    mutable_account(address).transientStorage[key] = value;
}

void State::clearAllTransientStorage()
{
    for (auto& [address, account] : m_accounts)
    {
        (void)address;
        account.transientStorage.clear();
    }
}

bool State::warm_up_address(const evmc_address& address)
{
    auto const inserted = m_warmAccounts.insert(address).second;
    if (inserted && has_checkpoint())
    {
        push_journal_warm_address(address);
    }
    return inserted;
}

bool State::warm_up_storage(const evmc_address& address, const evmc_bytes32& key)
{
    auto const inserted = m_warmStorage.insert({address, key}).second;
    if (inserted && has_checkpoint())
    {
        push_journal_warm_storage(address, key);
    }
    return inserted;
}

bool State::warm_up_address_no_journal(const evmc_address& address)
{
    return m_warmAccounts.insert(address).second;
}

bool State::warm_up_storage_no_journal(const evmc_address& address, const evmc_bytes32& key)
{
    return m_warmStorage.insert({address, key}).second;
}

void State::pin_warm_create_address(const evmc_address& address)
{
    bool const insertedWarm = m_warmAccounts.insert(address).second;
    m_pinnedWarmAccounts.insert(address);
    if (has_checkpoint())
    {
        push_journal_create_warm_pin(address, insertedWarm);
    }
}

bool State::is_address_warm(const evmc_address& address) const
{
    return m_warmAccounts.contains(address);
}

bool State::is_storage_warm(const evmc_address& address, const evmc_bytes32& key) const
{
    return m_warmStorage.contains({address, key});
}

StateDiff State::build_diff() const
{
    StateDiff diff;
    diff.accounts.reserve(m_accounts.size());
    for (auto const& [address, account] : m_accounts)
    {
        Account persisted = account;
        persisted.transientStorage.clear();
        diff.accounts.emplace(address, std::move(persisted));
    }
    return diff;
}

void State::mark_self_destructed(const evmc_address& address)
{
    journal_account_once(address);
    mutable_account(address).selfDestructed = true;
}

bool State::has_self_destructed(const evmc_address& address) const
{
    auto const account = find(address);
    return account.has_value() && account->selfDestructed;
}

void State::finalize_self_destructs()
{
    for (auto& [address, account] : m_accounts)
    {
        if (!account.selfDestructed)
        {
            continue;
        }
        account.code.clear();
        account.codeHash = {};
        account.storage.clear();
        account.balance = 0;
        account.nonce = 0;
        account.selfDestructed = false;
    }
}

void State::add_refund(uint64_t amount)
{
    m_gasRefund += amount;
}

void State::sub_refund(uint64_t amount)
{
    m_gasRefund = (m_gasRefund >= amount) ? (m_gasRefund - amount) : 0;
}

uint64_t State::get_refund() const noexcept
{
    return m_gasRefund;
}

void State::clear_refund() noexcept
{
    m_gasRefund = 0;
}
}  // namespace bcos::evm::state
