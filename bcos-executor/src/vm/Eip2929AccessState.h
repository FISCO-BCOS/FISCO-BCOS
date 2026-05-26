/*
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @brief EIP-2929 per-transaction warm/cold access sets (shared across CALL depth).
 * @file Eip2929AccessState.h
 */

#pragma once

#include <evmc/evmc.h>
#include <boost/container_hash/hash.hpp>
#include <cstring>
#include <optional>
#include <unordered_set>
#include <utility>

namespace bcos::executor
{

struct Eip2929AddrHash
{
    size_t operator()(const evmc_address& a) const noexcept
    {
        return boost::hash_range(a.bytes, a.bytes + 20);
    }
};

struct Eip2929AddrEqual
{
    bool operator()(const evmc_address& a, const evmc_address& b) const noexcept
    {
        return std::memcmp(a.bytes, b.bytes, 20) == 0;
    }
};

struct Eip2929PairHash
{
    size_t operator()(const std::pair<evmc_address, evmc_bytes32>& p) const noexcept
    {
        size_t h = 0;
        boost::hash_combine(h, boost::hash_range(p.first.bytes, p.first.bytes + 20));
        boost::hash_combine(h, boost::hash_range(p.second.bytes, p.second.bytes + 32));
        return h;
    }
};

struct Eip2929PairEqual
{
    bool operator()(const std::pair<evmc_address, evmc_bytes32>& a,
        const std::pair<evmc_address, evmc_bytes32>& b) const noexcept
    {
        return std::memcmp(a.first.bytes, b.first.bytes, 20) == 0 &&
               std::memcmp(a.second.bytes, b.second.bytes, 32) == 0;
    }
};

/// Warm account/storage sets for one transaction (contextID). Shared by all HostContext depths.
struct Eip2929AccessState
{
    std::unordered_set<evmc_address, Eip2929AddrHash, Eip2929AddrEqual> warmAccounts;
    std::unordered_set<std::pair<evmc_address, evmc_bytes32>, Eip2929PairHash, Eip2929PairEqual>
        warmStorage;

    bool warmUpAddress(const evmc_address& address) { return warmAccounts.insert(address).second; }

    bool warmUpStorage(const evmc_address& address, const evmc_bytes32& key)
    {
        return warmStorage.insert({address, key}).second;
    }

    bool containsAddress(const evmc_address& address) const
    {
        return warmAccounts.contains(address);
    }

    bool containsStorage(const evmc_address& address, const evmc_bytes32& key) const
    {
        return warmStorage.contains({address, key});
    }

    /// Warm all precompiles active at @p revision (EIP-2929: "the set of all precompiles").
    void warmUpActivePrecompiles(evmc_revision revision)
    {
        static constexpr unsigned precompile_hi = sizeof(evmc_address) - 1;
        for (uint8_t i = 1; i <= 9; ++i)
        {
            evmc_address p{};
            p.bytes[precompile_hi] = i;
            (void)warmUpAddress(p);
        }
        if (revision >= EVMC_CANCUN)
        {
            evmc_address p{};
            p.bytes[precompile_hi] = 0x0a;
            (void)warmUpAddress(p);
        }
        if (revision >= EVMC_PRAGUE)
        {
            for (uint8_t i = 0x0b; i <= 0x11; ++i)
            {
                evmc_address p{};
                p.bytes[precompile_hi] = i;
                (void)warmUpAddress(p);
            }
        }
        if (revision >= EVMC_OSAKA)
        {
            evmc_address p{};
            p.bytes[18] = 0x01;
            p.bytes[19] = 0x00;
            (void)warmUpAddress(p);
        }
    }

    /// EIP-2929 transaction-entry warm accesses: caller (origin/sender),
    /// optional recipient (omit for CREATE/CREATE2), and active precompiles at @p revision.
    void warmUpInitialTxSet(const evmc_address& origin,
        std::optional<evmc_address> transactionToEVMC, evmc_revision revision)
    {
        (void)warmUpAddress(origin);
        if (transactionToEVMC.has_value())
        {
            (void)warmUpAddress(*transactionToEVMC);
        }
        warmUpActivePrecompiles(revision);
    }

    /// EIP-2930: warm all accounts and storage slots from a typed-transaction access list.
    /// AddrConverter: std::string const& -> evmc_address (e.g. unhexAddress).
    template <typename AccessList, typename AddrConverter>
    void warmUpAccessList(AccessList const& list, AddrConverter&& toAddr)
    {
        for (auto const& [addrHex, keys] : list)
        {
            auto const addr = toAddr(addrHex);
            (void)warmUpAddress(addr);
            for (auto const& k : keys)
            {
                evmc_bytes32 key{};
                static_assert(sizeof(key.bytes) == 32);
                std::memcpy(key.bytes, k.data(), sizeof(key.bytes));
                (void)warmUpStorage(addr, key);
            }
        }
    }
};

}  // namespace bcos::executor
