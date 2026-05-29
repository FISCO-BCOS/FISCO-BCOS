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
 * @brief FIB-134 digest helper that binds packetType into the PBFT signature
 *        digest, gated by the message-carried protocol version (BaseMessage /
 *        RawMessage `version` proto field). v=0 preserves the legacy formula
 *        `hash(payload)`; v>=1 binds packetType as `hash(packetType_byte || payload)`.
 *        Used at both the inner (PBFTMessage::getHashFieldsDataHash) and outer
 *        (PBFTCodec::encode/decode for ViewChange/NewView) signing paths — both
 *        paths feed the same bytes-with-bound-packetType formula.
 * @file PacketTypeDigest.h
 */
#pragma once

#include "PBFTMsgVersion.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Common.h>
#include <cstdint>

namespace bcos::consensus
{
// FIB-134: compute the digest a PBFT signature is taken over, branching on the
// message-carried wire version. v=0 is the legacy formula `hash(data)`; v>=1
// binds packetType into the digest as `hash(packetType_byte || data)` so that
// rewriting the outer RawMessage.type invalidates the signature.
//
// `data` is the inner BaseMessage's hashFieldsData (inner signing path) or the
// outer wrapper payload (ViewChange/NewView outer signing path); the formula is
// identical for both.
struct PacketTypeDigest
{
    static bcos::crypto::HashType compute(int32_t version, int32_t packetType,
        bcos::bytesConstRef data, bcos::crypto::Hash::Ptr const& hashImpl)
    {
        if (!digestBindsPacketType(version))
        {
            return hashImpl->hash(data);
        }
        bcos::bytes buffer;
        buffer.reserve(1 + data.size());
        buffer.push_back(static_cast<bcos::byte>(packetType));
        buffer.insert(buffer.end(), data.begin(), data.end());
        return hashImpl->hash(bcos::bytesConstRef(buffer.data(), buffer.size()));
    }
};
}  // namespace bcos::consensus
