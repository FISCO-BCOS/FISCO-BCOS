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
 * @brief Execution-state journal with checkpoint/revert and EIP-2929 access tracking.
 * @file State.hpp
 */

#pragma once

#include "bcos-evm/eth/state/EvmStateReader.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include <optional>
#include <unordered_set>
#include <vector>

namespace bcos::evm::state
{
struct WarmStorageKeyHash
{
    size_t operator()(std::pair<evmc_address, evmc_bytes32> const& value) const noexcept
    {
        size_t hash = AddressHash{}(value.first);
        boost::hash_combine(hash, Bytes32Hash{}(value.second));
        return hash;
    }
};

struct WarmStorageKeyEqual
{
    bool operator()(std::pair<evmc_address, evmc_bytes32> const& lhs,
        std::pair<evmc_address, evmc_bytes32> const& rhs) const noexcept
    {
        return AddressEqual{}(lhs.first, rhs.first) && Bytes32Equal{}(lhs.second, rhs.second);
    }
};

class State : public EvmStateReader
{
public:
    explicit State(EvmStateReader const& baseEvmStateReader);

    [[nodiscard]] std::optional<Account> get_account(const evmc_address& address) const override;
    [[nodiscard]] bcos::u256 get_balance(const evmc_address& address) const override;
    [[nodiscard]] uint64_t get_nonce(const evmc_address& address) const override;
    [[nodiscard]] bcos::bytes get_code(const evmc_address& address) const override;
    [[nodiscard]] evmc_bytes32 get_code_hash(const evmc_address& address) const override;
    [[nodiscard]] evmc_bytes32 get_storage(
        const evmc_address& address, const evmc_bytes32& key) const override;
    [[nodiscard]] std::optional<Account> find(const evmc_address& address) const;

    void checkpoint();
    void revert();
    void commit();
    [[nodiscard]] bool has_checkpoint() const noexcept;

    void set_balance(const evmc_address& address, const bcos::u256& balance);
    void set_nonce(const evmc_address& address, uint64_t nonce);
    void set_code(const evmc_address& address, bcos::bytes code, evmc_bytes32 codeHash);
    void set_storage(
        const evmc_address& address, const evmc_bytes32& key, const evmc_bytes32& value);
    void clear_storage(const evmc_address& address);
    void set_transient_storage(
        const evmc_address& address, const evmc_bytes32& key, const evmc_bytes32& value);
    /// Clears in-memory transient slots for all overlay accounts (geth Prepare parity).
    void clearAllTransientStorage();

    [[nodiscard]] bool warm_up_address(const evmc_address& address);
    [[nodiscard]] bool warm_up_storage(const evmc_address& address, const evmc_bytes32& key);
    [[nodiscard]] bool warm_up_address_no_journal(const evmc_address& address);
    [[nodiscard]] bool warm_up_storage_no_journal(
        const evmc_address& address, const evmc_bytes32& key);
    void pin_warm_create_address(const evmc_address& address);
    [[nodiscard]] bool is_address_warm(const evmc_address& address) const;
    [[nodiscard]] bool is_storage_warm(const evmc_address& address, const evmc_bytes32& key) const;

    [[nodiscard]] StateDiff build_diff() const;

    void mark_self_destructed(const evmc_address& address);
    [[nodiscard]] bool has_self_destructed(const evmc_address& address) const;
    void finalize_self_destructs();

    void add_refund(uint64_t amount);
    void sub_refund(uint64_t amount);
    [[nodiscard]] uint64_t get_refund() const noexcept;
    void clear_refund() noexcept;

private:
    enum class JournalType : uint8_t
    {
        AccountSnapshot,
        WarmAddressInsert,
        WarmStorageInsert,
        CreateWarmPinInsert,
    };

    struct JournalEntry
    {
        JournalType type{JournalType::AccountSnapshot};
        evmc_address address{};
        evmc_bytes32 key{};
        std::optional<Account> previousAccount;
        /// True when pin_warm_create_address inserted into m_warmAccounts.
        bool pinInsertedWarm{false};
    };

    struct Checkpoint
    {
        size_t journalSize{0};
        uint64_t gasRefund{0};
        std::unordered_set<evmc_address, AddressHash, AddressEqual> touchedAccounts;
    };

    Account& mutable_account(const evmc_address& address);
    void journal_account_once(const evmc_address& address);
    void push_journal_account(const evmc_address& address, std::optional<Account> previous);
    void push_journal_warm_address(const evmc_address& address);
    void push_journal_warm_storage(const evmc_address& address, const evmc_bytes32& key);
    void push_journal_create_warm_pin(const evmc_address& address, bool insertedWarm);

private:
    EvmStateReader const* m_baseStateView;
    std::unordered_map<evmc_address, Account, AddressHash, AddressEqual> m_accounts;
    std::unordered_set<evmc_address, AddressHash, AddressEqual> m_warmAccounts;
    std::unordered_set<evmc_address, AddressHash, AddressEqual> m_pinnedWarmAccounts;
    std::unordered_set<std::pair<evmc_address, evmc_bytes32>, WarmStorageKeyHash,
        WarmStorageKeyEqual>
        m_warmStorage;
    std::vector<JournalEntry> m_journal;
    std::vector<Checkpoint> m_checkpoints;
    uint64_t m_gasRefund{0};
};

inline void installCreatedContractCode(
    State& state, const evmc_message& message, const evmc_result& result)
{
    if (result.status_code != EVMC_SUCCESS || result.output_size == 0 ||
        result.output_data == nullptr)
    {
        return;
    }
    if (message.kind != EVMC_CREATE && message.kind != EVMC_CREATE2)
    {
        return;
    }

    auto createAddr = message.recipient;
    if (isZeroAddress(createAddr))
    {
        createAddr = message.code_address;
    }
    if (isZeroAddress(createAddr))
    {
        createAddr = result.create_address;
    }
    if (isZeroAddress(createAddr))
    {
        return;
    }

    bcos::bytes code(result.output_data, result.output_data + result.output_size);
    state.set_code(createAddr, std::move(code), {});
}
}  // namespace bcos::evm::state
