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
 * @brief factory of vm
 * @file VMFactory.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once
#include "../Common.h"
#include "VMInstance.h"
#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include <evmone/evmone.h>
#include <boost/compute/detail/lru_cache.hpp>
#include <boost/functional/hash.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace bcos::executor
{
size_t const c_EVMONE_CACHE_SIZE = 1024;

class VMInstance;
enum class VMKind
{
    evmone,
    BcosWasm,
    DLL
};

/// Cache key pairs runtime code hash with the EVMC revision used at execution time.
struct EvmCodeCacheKey
{
    crypto::HashType codeHash;
    evmc_revision revision{EVMC_LONDON};

    bool operator==(EvmCodeCacheKey const& other) const noexcept
    {
        return codeHash == other.codeHash && revision == other.revision;
    }

    bool operator<(EvmCodeCacheKey const& other) const noexcept
    {
        if (codeHash != other.codeHash)
        {
            return codeHash < other.codeHash;
        }
        return revision < other.revision;
    }
};

class VMFactory
{
public:
    VMFactory(size_t cache_size = c_EVMONE_CACHE_SIZE);

    /// Creates a VM instance of the kind provided.
    /// Pass crypto::HashType{} as codeHash to skip the analysis cache (e.g. for CREATE init code).
    VMInstance create(VMKind _kind, evmc_revision revision, const crypto::HashType& codeHash,
        bytes_view code, bool isCreate = false);

    /// @brief Gets baseline analysis from the cache, or nullptr if missing / revision mismatch.
    std::shared_ptr<evmoneCodeAnalysis> get(EvmCodeCacheKey const& key) noexcept;

    void put(
        EvmCodeCacheKey const& key, const std::shared_ptr<evmoneCodeAnalysis>& analysis) noexcept;

private:
    boost::compute::detail::lru_cache<EvmCodeCacheKey, std::shared_ptr<evmoneCodeAnalysis>> m_cache;
    std::mutex m_cacheMutex;
};
}  // namespace bcos::executor

namespace std
{
template <>
struct hash<bcos::executor::EvmCodeCacheKey>
{
    size_t operator()(bcos::executor::EvmCodeCacheKey const& key) const noexcept
    {
        size_t seed = std::hash<bcos::crypto::HashType>{}(key.codeHash);
        boost::hash_combine(seed, static_cast<int>(key.revision));
        return seed;
    }
};
}  // namespace std
