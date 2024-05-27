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
#include <evmc/loader.h>
#include <evmone/evmone.h>
#include <boost/compute/detail/lru_cache.hpp>
#include <memory>
#include <shared_mutex>
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

class VMFactory
{
public:
    VMFactory(size_t cache_size = c_EVMONE_CACHE_SIZE) : m_cache(cache_size) {}

    /// Creates a VM instance of the kind provided.
    VMInstance create(VMKind _kind, evmc_revision revision, const crypto::HashType& codeHash,
        bytes_view code, bool isCreate = false);

    /// @brief Gets an anvanced EVM analysis from the cache. if not found return nullptr
    std::shared_ptr<evmoneCodeAnalysis> get(
        const crypto::HashType& key, evmc_revision revision) noexcept;

    void put(const crypto::HashType& key, const std::shared_ptr<evmoneCodeAnalysis>& analysis,
        evmc_revision revision) noexcept;

private:
    boost::compute::detail::lru_cache<crypto::HashType, std::shared_ptr<evmoneCodeAnalysis>>
        m_cache;
    evmc_revision m_revision = EVMC_PARIS;
    std::mutex m_cacheMutex;
};
}  // namespace bcos::executor
