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
 *        Used at both inner (PBFTMessage::getHashFieldsDataHash) and outer
 *        (PBFTCodec::encode/decode for ViewChange/NewView) signing paths.
 * @file PacketTypeDigest.h
 */
#pragma once

#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Common.h>
#include <cstdint>

namespace bcos::consensus
{
// Must stay <= PBFTConfig::c_pbftMsgDefaultVersion (sender default in PBFTConfig.h).
// Bumping the receiver threshold without bumping the sender default would silently
// break wire round-trip; both constants encode the same FIB-134 hardfork boundary.
constexpr unsigned c_pbftMsgVersion_PacketTypeBound = 1;

// FIB-134: helper that returns the digest of a PBFT message-bytes payload,
// branching by the message-carried protocol version. v=0 is the legacy formula
// `hash(payload)`; v>=1 binds packetType into the digest as
// `hash(packetType_byte || payload)`.
class PacketTypeDigest
{
public:
    static bcos::crypto::HashType inner(int32_t version, int32_t packetType,
        bcos::bytesConstRef hashFieldsData, bcos::crypto::Hash::Ptr const& hashImpl)
    {
        if (version >= static_cast<int32_t>(c_pbftMsgVersion_PacketTypeBound))
        {
            bcos::bytes buffer;
            buffer.reserve(1 + hashFieldsData.size());
            buffer.push_back(static_cast<bcos::byte>(packetType));
            buffer.insert(buffer.end(), hashFieldsData.begin(), hashFieldsData.end());
            return hashImpl->hash(bcos::bytesConstRef(buffer.data(), buffer.size()));
        }
        return hashImpl->hash(hashFieldsData);
    }

    static bcos::crypto::HashType outer(int32_t version, int32_t packetType,
        bcos::bytesConstRef payload, bcos::crypto::Hash::Ptr const& hashImpl)
    {
        if (version >= static_cast<int32_t>(c_pbftMsgVersion_PacketTypeBound))
        {
            bcos::bytes buffer;
            buffer.reserve(1 + payload.size());
            buffer.push_back(static_cast<bcos::byte>(packetType));
            buffer.insert(buffer.end(), payload.begin(), payload.end());
            return hashImpl->hash(bcos::bytesConstRef(buffer.data(), buffer.size()));
        }
        return hashImpl->hash(payload);
    }
};
}  // namespace bcos::consensus
