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
 * @brief Lightweight evmc host over eth::state with extension hooks.
 * @file EthHost.hpp
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-evm/eth/state/VmHostPolicy.h"
#include <evmc/evmc.hpp>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace bcos::evm
{
struct ChainCallTargetPort;
}

namespace bcos::evm::state
{
class EthHost : public evmc::Host
{
public:
    using address = evmc::address;
    using bytes32 = evmc::bytes32;
    using uint256be = evmc::uint256be;
    using Result = evmc::Result;

    EthHost(State& state, evmc_tx_context txContext,
        bcos::evm_standard::RevisionConfig revisionConfig, evmc::VM& vm, BlockHashes blockHashes,
        VmHostPolicy* extension = nullptr, bool fixStorageStatus = true,
        ChainCallTargetPort* chainPort = nullptr);

    bool account_exists(const address& addr) const noexcept final;
    bytes32 get_storage(const address& addr, const bytes32& key) const noexcept final;
    evmc_storage_status set_storage(
        const address& addr, const bytes32& key, const bytes32& value) noexcept final;
    uint256be get_balance(const address& addr) const noexcept final;
    size_t get_code_size(const address& addr) const noexcept final;
    bytes32 get_code_hash(const address& addr) const noexcept final;
    size_t copy_code(const address& addr, size_t code_offset, uint8_t* buffer_data,
        size_t buffer_size) const noexcept final;
    bool selfdestruct(const address& addr, const address& beneficiary) noexcept final;
    Result call(const evmc_message& msg) noexcept final;
    evmc_tx_context get_tx_context() const noexcept final;
    bytes32 get_block_hash(int64_t number) const noexcept final;
    void emit_log(const address& addr, const uint8_t* data, size_t data_size,
        const bytes32 topics[], size_t num_topics) noexcept final;
    evmc_access_status access_account(const address& addr) noexcept final;
    evmc_access_status access_storage(const address& addr, const bytes32& key) noexcept final;
    bytes32 get_transient_storage(const address& addr, const bytes32& key) const noexcept final;
    void set_transient_storage(
        const address& addr, const bytes32& key, const bytes32& value) noexcept final;
    void set_execution_address(const evmc_address& address) noexcept
    {
        m_executionAddress = address;
    }
    evmc_address& execution_address_ref() noexcept { return m_executionAddress; }
    void markCreatedInTx(evmc_address const& addr) noexcept;
    [[nodiscard]] bool wasCreatedInTx(evmc_address const& addr) const noexcept;
    std::vector<LogEntry> take_logs();

private:
    static evmc_storage_status classifyStorageStatus(const evmc_bytes32& oldValue,
        const evmc_bytes32& currentValue, const evmc_bytes32& newValue,
        bool fixStorageStatus) noexcept;

    void destroyContractState(evmc_address const& addr) noexcept;

private:
    State& m_state;
    evmc_tx_context m_txContext{};
    bcos::evm_standard::RevisionConfig m_revisionConfig{};
    evmc::VM& m_vm;
    BlockHashes m_blockHashes;
    VmHostPolicy* m_extension{nullptr};
    ChainCallTargetPort* m_chainPort{nullptr};
    std::unordered_map<std::pair<address, bytes32>, bytes32, WarmStorageKeyHash,
        WarmStorageKeyEqual>
        m_storageOriginalValues;
    bool m_fixStorageStatus{true};
    std::vector<LogEntry> m_logs;
    evmc_address m_executionAddress{};
    std::unordered_set<evmc_address, AddressHash, AddressEqual> m_createdInTx;
};
}  // namespace bcos::evm::state
