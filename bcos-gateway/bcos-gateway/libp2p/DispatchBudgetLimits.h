/*
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

#include <cstddef>

namespace bcos::gateway
{
// Charges use Message::ownedStorageBytes(). On the take-buffer path this includes the whole grown
// receive buffer, roughly twice the frame size: a 32MiB frame charges about 64MiB, so the 96MiB
// peer cap admits one such message and the 768MiB global cap about twelve node-wide.
// Per-peer caps are smaller than the global cap in both dimensions, so one peer cannot exhaust
// either global dimension. Defaults require eight peers by bytes or twelve by count to saturate
// the global budget. At genuine saturation, a receive loop waiting for a global permit also pauses
// later packet types on its session until a permit is released or disconnect/stop cancels the wait.
struct DispatchBudgetLimits
{
    constexpr static std::size_t DEFAULT_GLOBAL_IN_FLIGHT_BYTES = 768 * 1024 * 1024;
    constexpr static std::size_t DEFAULT_GLOBAL_IN_FLIGHT_COUNT = 3072;
    constexpr static std::size_t DEFAULT_PEER_IN_FLIGHT_BYTES = 96 * 1024 * 1024;
    constexpr static std::size_t DEFAULT_PEER_IN_FLIGHT_COUNT = 256;

    std::size_t globalBytes = DEFAULT_GLOBAL_IN_FLIGHT_BYTES;
    std::size_t globalCount = DEFAULT_GLOBAL_IN_FLIGHT_COUNT;
    std::size_t peerBytes = DEFAULT_PEER_IN_FLIGHT_BYTES;
    std::size_t peerCount = DEFAULT_PEER_IN_FLIGHT_COUNT;
};
}  // namespace bcos::gateway
