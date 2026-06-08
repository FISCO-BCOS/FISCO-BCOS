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
 * @brief Protocol version carried in the PBFT wire `version` field
 *        (RawMessage.version / BaseMessage.version). The version selects the
 *        signature-digest formula (FIB-134), so it is the single knob that both
 *        the sender default and the codec encode default read from.
 * @file PBFTMsgVersion.h
 */
#pragma once

#include <cstdint>

namespace bcos::consensus
{
// Wire protocol version for PBFT messages. The numeric values are serialized
// into RawMessage.version / BaseMessage.version, so they must never be reused or
// reordered — only appended.
enum class PBFTMsgVersion : int32_t
{
    // Legacy digest formula: hash(payload). packetType is NOT authenticated.
    Base = 0,
    // FIB-134 digest formula: hash(packetType || payload). Binds the outer
    // packetType into the signature so a tampered wire type fails verification.
    PacketTypeBound = 1,
};

// The version every up-to-date node emits. Single source of truth for the
// sender-side default (PBFTConfig::c_pbftMsgDefaultVersion) and the codec
// encode() default. Bumping this is a hardfork: see FIB-134 / PR #5180.
constexpr PBFTMsgVersion c_currentPBFTMsgVersion = PBFTMsgVersion::PacketTypeBound;

constexpr int32_t toWireVersion(PBFTMsgVersion _version) noexcept
{
    return static_cast<int32_t>(_version);
}

// True when the given wire version binds packetType into the signature digest.
constexpr bool digestBindsPacketType(int32_t _wireVersion) noexcept
{
    return _wireVersion >= toWireVersion(PBFTMsgVersion::PacketTypeBound);
}
}  // namespace bcos::consensus
