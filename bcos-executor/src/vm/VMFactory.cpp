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
#ifdef WITH_WASM
#include <BCOS_WASM.h>
#endif
#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace bcos::executor
{

/// The pointer to VMInstance create function in DLL VMInstance VM.
///
/// This variable is only written once when processing command line arguments,
/// so access is thread-safe.

// evmc_create_fn g_evmcCreateFn;

VMInstance VMFactory::create(VMKind kind, evmc_revision revision, const crypto::HashType& codeHash,
    bytes_view code, bool isCreate)
{
    switch (kind)
    {
#ifdef WITH_WASM
    case VMKind::BcosWasm:
        return VMInstance{evmc_create_bcoswasm(), revision, code};
#endif
    // case VMKind::DLL:
    //     return VMInstance{g_evmcCreateFn()};
    case VMKind::evmone:
    default:
    {
        if (isCreate)
        {
            return VMInstance{evmc_create_evmone(), revision, code};
        }
        std::shared_ptr<evmoneCodeAnalysis> analysis{get(codeHash, revision)};
        if (!analysis)
        {
            analysis =
                std::make_shared<evmoneCodeAnalysis>(evmone::advanced::analyze(revision, code));
            // analysis = std::make_shared<evmoneCodeAnalysis>(
            //     evmone::baseline::analyze(revision, code));
            put(codeHash, analysis, revision);
        }
        return VMInstance{analysis, revision, code};
    }
    }
}

std::shared_ptr<evmoneCodeAnalysis> VMFactory::get(
    const crypto::HashType& key, evmc_revision revision) noexcept
{
    if (revision == m_revision)
    {
        ReadGuard guard(m_cacheMutex);
        auto analysis = m_cache.find(key);
        guard.unlock();
        if (analysis != m_cache.end())
        {
            return analysis->second;
        }
    }
    return nullptr;
}

void VMFactory::put(const crypto::HashType& key,
    const std::shared_ptr<evmoneCodeAnalysis>& analysis, evmc_revision revision) noexcept
{
    WriteGuard guard(m_cacheMutex);
    if (revision != m_revision)
    {
        m_cache.clear();
        m_revision = revision;
    }

    m_cache.emplace(key, analysis);
}

}  // namespace bcos::executor
