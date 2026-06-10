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

#include "Eip2929PrecompileWarm.h"
#include <evmc/evmc.h>
#include <boost/container_hash/hash.hpp>
#include <cstring>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

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

struct Eip2929Checkpoint
{
    std::vector<evmc_address> newAccounts;
    std::vector<std::pair<evmc_address, evmc_bytes32>> newStorage;
    /// EIP-2929: CREATE/CREATE2 contract addresses that stay warm across scope rollback.
    std::vector<evmc_address> pinnedCreateAddresses;
};

/// Warm account/storage sets for one transaction (contextID). Shared by all HostContext depths.
struct Eip2929AccessState
{
    std::vector<Eip2929Checkpoint> m_checkpointStack;

    std::unordered_set<evmc_address, Eip2929AddrHash, Eip2929AddrEqual> warmAccounts;
    std::unordered_set<std::pair<evmc_address, evmc_bytes32>, Eip2929PairHash, Eip2929PairEqual>
        warmStorage;

    void pushCheckpoint() { m_checkpointStack.emplace_back(); }

    bool hasActiveCheckpoint() const noexcept { return !m_checkpointStack.empty(); }

    /// Pin contract address for CREATE/CREATE2 child scope rollback (EIP-2929 clarification).
    void setCreateRollbackPin(evmc_address const& address)
    {
        if (m_checkpointStack.empty())
        {
            return;
        }
        auto& cp = m_checkpointStack.back();
        recordPinnedCreateAddress(cp, address);
        if (warmAccounts.insert(address).second)
        {
            cp.newAccounts.push_back(address);
        }
    }

    void rollbackCheckpoint()
    {
        if (m_checkpointStack.empty())
        {
            return;
        }
        auto& cp = m_checkpointStack.back();
        for (auto const& [addr, key] : cp.newStorage)
        {
            warmStorage.erase({addr, key});
        }
        for (auto const& addr : cp.newAccounts)
        {
            if (isPinnedCreateAddress(cp, addr))
            {
                continue;
            }
            warmAccounts.erase(addr);
        }
        m_checkpointStack.pop_back();
    }

    void commitCheckpoint()
    {
        if (m_checkpointStack.empty())
        {
            return;
        }
        if (m_checkpointStack.size() >= 2)
        {
            auto top = std::move(m_checkpointStack.back());
            m_checkpointStack.pop_back();
            auto& parent = m_checkpointStack.back();
            parent.newAccounts.insert(parent.newAccounts.end(),
                std::make_move_iterator(top.newAccounts.begin()),
                std::make_move_iterator(top.newAccounts.end()));
            parent.newStorage.insert(parent.newStorage.end(),
                std::make_move_iterator(top.newStorage.begin()),
                std::make_move_iterator(top.newStorage.end()));
            for (auto const& pinned : top.pinnedCreateAddresses)
            {
                recordPinnedCreateAddress(parent, pinned);
            }
        }
        else
        {
            m_checkpointStack.pop_back();
        }
    }

    bool warmUpAddress(const evmc_address& address)
    {
        return warmUpAddressImpl(address, /*recordJournal=*/true);
    }

    /// Warm without checkpoint journal (tx-entry warmth: CREATE pin, access list).
    bool warmUpAddressNoJournal(const evmc_address& address)
    {
        return warmUpAddressImpl(address, /*recordJournal=*/false);
    }

    bool warmUpStorage(const evmc_address& address, const evmc_bytes32& key)
    {
        return warmUpStorageImpl(address, key, /*recordJournal=*/true);
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
        forEachActivePrecompileAddress(revision,
            [this](evmc_address const& precompile) { (void)warmUpAddressImpl(precompile, false); });
    }

    /// EIP-2929 transaction-entry warm accesses: caller (origin/sender),
    /// optional recipient (omit for CREATE/CREATE2), and active precompiles at @p revision.
    void warmUpInitialTxSet(const evmc_address& origin,
        std::optional<evmc_address> transactionToEVMC, evmc_revision revision)
    {
        (void)warmUpAddressImpl(origin, false);
        if (transactionToEVMC.has_value())
        {
            (void)warmUpAddressImpl(*transactionToEVMC, false);
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
            (void)warmUpAddressImpl(addr, false);
            for (auto const& k : keys)
            {
                evmc_bytes32 key{};
                static_assert(sizeof(key.bytes) == 32);
                std::memcpy(key.bytes, k.data(), sizeof(key.bytes));
                (void)warmUpStorageImpl(addr, key, false);
            }
        }
    }

private:
    static bool addressesEqual(evmc_address const& a, evmc_address const& b) noexcept
    {
        return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
    }

    static bool isPinnedCreateAddress(
        Eip2929Checkpoint const& checkpoint, evmc_address const& address) noexcept
    {
        for (auto const& pinned : checkpoint.pinnedCreateAddresses)
        {
            if (addressesEqual(pinned, address))
            {
                return true;
            }
        }
        return false;
    }

    static void recordPinnedCreateAddress(
        Eip2929Checkpoint& checkpoint, evmc_address const& address)
    {
        for (auto const& pinned : checkpoint.pinnedCreateAddresses)
        {
            if (addressesEqual(pinned, address))
            {
                return;
            }
        }
        checkpoint.pinnedCreateAddresses.push_back(address);
    }

    bool warmUpAddressImpl(const evmc_address& address, bool recordJournal)
    {
        auto const inserted = warmAccounts.insert(address).second;
        if (recordJournal && inserted && !m_checkpointStack.empty())
        {
            m_checkpointStack.back().newAccounts.push_back(address);
        }
        return inserted;
    }

    bool warmUpStorageImpl(const evmc_address& address, const evmc_bytes32& key, bool recordJournal)
    {
        auto const inserted = warmStorage.insert({address, key}).second;
        if (recordJournal && inserted && !m_checkpointStack.empty())
        {
            m_checkpointStack.back().newStorage.emplace_back(address, key);
        }
        return inserted;
    }
};

}  // namespace bcos::executor
