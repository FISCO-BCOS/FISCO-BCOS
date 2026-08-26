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
 * @file Web3TxEnvelope.cpp
 * @brief Library TU for web3ChainIdFromEnvelope. Kept out of the public header so every
 *        consumer compiles one copy (link-period identity) rather than one copy per include.
 */
#include "Web3TxEnvelope.h"
#include <bcos-codec/rlp/RLPDecode.h>

namespace bcos::rlp::protocol
{
std::optional<uint64_t> web3ChainIdFromEnvelope(bcos::bytesConstRef payload)
{
    if (payload.empty()) [[unlikely]]
    {
        return std::nullopt;
    }
    auto const firstByte = payload[0];
    bcos::bytesRef cursor(const_cast<bcos::byte*>(payload.data()), payload.size());
    // 0x7E (Deposit) has no chainId field — field 0 is sourceHash (h256).
    if (firstByte == 0x7E) [[unlikely]]
    {
        return std::nullopt;
    }
    if (firstByte > 0 && firstByte < bcos::codec::rlp::BYTES_HEAD_BASE)
    {
        cursor = cursor.getCroppedData(1);
        auto&& [error, header] = bcos::codec::rlp::decodeHeader(cursor);
        if (error || !header.isList || header.payloadLength > cursor.size()) [[unlikely]]
        {
            return std::nullopt;
        }
        uint64_t chainId = 0;
        if (auto e = bcos::codec::rlp::decode(cursor, chainId); e != nullptr) [[unlikely]]
        {
            return std::nullopt;
        }
        return chainId;
    }
    auto&& [error, header] = bcos::codec::rlp::decodeHeader(cursor);
    if (error || !header.isList || header.payloadLength > cursor.size()) [[unlikely]]
    {
        return std::nullopt;
    }
    bcos::bytesRef walker(cursor.data(), header.payloadLength);
    for (int i = 0; i < 6; ++i)
    {
        auto [fieldError, fieldHeader] = bcos::codec::rlp::decodeHeader(walker);
        if (fieldError || fieldHeader.payloadLength > walker.size()) [[unlikely]]
        {
            return std::nullopt;
        }
        walker = walker.getCroppedData(fieldHeader.payloadLength);
    }
    // Legacy dual layout: the tail after the 6 fields is either the preimage's (chainId, 0, 0)
    // — the txpool-stage signing preimage stored by takeToTarsTransaction — or the full signed
    // envelope's (v, r, s) on the sealed-block path. RLP encodes the integer 0 as an empty
    // payload while secp256k1 r/s never are, so emptiness of fields 8/9 discriminates the two
    // shapes unambiguously. Only field 8 is checked here; field 9 validation is deferred to
    // reassembleWeb3RawTransaction (which validates the full 0,0 tail).
    if (walker.empty())
    {
        return std::nullopt;
    }
    // field7Item keeps the WHOLE field-7 item (header + payload): decodeHeader advances the
    // walker to the payload start, and decoding that payload as a fresh item mis-reads any
    // multi-byte chainId/v (e.g. chainId 8453 -> 33) or classifies it as a list header
    // (chainId 200 -> nullopt -> bogus "unprotected" exemption). Decode from the item start.
    bcos::bytesRef field7Item = walker;
    auto [field7Error, field7Header] = bcos::codec::rlp::decodeHeader(walker);
    if (field7Error || field7Header.payloadLength > walker.size()) [[unlikely]]
    {
        return std::nullopt;
    }
    bcos::bytesRef afterField7 = walker.getCroppedData(field7Header.payloadLength);
    bool const isPreimageTail = [&] {
        if (afterField7.empty()) [[unlikely]]
        {
            return false;
        }
        auto [field8Error, field8Header] = bcos::codec::rlp::decodeHeader(afterField7);
        return field8Error == nullptr && !field8Header.isList && field8Header.payloadLength == 0;
    }();
    if (isPreimageTail)
    {
        uint64_t chainId = 0;
        if (auto e = bcos::codec::rlp::decode(field7Item, chainId); e != nullptr) [[unlikely]]
        {
            return std::nullopt;
        }
        return chainId;
    }
    uint64_t v = 0;
    if (auto e = bcos::codec::rlp::decode(field7Item, v); e != nullptr) [[unlikely]]
    {
        return std::nullopt;
    }
    if (v == 27 || v == 28)
    {
        return std::nullopt;
    }
    if (v >= 35)
    {
        return (v - 35) >> 1;
    }
    return std::nullopt;
}
}  // namespace bcos::rlp::protocol
