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
};

}  // namespace bcos::executor
