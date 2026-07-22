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
 * @brief Cold-state read abstraction consumed by execution State.
 * @file EvmStateReader.hpp
 */

#pragma once

#include "bcos-evm/eth/state/Account.hpp"
#include <optional>

namespace bcos::evm::state
{
class EvmStateReader
{
public:
    virtual ~EvmStateReader() = default;

    virtual std::optional<Account> get_account(const evmc_address& address) const = 0;

    [[nodiscard]] virtual bcos::u256 get_balance(const evmc_address& address) const
    {
        auto const account = get_account(address);
        return account.has_value() ? account->balance : bcos::u256{0};
    }

    [[nodiscard]] virtual uint64_t get_nonce(const evmc_address& address) const
    {
        auto const account = get_account(address);
        return account.has_value() ? account->nonce : 0;
    }

    [[nodiscard]] virtual bcos::bytes get_code(const evmc_address& address) const
    {
        auto const account = get_account(address);
        return account.has_value() ? account->code : bcos::bytes{};
    }

    [[nodiscard]] virtual evmc_bytes32 get_code_hash(const evmc_address& address) const
    {
        auto const account = get_account(address);
        return account.has_value() ? account->codeHash : evmc_bytes32{};
    }

    [[nodiscard]] virtual evmc_bytes32 get_storage(
        const evmc_address& address, const evmc_bytes32& key) const
    {
        auto const account = get_account(address);
        if (!account.has_value())
        {
            return {};
        }
        auto const it = account->storage.find(key);
        if (it == account->storage.end())
        {
            return {};
        }
        return it->second;
    }
};
}  // namespace bcos::evm::state
