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
 * @brief EIP-2929 active precompile address enumeration by fork revision.
 * @file Eip2929PrecompileWarm.h
 */

#pragma once

#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::executor
{

/// Invoke @p consume for each precompile address active at @p revision (EIP-2929 initial set).
template <typename AddressConsumer>
void forEachActivePrecompileAddress(evmc_revision revision, AddressConsumer&& consume)
{
    static constexpr unsigned precompileHi = sizeof(evmc_address) - 1;
    for (uint8_t i = 1; i <= 9; ++i)
    {
        evmc_address precompile{};
        precompile.bytes[precompileHi] = i;
        consume(precompile);
    }
    if (revision >= EVMC_CANCUN)
    {
        evmc_address precompile{};
        precompile.bytes[precompileHi] = 0x0a;
        consume(precompile);
    }
    if (revision >= EVMC_PRAGUE)
    {
        for (uint8_t i = 0x0b; i <= 0x11; ++i)
        {
            evmc_address precompile{};
            precompile.bytes[precompileHi] = i;
            consume(precompile);
        }
    }
    if (revision >= EVMC_OSAKA)
    {
        evmc_address precompile{};
        precompile.bytes[18] = 0x01;
        precompile.bytes[19] = 0x00;
        consume(precompile);
    }
}

}  // namespace bcos::executor
