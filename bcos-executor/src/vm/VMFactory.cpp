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
 * @file VMFactory.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */


#include "VMFactory.h"
#include "VMInstance.h"
#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace bcos::executor
{
VMFactory::VMFactory(size_t cache_size) : m_cache(cache_size) {}

VMInstance VMFactory::create(VMKind kind, evmc_revision revision, const crypto::HashType& codeHash,
    bytes_view code, bool isCreate)
{
    switch (kind)
    {
    case VMKind::evmone:
    default:
    {
        if (isCreate)
        {
            // CREATE: init code + code deposit — EVMC execute(), no baseline analysis or cache.
            return VMInstance{evmc_create_evmone(), revision, code};
        }

        // CALL: baseline analyze + LRU cache + baseline::execute (pre-0.21 behavior).
        bool useCache = (codeHash != crypto::HashType{});
        EvmCodeCacheKey cacheKey{codeHash, revision};
        std::shared_ptr<EvmoneCodeAnalysis> analysis{useCache ? get(cacheKey) : nullptr};
        if (!analysis)
        {
            analysis = std::make_shared<EvmoneCodeAnalysis>(
                evmone::baseline::analyze(evmone::bytes_view(code.data(), code.size())));
            if (useCache)
            {
                put(cacheKey, analysis);
            }
        }
        return VMInstance{analysis, revision, code};
    }
    }
}

std::shared_ptr<EvmoneCodeAnalysis> VMFactory::get(EvmCodeCacheKey const& key) noexcept
{
    std::unique_lock lock(m_cacheMutex);
    auto analysis = m_cache.get(key);
    if (analysis)
    {
        return analysis.value();
    }
    return nullptr;
}

void VMFactory::put(
    EvmCodeCacheKey const& key, const std::shared_ptr<EvmoneCodeAnalysis>& analysis) noexcept
{
    std::unique_lock lock(m_cacheMutex);
    m_cache.insert(key, analysis);
}

}  // namespace bcos::executor
