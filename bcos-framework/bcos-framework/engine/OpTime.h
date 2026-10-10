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
 * @file OpTime.h
 * @brief Convert FISCO internal-ms timestamps to OP fork-schedule Unix seconds.
 */
#pragma once

#include <cstdint>

namespace bcos::engine
{
/// FISCO block headers store timestamps in internal milliseconds; OP fork schedule activations
/// are Unix seconds. Convert exactly once at Engine/scheduler boundaries.
inline uint64_t unixSecondsFromInternalMillis(uint64_t ms)
{
    return ms / 1000;
}
}  // namespace bcos::engine
