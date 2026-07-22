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
 * @brief Lightweight transaction/request model for execution entry points.
 * @file Transaction.hpp
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <optional>
#include <vector>

namespace bcos::evm::state
{
struct Transaction
{
    evmc_address from{};
    std::optional<evmc_address> to;
    bcos::bytes data;
    bcos::u256 value{0};
    bcos::u256 gasPrice{0};
    int64_t gasLimit{0};
    uint64_t nonce{0};
};

struct TransactionProperties
{
    bool isStatic{false};
    bool warmCoinbase{true};
    bool warmDestination{true};
};

struct LogEntry
{
    evmc_address address{};
    bcos::bytes data;
    std::vector<evmc_bytes32> topics;
};
}  // namespace bcos::evm::state
