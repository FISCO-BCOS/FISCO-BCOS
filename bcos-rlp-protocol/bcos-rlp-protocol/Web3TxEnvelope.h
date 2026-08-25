/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may use this file except in compliance with the License.
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
 * @file Web3TxEnvelope.h
 * @brief Signed-envelope walkers for Web3 transactions (chainId / typed-vs-legacy)
 * @date 2026/8/21
 */
#pragma once

#include "bcos-utilities/Common.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <optional>

namespace bcos::rlp::protocol
{
/// True if the envelope's first byte is a typed-transaction marker (EIP-2718: type byte < 0x80).
/// Used to key typed/legacy decisions on the envelope rather than the forgeable mirror kind.
[[nodiscard]] inline bool isTypedWeb3Envelope(bcos::bytesConstRef payload) noexcept
{
    return !payload.empty() && payload[0] > 0 && payload[0] < bcos::codec::rlp::BYTES_HEAD_BASE;
}

/// Chain id from a Web3 transaction's SIGNED envelope (extraTransactionBytes), never the
/// unauthenticated tars mirror. Inline so opstack-executor can use it without linking
/// rlp-protocol (that target pulls bcos-crypto → wedprcrypto and breaks libc++ typed catch).
///   typed (first byte < 0x80, not 0x7E): chainId = RLP field 0 of the inner list;
///   legacy: walk the first 6 fields; if a 7th is present it is the EIP-155 chainId or v.
/// nullopt = pre-EIP-155 unprotected legacy (6-field, v=27/28) or a malformed preimage.
[[nodiscard]] inline std::optional<uint64_t> web3ChainIdFromEnvelope(bcos::bytesConstRef payload)
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
    if (walker.empty())
    {
        return std::nullopt;
    }
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
