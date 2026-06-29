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
 * @brief Block context used by eth transition().
 * @file BlockInfo.hpp
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <functional>

namespace bcos::evm::state
{
using BlockHashes = std::function<evmc_bytes32(int64_t)>;

struct BlockInfo
{
    int64_t number{0};
    int64_t timestamp{0};
    int64_t gasLimit{0};
    evmc_address coinbase{};
    evmc_bytes32 prevRandao{};
    bcos::u256 baseFee{0};
    bcos::u256 chainId{0};
    bcos::u256 blobBaseFee{0};
};
}  // namespace bcos::evm::state
