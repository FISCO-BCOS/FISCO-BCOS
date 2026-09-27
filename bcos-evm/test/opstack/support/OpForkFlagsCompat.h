/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 */

#pragma once

// Test-only compatibility wrapper: production selects forks via OpForkSchedule timestamps.
// Do not include from production translation units.

#include <bcos-evm/opstack/OpForkSchedule.h>

namespace bcos::evm::opstack
{
struct OpForkFlags
{
    bool jovianActive = false;
};

inline const OpForkConfig& configAt(const OpForkFlags& flags) noexcept
{
    return flags.jovianActive ? jovianConfig() : isthmusConfig();
}
}  // namespace bcos::evm::opstack
