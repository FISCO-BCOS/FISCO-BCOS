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
 * @brief MPT key namespace constants and encode/decode helpers (spec §5.1)
 * @file KeyPrefixes.h
 * @author: kyonRay
 * @date: 2026-05-12
 */
#pragma once
#include <bcos-utilities/FixedBytes.h>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::storage2
{

/// The sole "/mpt/" ASCII prefix for MPT node keys in the default ColumnFamily.
/// All MPT keys are exactly 37 bytes: kMPTPrefix (5) + raw keccak hash (32).
/// Do NOT write "/mpt/" literals anywhere else — use makeMPTNodeKey / parseMPTNodeKey.
inline constexpr std::string_view kMPTPrefix = "/mpt/";
inline constexpr std::size_t kMPTKeyLength = kMPTPrefix.size() + 32;  // 37

/// Encode a 32-byte keccak hash into a 37-byte RocksDB key with the /mpt/ namespace prefix.
/// This is the ONLY place that embeds kMPTPrefix into a key.
inline std::string makeMPTNodeKey(const h256& hash)
{
    std::string key;
    key.reserve(kMPTPrefix.size() + h256::SIZE);
    key.append(kMPTPrefix);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    key.append(reinterpret_cast<const char*>(hash.data()), h256::SIZE);
    return key;
}

/// Decode a raw RocksDB key back to its h256 hash.
/// Returns std::nullopt if the key is not a valid 37-byte /mpt/-prefixed key.
/// Never throws — this is runtime data validation.
inline std::optional<h256> parseMPTNodeKey(std::string_view key) noexcept
{
    if (key.size() != kMPTKeyLength)
    {
        return std::nullopt;
    }
    if (key.substr(0, kMPTPrefix.size()) != kMPTPrefix)
    {
        return std::nullopt;
    }
    h256 hash;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    std::memcpy(hash.data(), key.data() + kMPTPrefix.size(), h256::SIZE);
    return hash;
}

}  // namespace bcos::storage2
