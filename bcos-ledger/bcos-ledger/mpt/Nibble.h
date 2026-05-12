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
 * @file Nibble.h
 * @brief Nibble (4-bit) helper routines for the MPT module (spec §5.5)
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace bcos::ledger::mpt
{

/// Splits each byte into two nibbles (high nibble first).
/// Example: {0xab, 0xcd} -> {0x0a, 0x0b, 0x0c, 0x0d}
std::vector<uint8_t> bytesToNibbles(std::span<bcos::byte const> in);

/// Merges pairs of nibbles back into bytes (high nibble first).
/// @throws MPTInvariantViolation when nibbles.size() is odd.
bcos::bytes nibblesToBytes(std::span<uint8_t const> nibbles);

/// Returns the length of the longest common prefix of two nibble sequences.
size_t commonPrefixLen(std::span<uint8_t const> a, std::span<uint8_t const> b) noexcept;

}  // namespace bcos::ledger::mpt
