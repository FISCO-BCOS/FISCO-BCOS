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
 * @brief In-memory account model for execution-time State journal.
 * @file Account.hpp
 */

#pragma once

#include "bcos-evm/eth/state/HashUtils.hpp"
#include <unordered_map>

namespace bcos::evm::state
{
using StorageMap = std::unordered_map<evmc_bytes32, evmc_bytes32, Bytes32Hash, Bytes32Equal>;

struct Account
{
    bcos::u256 balance{0};
    uint64_t nonce{0};
    bcos::bytes code;
    evmc_bytes32 codeHash{};
    StorageMap storage;
    StorageMap transientStorage;
    bool balanceDirty{false};
    bool nonceDirty{false};
    bool codeDirty{false};
    bool selfDestructed{false};
};
}  // namespace bcos::evm::state
