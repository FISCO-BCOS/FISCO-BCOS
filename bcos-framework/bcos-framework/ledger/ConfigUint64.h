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
 * @file ConfigUint64.h
 * @brief The one acceptance window for operator-written config numbers.
 */

#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>

namespace bcos::ledger
{
/// Why a config number was refused. The callers all raise their own domain error with their
/// own message (a section-qualified config error in the loaders, InvalidOpForkSchedule in the
/// codec), so the parser reports the reason instead of a message.
enum class ConfigUint64Error : std::uint8_t
{
    none = 0,
    empty,
    invalid,
    outOfRange,
    sentinel,
};

/// The acceptance window: decimal, optionally 0x-prefixed hex, the ENTIRE string consumed
/// (std::stoull silently truncates "1677557088abc"; std::from_chars on an unsigned type
/// rejects the sign characters '-'/'+', unlike std::stoull which wraps '-' to a huge value),
/// an optional upper bound, and an optional not-scheduled sentinel to refuse (accepting it as
/// a real activation time would make resolveOpFork read the fork as never active).
struct ConfigUint64Options
{
    bool allowHex = false;
    std::uint64_t maxValue = std::numeric_limits<std::uint64_t>::max();
    bool refuseSentinel = false;
    std::uint64_t sentinel = std::numeric_limits<std::uint64_t>::max();
};

struct ConfigUint64Result
{
    std::optional<std::uint64_t> value;
    ConfigUint64Error error = ConfigUint64Error::none;
};

/// The single implementation behind the [op_fork_timestamps] / [op_eip1559] loaders and the
/// canonical [op_fork_schedule] codec: three copies of this loop had already diverged (one
/// gained the sentinel refusal, another a uint32 bound, the third rejected hex). Each caller
/// maps @c error to its own message.
[[nodiscard]] inline ConfigUint64Result parseConfigUint64(
    std::string_view text, ConfigUint64Options const& options = {})
{
    if (text.empty())
    {
        return {.value = std::nullopt, .error = ConfigUint64Error::empty};
    }
    std::string_view digits = text;
    int base = 10;
    if (options.allowHex && digits.size() > 2 && digits[0] == '0' &&
        (digits[1] == 'x' || digits[1] == 'X'))
    {
        base = 16;
        digits.remove_prefix(2);
    }
    std::uint64_t out = 0;
    auto const [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), out, base);
    if (ec == std::errc::result_out_of_range)
    {
        return {.value = std::nullopt, .error = ConfigUint64Error::outOfRange};
    }
    if (ec != std::errc{} || ptr != digits.data() + digits.size())
    {
        return {.value = std::nullopt, .error = ConfigUint64Error::invalid};
    }
    if (out > options.maxValue)
    {
        return {.value = std::nullopt, .error = ConfigUint64Error::outOfRange};
    }
    if (options.refuseSentinel && out == options.sentinel)
    {
        return {.value = std::nullopt, .error = ConfigUint64Error::sentinel};
    }
    return {.value = out, .error = ConfigUint64Error::none};
}
}  // namespace bcos::ledger
