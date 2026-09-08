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
 *
 * @file NumericBounds.h
 * @brief Shared numeric range predicates for engine validation and OP execution.
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <cstdint>
#include <limits>

namespace bcos::engine
{

/// Shared u256→uint64 ceiling. Engine validation returns optional; the OP
/// executor throws OpConsensusError — both must use this one comparison (A9-1).
[[nodiscard]] inline bool u256FitsUint64(u256 const& value)
{
    static u256 const c_maxU64(std::numeric_limits<std::uint64_t>::max());
    return value <= c_maxU64;
}

}  // namespace bcos::engine
