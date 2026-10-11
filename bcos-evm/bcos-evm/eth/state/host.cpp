// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "host.hpp"
#include "precompiles.hpp"
#include "system_contracts.hpp"
#include <evmone/constants.hpp>
#include <evmone/state_gas.hpp>

namespace evmone::state
{
bool Host::account_exists(const address& addr) const noexcept
{
    const auto* const acc = m_state.find(addr);
    return acc != nullptr && (m_rev < EVMC_SPURIOUS_DRAGON || !acc->is_empty());
}

bytes32 Host::get_storage(const address& addr, const bytes32& key) const noexcept
{
    return m_state.get_storage(addr, key).current;
}

evmc_storage_status Host::set_storage(
    const address& addr, const bytes32& key, const bytes32& value) noexcept
{
    // Follow EVMC documentation https://evmc.ethereum.org/storagestatus.html#autotoc_md3
    // and EIP-2200 specification https://eips.ethereum.org/EIPS/eip-2200.

    auto& storage_slot = m_state.get_storage(addr, key);
    const auto& [current, original, _] = storage_slot;

    const auto dirty = original != current;
    const auto restored = original == value;
    const auto current_is_zero = is_zero(current);
    const auto value_is_zero = is_zero(value);

    auto status = EVMC_STORAGE_ASSIGNED;  // All other cases.
    if (!dirty && !restored)
    {
        if (current_is_zero)
            status = EVMC_STORAGE_ADDED;  // 0 → 0 → Z
        else if (value_is_zero)
            status = EVMC_STORAGE_DELETED;  // X → X → 0
        else
            status = EVMC_STORAGE_MODIFIED;  // X → X → Z
    }
    else if (dirty && !restored)
    {
        if (current_is_zero && !value_is_zero)
            status = EVMC_STORAGE_DELETED_ADDED;  // X → 0 → Z
        else if (!current_is_zero && value_is_zero)
            status = EVMC_STORAGE_MODIFIED_DELETED;  // X → Y → 0
    }
    else if (dirty)
    {
        assert(restored);  // Always true.
        if (current_is_zero)
            status = EVMC_STORAGE_DELETED_RESTORED;  // X → 0 → X
        else if (value_is_zero)
            status = EVMC_STORAGE_ADDED_DELETED;  // 0 → Y → 0
        else
            status = EVMC_STORAGE_MODIFIED_RESTORED;  // X → Y → X
    }

    m_state.journal_storage_change(storage_slot);
    storage_slot.current = value;  // Update current value.
    return status;
}

uint256be Host::get_balance(const address& addr) const noexcept
{
    const auto* const acc = m_state.find(addr);
    return (acc != nullptr) ? intx::be::store<uint256be>(acc->balance) : uint256be{};
}

uint64_t Host::get_nonce(const address& addr) const noexcept
{
    const auto* const acc = m_state.find(addr);
    return (acc != nullptr) ? acc->nonce : 0;
}

namespace
{
/// Check if an existing account is the "create collision"
/// as defined in the [EIP-7610](https://eips.ethereum.org/EIPS/eip-7610).
[[nodiscard]] bool is_create_collision(const Account& acc) noexcept
{
    // TODO: This requires much more testing:
    // - what if an account had storage but is destructed?
    // - what if an account had cold storage but it was emptied?
    // - what if an account without cold storage gain one?
    if (acc.nonce != 0)
        return true;
    if (acc.code_hash != Account::EMPTY_CODE_HASH)
        return true;
    if (acc.has_initial_storage)
        return true;

    // The hot storage is ignored because it can contain elements from access list.
    // TODO: Is this correct for destructed accounts?
    assert(!acc.destructed && "untested");
    return false;
}
}  // namespace

size_t Host::get_code_size(const address& addr) const noexcept
{
    const auto raw_code = m_state.get_code(addr);
    return raw_code.size();
}

bytes32 Host::get_code_hash(const address& addr) const noexcept
{
    const auto* const acc = m_state.find(addr);
    if (acc == nullptr || acc->is_empty())
        return {};

    return acc->code_hash;
}

size_t Host::copy_code(const address& addr, size_t code_offset, uint8_t* buffer_data,
    size_t buffer_size) const noexcept
{
    const auto code = m_state.get_code(addr);
    const auto code_slice = code.substr(std::min(code_offset, code.size()));
    const auto num_bytes = std::min(buffer_size, code_slice.size());
    std::copy_n(code_slice.begin(), num_bytes, buffer_data);
    return num_bytes;
}

bool Host::selfdestruct(const address& addr, const address& beneficiary) noexcept
{
    if (m_state.find(beneficiary) == nullptr)
        m_state.journal_new_account(beneficiary);
    auto& acc = m_state.get(addr);
    const auto balance = acc.balance;
    auto& beneficiary_acc = m_state.touch(beneficiary);

    m_state.journal_balance_change(beneficiary, beneficiary_acc.balance);
    m_state.journal_balance_change(addr, balance);

    if (m_rev >= EVMC_CANCUN && !acc.just_created)
    {
        // EIP-6780:
        // "SELFDESTRUCT is executed in a transaction that is not the same
        // as the contract invoking SELFDESTRUCT was created"
        acc.balance = 0;
        beneficiary_acc.balance += balance;  // Keep balance if acc is the beneficiary.

        if (m_rev >= EVMC_AMSTERDAM)
            emit_transfer_log(m_logs, addr, beneficiary, balance);

        // Return "selfdestruct not registered".
        // In practice this affects only refunds before Cancun.
        return false;
    }

    if (m_rev < EVMC_AMSTERDAM || beneficiary != addr)
    {
        // Transfer may happen multiple times per single account as account's balance
        // can be increased with a call following previous selfdestruct.
        beneficiary_acc.balance += balance;
        acc.balance = 0;  // Zero balance if acc is the beneficiary (before EIP-8246)
    }

    if (m_rev >= EVMC_AMSTERDAM)
        emit_transfer_log(m_logs, addr, beneficiary, balance);

    // Mark the destruction if not done already.
    if (!acc.destructed)
    {
        m_state.journal_account_flags(addr, acc);
        acc.destructed = true;
        return true;
    }
    return false;
}

evmc::Result Host::create(const evmc_message& msg) noexcept
{
    assert(msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2);
    assert(msg.recipient != address{});  // Must be computed already.

    // TODO: find()+insert() probes m_modified twice for a new recipient.
    auto* new_acc = m_state.find(msg.recipient);
    if (new_acc == nullptr)
    {
        new_acc = &m_state.insert(msg.recipient);
        m_state.journal_new_account(msg.recipient);
    }
    else
    {
        // TODO: Add EVMC errors for creation failures.
        if (is_create_collision(*new_acc))
            return evmc::Result{EVMC_FAILURE, {.left = msg.state_gas}};
        m_state.journal_create(msg.recipient);
    }

    assert(new_acc != nullptr);
    assert(new_acc->nonce == 0);

    if (m_rev >= EVMC_SPURIOUS_DRAGON)
        new_acc->nonce = 1;  // No need to journal: create revert will 0 the nonce.

    new_acc->just_created = true;

    auto& sender_acc = m_state.get(msg.sender);  // TODO: Duplicated account lookup.
    const auto value = intx::be::load<intx::uint256>(msg.value);
    assert(sender_acc.balance >= value && "EVM must guarantee balance");
    m_state.journal_balance_change(msg.sender, sender_acc.balance);
    m_state.journal_balance_change(msg.recipient, new_acc->balance);
    sender_acc.balance -= value;
    new_acc->balance += value;  // The new account may be prefunded.

    if (m_rev >= EVMC_AMSTERDAM)
        emit_transfer_log(m_logs, msg.sender, msg.recipient, value);

    auto create_msg = msg;
    create_msg.input_data = nullptr;
    create_msg.input_size = 0;
    const bytes_view initcode{msg.input_data, msg.input_size};
    auto result = m_vm.execute(*this, m_rev, create_msg, initcode.data(), initcode.size());
    if (result.status_code != EVMC_SUCCESS)
        return result;

    auto gas_left = result.gas_left;
    assert(gas_left >= 0);

    const bytes_view code{result.output_data, result.output_size};

    const size_t max_code_size = m_rev >= EVMC_AMSTERDAM ? MAX_CODE_SIZE_AMSTERDAM : MAX_CODE_SIZE;
    if (m_rev >= EVMC_SPURIOUS_DRAGON && code.size() > max_code_size)
        return evmc::Result{EVMC_FAILURE, {.left = msg.state_gas}};

    // Reject new contract code starting with the 0xEF byte (EIP-3541).
    if (m_rev >= EVMC_LONDON && code.starts_with(0xEF))
        return evmc::Result{EVMC_CONTRACT_VALIDATION_FAILURE, {.left = msg.state_gas}};

    StateGas state_gas{result.state_gas};  // The initcode's state-gas for code deposit.
    if (m_rev >= EVMC_AMSTERDAM)
    {
        // The code deposit splits into an execution-gas and a state-gas component (EIP-8037).
        const auto execution_cost = 6 * ((std::ssize(code) + 31) / 32);
        const auto state_cost = std::ssize(code) * COST_PER_STATE_BYTE;
        gas_left -= execution_cost;
        if (gas_left < 0 || !state_gas.charge(gas_left, state_cost))
            return evmc::Result{EVMC_FAILURE, {.left = msg.state_gas}};
    }
    else
    {
        // Code deployment cost.
        const auto cost = std::ssize(code) * 200;
        gas_left -= cost;
        if (gas_left < 0)
        {
            return (m_rev == EVMC_FRONTIER) ?
                       evmc::Result{EVMC_SUCCESS, result.gas_left, result.gas_refund} :
                       evmc::Result{EVMC_FAILURE, {.left = msg.state_gas}};
        }
    }

    if (!code.empty())
    {
        new_acc->code_hash = keccak256(code);
        new_acc->code = code;
        new_acc->code_changed = true;
    }

    return evmc::Result{result.status_code, gas_left, result.gas_refund, state_gas};
}

evmc::Result Host::execute_message(const evmc_message& msg) noexcept
{
    if (msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2)
        return create(msg);

    if (msg.kind == EVMC_CALL)
    {
        auto* recipient_acc = m_state.find(msg.recipient);
        if (recipient_acc == nullptr)
            m_state.journal_new_account(msg.recipient);
        // TODO: Both branches will insert new account so better to do it in common path.

        if (evmc::is_zero(msg.value))
        {
            m_state.touch(msg.recipient);
        }
        else
        {
            // We skip touching if we send value, because account cannot end up empty.
            // It will either have value, or code that transfers this value out, or will be
            // selfdestructed anyway.
            if (recipient_acc == nullptr)
                recipient_acc = &m_state.insert(msg.recipient);

            // Transfer value: sender → recipient.
            // The sender's balance is already checked therefore the sender account must exist.
            const auto value = intx::be::load<intx::uint256>(msg.value);
            auto& sender_acc = m_state.get(msg.sender);
            assert(sender_acc.balance >= value);
            m_state.journal_balance_change(msg.sender, sender_acc.balance);
            m_state.journal_balance_change(msg.recipient, recipient_acc->balance);
            sender_acc.balance -= value;
            recipient_acc->balance += value;

            if (m_rev >= EVMC_AMSTERDAM)
                emit_transfer_log(m_logs, msg.sender, msg.recipient, value);
        }
    }

    // Calls to precompile address via EIP-7702 delegation execute empty code instead of precompile.
    if ((msg.flags & EVMC_DELEGATED) == 0 && is_precompile(m_rev, msg.code_address))
        return call_precompile(m_rev, msg);

    // TODO: get_code() performs the account lookup. Add a way to get an account with code?
    const auto code = m_state.get_code(msg.code_address);
    if (code.empty())  // Skip trivial execution.
        return evmc::Result{EVMC_SUCCESS, msg.gas, 0, {.left = msg.state_gas}};

    return m_vm.execute(*this, m_rev, msg, code.data(), code.size());
}

evmc::Result Host::call(const evmc_message& msg) noexcept
{
    if (msg.depth != 0 && (msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2))
    {
        // Bump the creator's nonce (already done for depth 0). Not reverted if creation fails.
        auto& sender_acc = m_state.get(msg.sender);
        assert(sender_acc.nonce != MAX_NONCE);
        m_state.journal_bump_nonce(msg.sender);
        ++sender_acc.nonce;
    }

    auto exec_msg = msg;
    StateGas state_gas{{.left = msg.state_gas}};  // State-gas for top-level account creation cost.
    if (msg.depth == 0 && m_rev >= EVMC_AMSTERDAM &&
        (msg.kind == EVMC_CREATE || !evmc::is_zero(msg.value)) && !account_exists(msg.recipient))
    {
        if (!state_gas.charge(exec_msg.gas, NEW_ACCOUNT_STATE_GAS))
            return evmc::Result{EVMC_OUT_OF_GAS, {.left = msg.state_gas}};
        exec_msg.state_gas = state_gas.left;
    }

    const auto logs_checkpoint = m_logs.size();
    const auto state_checkpoint = m_state.checkpoint();

    auto result = execute_message(exec_msg);

    if (result.status_code == EVMC_SUCCESS)
    {
        result.state_gas.spilled += state_gas.spilled;  // Commit the top-level new account cost.
    }
    else
    {
        // Rollback state-gas costs.
        assert(result.state_gas.left == exec_msg.state_gas);
        assert(result.state_gas.spilled == 0);
        if (result.status_code == EVMC_REVERT)
            result.gas_left += state_gas.spilled;
        result.state_gas.left = msg.state_gas;

        // The 0x03 (RIPEMD-160) touch quirk: a touch on this address is
        // never reverted. It only matters when the account is empty, so gate it by rev range.
        static constexpr auto ADDR_03 = 0x03_address;
        bool is_03_touched = false;
        if (m_rev < EVMC_PARIS && m_rev >= EVMC_SPURIOUS_DRAGON) [[unlikely]]
        {
            const auto* const acc_03 = m_state.find(ADDR_03);
            is_03_touched = acc_03 != nullptr && acc_03->erase_if_empty;
        }

        // Revert.
        m_state.rollback(state_checkpoint);
        m_logs.resize(logs_checkpoint);

        if (is_03_touched) [[unlikely]]
            m_state.touch(ADDR_03);
    }
    return result;
}

evmc_tx_context Host::get_tx_context() const noexcept
{
    // TODO: The effective gas price is already computed in transaction validation.
    // TODO: The effective gas price calculation is broken for system calls (gas price 0).
    assert(m_tx.max_gas_price >= m_block.base_fee || m_tx.max_gas_price == 0);
    const auto priority_gas_price =
        std::min(m_tx.max_priority_gas_price, m_tx.max_gas_price - m_block.base_fee);
    const auto effective_gas_price = m_block.base_fee + priority_gas_price;

    return evmc_tx_context{
        intx::be::store<uint256be>(effective_gas_price),  // By EIP-1559.
        m_tx.sender,
        m_block.coinbase,
        m_block.number,
        m_block.timestamp,
        m_block.gas_limit,
        m_block.prev_randao,
        uint256be{m_block.chain_id},
        uint256be{m_block.base_fee},
        intx::be::store<uint256be>(m_block.blob_base_fee.value_or(0)),
        m_tx.blob_hashes.data(),
        m_tx.blob_hashes.size(),
        m_block.slot_number.value_or(0),
    };
}

bytes32 Host::get_block_hash(int64_t block_number) const noexcept
{
    return m_block_hashes.get_block_hash(block_number);
}

void Host::emit_log(const address& addr, const uint8_t* data, size_t data_size,
    const bytes32 topics[], size_t topics_count) noexcept
{
    m_logs.push_back({addr, {data, data_size}, {topics, topics + topics_count}});
}

evmc_access_status Host::access_account(const address& addr) noexcept
{
    if (m_rev < EVMC_BERLIN)
        return EVMC_ACCESS_COLD;  // Ignore before Berlin.

    auto* acc = m_state.find(addr);

    if (acc != nullptr && acc->access_status == EVMC_ACCESS_WARM)
        return EVMC_ACCESS_WARM;

    if (is_precompile(m_rev, addr))  // Precompiles are always warm. Don't insert to state.
        return EVMC_ACCESS_WARM;

    // TODO: On a modified-set miss the account is looked up twice. This can be improved with
    //   a try_emplace-like API, but the miss happens only in ~39% of the calls on Mainnet.
    if (acc == nullptr)
    {
        acc = &m_state.insert(addr, {.erase_if_empty = true});
        m_state.journal_new_account(addr);
    }
    else
        m_state.journal_account_flags(addr, *acc);

    acc->access_status = EVMC_ACCESS_WARM;
    return EVMC_ACCESS_COLD;
}

evmc_access_status Host::access_storage(const address& addr, const bytes32& key) noexcept
{
    auto& storage_slot = m_state.get_storage(addr, key);
    if (storage_slot.access_status == EVMC_ACCESS_WARM)
        return EVMC_ACCESS_WARM;  // Nothing changes, skip journaling.
    m_state.journal_storage_change(storage_slot);
    storage_slot.access_status = EVMC_ACCESS_WARM;
    return EVMC_ACCESS_COLD;
}


evmc::bytes32 Host::get_transient_storage(const address& addr, const bytes32& key) const noexcept
{
    const auto& acc = m_state.get(addr);
    const auto it = acc.transient_storage.find(key);
    return it != acc.transient_storage.end() ? it->second : bytes32{};
}

void Host::set_transient_storage(
    const address& addr, const bytes32& key, const bytes32& value) noexcept
{
    auto& slot = m_state.get(addr).transient_storage[key];
    m_state.journal_transient_storage_change(slot);
    slot = value;
}
}  // namespace evmone::state
