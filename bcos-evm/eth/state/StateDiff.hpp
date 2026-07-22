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
 * @brief State mutation set to be applied after successful execution.
 * @file StateDiff.hpp
 */

#pragma once

#include "bcos-evm/eth/state/Account.hpp"
#include <unordered_map>

namespace bcos::evm::state
{
struct StateDiff
{
    std::unordered_map<evmc_address, Account, AddressHash, AddressEqual> accounts;

    [[nodiscard]] bool empty() const noexcept { return accounts.empty(); }
};
}  // namespace bcos::evm::state
