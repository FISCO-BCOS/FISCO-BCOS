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
 * @file EthHost.cpp
 */

#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/Transfer.h"
#include "bcos-evm/eth/execution/Eip2929Access.h"
#include "bcos-evm/eth/execution/ExecutionFrame.h"
#include "bcos-evm/eth/gas/Eip2929StorageGas.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace bcos::evm::state
{
namespace
{
using bcos::evm::gas::COLD_SLOAD_COST_EIP2929;
using bcos::evm::gas::SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529;
using bcos::evm::gas::SSTORE_RESET_GAS_EIP2200;
using bcos::evm::gas::SSTORE_SET_GAS_EIP2200;
using bcos::evm::gas::WARM_STORAGE_READ_COST_EIP2929;

void applySstoreRefundEip3529(State& state, const evmc_bytes32& current,
    const evmc_bytes32& original, const evmc_bytes32& value) noexcept
{
    if (Bytes32Equal{}(current, value))
    {
        return;
    }
    if (Bytes32Equal{}(original, current))
    {
        if (isZeroBytes32(original))
        {
            return;
        }
        if (isZeroBytes32(value))
        {
            state.add_refund(SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529);
        }
        return;
    }
    if (!isZeroBytes32(original))
    {
        if (isZeroBytes32(current))
        {
            state.sub_refund(SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529);
        }
        else if (isZeroBytes32(value))
        {
            state.add_refund(SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529);
        }
    }
    if (Bytes32Equal{}(original, value))
    {
        if (isZeroBytes32(original))
        {
            state.add_refund(SSTORE_SET_GAS_EIP2200 - WARM_STORAGE_READ_COST_EIP2929);
        }
        else
        {
            state.add_refund((SSTORE_RESET_GAS_EIP2200 - COLD_SLOAD_COST_EIP2929) -
                             WARM_STORAGE_READ_COST_EIP2929);
        }
    }
}

}  // namespace

EthHost::EthHost(State& state, evmc_tx_context txContext,
    bcos::evm_standard::RevisionConfig revisionConfig, evmc::VM& vm, BlockHashes blockHashes,
    VmHostPolicy* extension, bool fixStorageStatus, ChainCallTargetPort* chainPort)
  : m_state(state),
    m_txContext(txContext),
    m_revisionConfig(revisionConfig),
    m_vm(vm),
    m_blockHashes(std::move(blockHashes)),
    m_extension(extension),
    m_chainPort(chainPort),
    m_fixStorageStatus(fixStorageStatus)
{}

bool EthHost::account_exists(const address& addr) const noexcept
{
    return m_state.get_account(addr).has_value();
}

EthHost::bytes32 EthHost::get_storage(const address& addr, const bytes32& key) const noexcept
{
    return m_state.get_storage(addr, key);
}

evmc_storage_status EthHost::set_storage(
    const address& addr, const bytes32& key, const bytes32& value) noexcept
{
    auto const slot = std::make_pair(addr, key);
    auto [it, inserted] = m_storageOriginalValues.try_emplace(slot, evmc_bytes32{});
    if (inserted)
    {
        // FISCO sstoreStatus reads committed storage directly for status classification.
        it->second = m_state.get_storage(addr, key);
    }

    auto const& original = it->second;
    auto const current = m_state.get_storage(addr, key);
    if (Bytes32Equal{}(current, value))
    {
        // Unchanged slot: evmone keeps ASSIGNED (100 gas) even when original was zero.
        return EVMC_STORAGE_ASSIGNED;
    }
    if (m_fixStorageStatus)
    {
        applySstoreRefundEip3529(m_state, current, original, value);
    }

    m_state.set_storage(addr, key, value);
    auto const status = classifyStorageStatus(original, current, value, m_fixStorageStatus);
    if (!m_fixStorageStatus && status == EVMC_STORAGE_DELETED)
    {
        m_state.add_refund(SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529);
    }
    return status;
}

EthHost::uint256be EthHost::get_balance(const address& addr) const noexcept
{
    return toEvmC(m_state.get_balance(addr));
}

size_t EthHost::get_code_size(const address& addr) const noexcept
{
    return m_state.get_code(addr).size();
}

EthHost::bytes32 EthHost::get_code_hash(const address& addr) const noexcept
{
    return m_state.get_code_hash(addr);
}

size_t EthHost::copy_code(const address& addr, size_t code_offset, uint8_t* buffer_data,
    size_t buffer_size) const noexcept
{
    auto const code = m_state.get_code(addr);
    if (code_offset >= code.size() || buffer_size == 0)
    {
        return 0;
    }
    auto const count = std::min(buffer_size, code.size() - code_offset);
    std::copy_n(code.data() + code_offset, count, buffer_data);
    return count;
}

void EthHost::markCreatedInTx(evmc_address const& addr) noexcept
{
    if (!isZeroAddress(addr))
    {
        m_createdInTx.insert(addr);
    }
}

bool EthHost::wasCreatedInTx(evmc_address const& addr) const noexcept
{
    return m_createdInTx.contains(addr);
}

void EthHost::destroyContractState(evmc_address const& addr) noexcept
{
    m_state.set_code(addr, {}, {});
    m_state.clear_storage(addr);
}

bool EthHost::selfdestruct(const address& addr, const address& beneficiary) noexcept
{
    if (m_extension != nullptr)
    {
        auto const account = m_state.find(addr).value_or(Account{});
        if (!m_extension->allowSelfdestruct(account))
        {
            return false;
        }
    }

    auto const balance = m_state.get_balance(addr);
    auto const selfBeneficiary =
        std::memcmp(addr.bytes, beneficiary.bytes, sizeof(addr.bytes)) == 0;

    if (m_revisionConfig.eip6780 && !wasCreatedInTx(addr))
    {
        // EIP-6780: pre-existing accounts are not destroyable; move balance only.
        // evmone: acc.balance = 0; beneficiary += balance (net unchanged when self-beneficiary).
        if (balance != 0)
        {
            m_state.set_balance(addr, 0);
            if (selfBeneficiary)
            {
                m_state.set_balance(addr, balance);
            }
            else
            {
                m_state.set_balance(beneficiary, m_state.get_balance(beneficiary) + balance);
            }
        }
        return false;
    }

    if (balance != 0)
    {
        if (!selfBeneficiary)
        {
            bcos::evm::transfer(m_state, addr, beneficiary, balance);
        }
        else
        {
            m_state.set_balance(addr, 0);
        }
    }

    if (m_revisionConfig.eip6780)
    {
        m_state.mark_self_destructed(addr);
        return true;
    }

    destroyContractState(addr);
    return true;
}

EthHost::Result EthHost::call(const evmc_message& msg) noexcept
{
    if (msg.depth > 0)
    {
        EVM_LOG(TRACE) << LOG_DESC("EthHost::call") << LOG_KV("kind", trace::callKind(msg.kind))
                       << LOG_KV("depth", msg.depth) << LOG_KV("gas", msg.gas)
                       << LOG_KV("sender", trace::evmcAddress(msg.sender))
                       << LOG_KV("target", trace::evmcAddress(msg.recipient));
    }

    struct ExecutionAddressGuard
    {
        evmc_address& address;
        evmc_address saved;
        explicit ExecutionAddressGuard(evmc_address& address_) : address(address_), saved(address_)
        {}
        ~ExecutionAddressGuard() { address = saved; }
    };
    ExecutionAddressGuard guard{m_executionAddress};

    execution::FrameContext frameCtx{m_state, m_vm, m_revisionConfig, m_extension,
        m_txContext.tx_origin, m_executionAddress, false, m_chainPort};
    auto fr = execution::runExecutionFrame(frameCtx, msg, execution::FrameScope::Nested, *this);
    return Result(std::move(fr.result));
}

evmc_tx_context EthHost::get_tx_context() const noexcept
{
    return m_txContext;
}

EthHost::bytes32 EthHost::get_block_hash(int64_t number) const noexcept
{
    if (!m_blockHashes)
    {
        return {};
    }
    return m_blockHashes(number);
}

void EthHost::emit_log(const address& addr, const uint8_t* data, size_t data_size,
    const bytes32 topics[], size_t num_topics) noexcept
{
    LogEntry entry;
    entry.address = addr;
    if (data != nullptr && data_size > 0)
    {
        entry.data.assign(data, data + data_size);
    }
    entry.topics.reserve(num_topics);
    for (size_t i = 0; i < num_topics; ++i)
    {
        entry.topics.push_back(topics[i]);
    }
    m_logs.push_back(std::move(entry));
}

evmc_access_status EthHost::access_account(const address& addr) noexcept
{
    if (!execution::isEip2929Enabled(m_revisionConfig))
    {
        return EVMC_ACCESS_COLD;
    }
    return m_state.warm_up_address(addr) ? EVMC_ACCESS_COLD : EVMC_ACCESS_WARM;
}

evmc_access_status EthHost::access_storage(const address& addr, const bytes32& key) noexcept
{
    if (!execution::isEip2929Enabled(m_revisionConfig))
    {
        return EVMC_ACCESS_COLD;
    }
    return m_state.warm_up_storage(addr, key) ? EVMC_ACCESS_COLD : EVMC_ACCESS_WARM;
}

EthHost::bytes32 EthHost::get_transient_storage(
    const address& addr, const bytes32& key) const noexcept
{
    auto const account = m_state.find(addr);
    if (!account.has_value())
    {
        return {};
    }
    auto it = account->transientStorage.find(key);
    if (it == account->transientStorage.end())
    {
        return {};
    }
    return it->second;
}

void EthHost::set_transient_storage(
    const address& addr, const bytes32& key, const bytes32& value) noexcept
{
    m_state.set_transient_storage(addr, key, value);
}

std::vector<LogEntry> EthHost::take_logs()
{
    return std::exchange(m_logs, {});
}

evmc_storage_status EthHost::classifyStorageStatus(const evmc_bytes32& oldValue,
    const evmc_bytes32& currentValue, const evmc_bytes32& newValue, bool fixStorageStatus) noexcept
{
    if (!fixStorageStatus)
    {
        return isZeroBytes32(newValue) ? EVMC_STORAGE_DELETED : EVMC_STORAGE_MODIFIED;
    }

    // evmone test/state/host.cpp set_storage (EIP-2200 / EIP-3529 status mapping).
    auto const dirty = !Bytes32Equal{}(oldValue, currentValue);
    auto const restored = Bytes32Equal{}(oldValue, newValue);
    auto const currentIsZero = isZeroBytes32(currentValue);
    auto const valueIsZero = isZeroBytes32(newValue);

    auto status = EVMC_STORAGE_ASSIGNED;
    if (!dirty && !restored)
    {
        if (currentIsZero)
        {
            status = EVMC_STORAGE_ADDED;
        }
        else if (valueIsZero)
        {
            status = EVMC_STORAGE_DELETED;
        }
        else
        {
            status = EVMC_STORAGE_MODIFIED;
        }
    }
    else if (dirty && !restored)
    {
        if (currentIsZero && !valueIsZero)
        {
            status = EVMC_STORAGE_DELETED_ADDED;
        }
        else if (!currentIsZero && valueIsZero)
        {
            status = EVMC_STORAGE_MODIFIED_DELETED;
        }
    }
    else if (dirty && restored)
    {
        if (currentIsZero)
        {
            status = EVMC_STORAGE_DELETED_RESTORED;
        }
        else if (valueIsZero)
        {
            status = EVMC_STORAGE_ADDED_DELETED;
        }
        else
        {
            status = EVMC_STORAGE_MODIFIED_RESTORED;
        }
    }
    return status;
}
}  // namespace bcos::evm::state
