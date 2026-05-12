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
 * @file HexPrefix.h
 * @brief Hex-Prefix (compact) encoding per Yellow Paper Appendix C (spec §5.5)
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{

/// Encodes a nibble sequence with a terminator flag using Hex-Prefix encoding.
///
/// HP 编码（Yellow Paper Appendix C）首字节布局：
///   bits [7:6] = 0
///   bit  5     = terminator flag (1 = leaf, 0 = extension)
///   bit  4     = 1 when the nibble count is odd
///   bits [3:0] = first nibble (only meaningful when bit 4 = 1; zero when even)
///
/// @param nibbles  Input nibble sequence (each element in [0, 15])
/// @param terminator  true = leaf node (terminating), false = extension node
/// @return Compact-encoded byte sequence
bcos::bytes hexPrefixEncode(std::span<uint8_t const> nibbles, bool terminator);

/// Decodes a Hex-Prefix encoded byte sequence.
///
/// @param encoded  Compact-encoded input (must be non-empty)
/// @return {nibbles, terminator}
/// @throws MPTDecodeError on empty input
std::pair<std::vector<uint8_t>, bool> hexPrefixDecode(std::span<bcos::byte const> encoded);

}  // namespace bcos::ledger::mpt
