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
 * @file PrecompiledManager.h
 */

#pragma once

#include "PrecompiledImpl.h"
#include "bcos-framework/ledger/Features.h"

namespace bcos::executor_v1
{

class PrecompiledManager
{
public:
    explicit PrecompiledManager(crypto::Hash::Ptr hashImpl);
    Precompiled const* getPrecompiled(unsigned long contractAddress) const;
    Precompiled const* getPrecompiled(const evmc_address& address) const;

    // FIB-84: feature-aware lookup - returns nullptr if precompiled's flag is disabled
    Precompiled const* getPrecompiled(
        unsigned long contractAddress, const ledger::Features& features) const;
    Precompiled const* getPrecompiled(
        const evmc_address& address, const ledger::Features& features) const;

private:
    crypto::Hash::Ptr m_hashImpl;
    std::vector<std::tuple<unsigned long, Precompiled>> m_address2Precompiled{};
};

}  // namespace bcos::executor_v1