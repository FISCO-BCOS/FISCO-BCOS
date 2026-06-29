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
 * @brief Hash and conversion helpers for evmc address/bytes32 keys.
 * @file HashUtils.hpp
 */

#pragma once

#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <evmc/evmc.h>
#include <boost/container_hash/hash.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <evmone_precompiles/keccak.hpp>
#include <string_view>

namespace bcos::evm::state
{
struct AddressHash
{
    size_t operator()(evmc_address const& address) const noexcept
    {
        return boost::hash_range(address.bytes, address.bytes + sizeof(address.bytes));
    }
};

struct AddressEqual
{
    bool operator()(evmc_address const& lhs, evmc_address const& rhs) const noexcept
    {
        return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
    }
};

struct Bytes32Hash
{
    size_t operator()(evmc_bytes32 const& value) const noexcept
    {
        return boost::hash_range(value.bytes, value.bytes + sizeof(value.bytes));
    }
};

struct Bytes32Equal
{
    bool operator()(evmc_bytes32 const& lhs, evmc_bytes32 const& rhs) const noexcept
    {
        return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
    }
};

inline bool isZeroAddress(const evmc_address& address) noexcept
{
    return std::all_of(std::begin(address.bytes), std::end(address.bytes),
        [](uint8_t value) { return value == 0; });
}

inline bool isZeroBytes32(const evmc_bytes32& value) noexcept
{
    return std::all_of(
        std::begin(value.bytes), std::end(value.bytes), [](uint8_t item) { return item == 0; });
}

inline evmc_bytes32 emptyCodeHash() noexcept
{
    evmc_bytes32 out{};
    out.bytes[0] = 0xc5;
    out.bytes[1] = 0xd2;
    out.bytes[2] = 0x46;
    out.bytes[3] = 0x01;
    out.bytes[4] = 0x86;
    out.bytes[5] = 0xf7;
    out.bytes[6] = 0x23;
    out.bytes[7] = 0x3c;
    out.bytes[8] = 0x92;
    out.bytes[9] = 0x7e;
    out.bytes[10] = 0x7d;
    out.bytes[11] = 0xb2;
    out.bytes[12] = 0xdc;
    out.bytes[13] = 0xc7;
    out.bytes[14] = 0x03;
    out.bytes[15] = 0xc0;
    out.bytes[16] = 0xe5;
    out.bytes[17] = 0x00;
    out.bytes[18] = 0xb6;
    out.bytes[19] = 0x53;
    out.bytes[20] = 0xca;
    out.bytes[21] = 0x82;
    out.bytes[22] = 0x27;
    out.bytes[23] = 0x3b;
    out.bytes[24] = 0x7b;
    out.bytes[25] = 0xfa;
    out.bytes[26] = 0xd8;
    out.bytes[27] = 0x04;
    out.bytes[28] = 0x5d;
    out.bytes[29] = 0x85;
    out.bytes[30] = 0xa4;
    out.bytes[31] = 0x70;
    return out;
}

inline evmc_bytes32 keccak256Code(bcos::bytesConstRef code)
{
    if (code.empty())
    {
        return emptyCodeHash();
    }
    auto const hash = ethash::keccak256(code.data(), code.size());
    evmc_bytes32 out{};
    std::memcpy(out.bytes, hash.bytes, sizeof(out.bytes));
    return out;
}

inline bcos::u256 fromEvmC(const evmc_bytes32& value)
{
    return fromBigEndian<bcos::u256>(value.bytes);
}

inline evmc_bytes32 toEvmC(const bcos::u256& value)
{
    evmc_bytes32 out{};
    auto encoded = toBigEndian(value);
    std::copy(encoded.begin(), encoded.end(), out.bytes);
    return out;
}

inline evmc_bytes32 toEvmC(const bcos::crypto::HashType& value)
{
    evmc_bytes32 out{};
    std::copy(value.begin(), value.end(), out.bytes);
    return out;
}

inline uint8_t fromHexNibble(char value) noexcept
{
    auto lower = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    if (lower >= '0' && lower <= '9')
    {
        return static_cast<uint8_t>(lower - '0');
    }
    if (lower >= 'a' && lower <= 'f')
    {
        return static_cast<uint8_t>(10 + lower - 'a');
    }
    return 0xff;
}

inline evmc_address parseHexAddress(std::string_view value) noexcept
{
    if (value.starts_with("0x") || value.starts_with("0X"))
    {
        value.remove_prefix(2);
    }
    if (value.size() != sizeof(evmc_address) * 2)
    {
        return {};
    }

    evmc_address address{};
    for (size_t index = 0; index < sizeof(evmc_address); ++index)
    {
        auto high = fromHexNibble(value[index * 2]);
        auto low = fromHexNibble(value[index * 2 + 1]);
        if (high == 0xff || low == 0xff)
        {
            return {};
        }
        address.bytes[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return address;
}

inline void appendRlpUint64(bcos::bytes& out, uint64_t value)
{
    if (value == 0)
    {
        out.push_back(0x80);
        return;
    }
    bcos::bytes encoded;
    while (value > 0)
    {
        encoded.insert(encoded.begin(), static_cast<uint8_t>(value & 0xff));
        value >>= 8;
    }
    if (encoded.size() == 1 && encoded[0] < 0x80)
    {
        out.push_back(encoded[0]);
        return;
    }
    out.push_back(static_cast<uint8_t>(0x80 + encoded.size()));
    out.insert(out.end(), encoded.begin(), encoded.end());
}

inline evmc_address predictLegacyCreateAddress(evmc_address const& sender, uint64_t nonce) noexcept
{
    bcos::bytes payload;
    payload.push_back(0x94);
    payload.insert(payload.end(), sender.bytes, sender.bytes + sizeof(sender.bytes));
    appendRlpUint64(payload, nonce);

    bcos::bytes rlp;
    rlp.push_back(static_cast<uint8_t>(0xc0 + payload.size()));
    rlp.insert(rlp.end(), payload.begin(), payload.end());

    auto const hash = ethash::keccak256(rlp.data(), rlp.size());
    evmc_address out{};
    std::memcpy(out.bytes, hash.bytes + 12, sizeof(out.bytes));
    return out;
}
}  // namespace bcos::evm::state
