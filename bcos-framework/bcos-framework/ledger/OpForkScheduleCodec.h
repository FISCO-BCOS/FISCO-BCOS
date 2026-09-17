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
 * @file OpForkScheduleCodec.h
 * @brief Canonical OP fork-schedule codec (any contiguous EL fork range).
 */
#pragma once

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/throw_exception.hpp>

// Canonical fork-schedule codec aligned with op-geth / op-node. The schedule may
// start at any EL fork and must be contiguous from there; baseline timestamp is 0.

namespace bcos::ledger
{
struct OpForkActivationRecord
{
    std::string forkName;
    uint64_t timestamp = 0;
};

DERIVE_BCOS_EXCEPTION(InvalidOpForkSchedule);

inline constexpr std::string_view c_legacyIsthmusCanonical = "0:isthmus";
inline constexpr std::string_view c_legacyJovianCanonical = "0:jovian";

[[nodiscard]] inline std::string_view legacyOpForkScheduleCanonical(bool jovianActive) noexcept
{
    return jovianActive ? c_legacyJovianCanonical : c_legacyIsthmusCanonical;
}

[[noreturn]] inline void throwInvalidOpForkSchedule(std::string_view msg)
{
    BOOST_THROW_EXCEPTION(InvalidOpForkSchedule() << errinfo_comment(std::string(msg)));
}

namespace detail
{
// Protocol order is the array index; bcos-evm's OpFork enum must match it 1:1.
// op-geth params/config_op.go, whose OP EL fork fields have no Delta entry: delta
// does not affect the execution layer, so it is not nameable here. The count is
// deduced from the list, so the two cannot drift apart.
inline constexpr auto c_opForkNames = std::to_array<std::string_view>({
    "regolith",
    "canyon",
    "ecotone",
    "fjord",
    "granite",
    "holocene",
    "isthmus",
    "jovian",
    "karst",
});

// Pure lookups: no allocation and no throw path, so they are noexcept like
// legacyOpForkScheduleCanonical.
[[nodiscard]] inline constexpr int forkOrder(std::string_view forkName) noexcept
{
    for (std::size_t i = 0; i < c_opForkNames.size(); ++i)
    {
        if (c_opForkNames[i] == forkName)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

[[nodiscard]] inline constexpr bool isAllowedBaseline(std::string_view forkName) noexcept
{
    return forkOrder(forkName) >= 0;
}

inline std::string trimAscii(std::string_view input)
{
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front())) != 0)
        input.remove_prefix(1);
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back())) != 0)
        input.remove_suffix(1);
    return std::string(input);
}

inline uint64_t parseTimestamp(std::string_view token)
{
    if (token.empty())
        throwInvalidOpForkSchedule("empty timestamp");
    uint64_t value = 0;
    for (const char ch : token)
    {
        if (ch < '0' || ch > '9')
            throwInvalidOpForkSchedule("invalid timestamp");
        const auto digit = static_cast<uint64_t>(ch - '0');
        // Pre-multiply guard: `next < value` misses wraps that land above value.
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
            throwInvalidOpForkSchedule("timestamp overflow");
        value = value * 10 + digit;
    }
    return value;
}

inline std::string normalizeForkName(std::string_view token)
{
    auto forkName = trimAscii(token);
    for (char& ch : forkName)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return forkName;
}

inline void validateScheduleRecords(std::span<const OpForkActivationRecord> activations)
{
    if (activations.empty())
        throwInvalidOpForkSchedule("empty schedule");

    if (activations.front().timestamp != 0)
        throwInvalidOpForkSchedule("missing timestamp-0 baseline");

    if (!isAllowedBaseline(activations.front().forkName))
        throwInvalidOpForkSchedule("invalid baseline fork");

    int previousOrder = -1;
    uint64_t previousTimestamp = 0;

    for (std::size_t index = 0; index < activations.size(); ++index)
    {
        const auto& activation = activations[index];
        const int order = forkOrder(activation.forkName);
        if (order < 0)
            throwInvalidOpForkSchedule("unknown fork");

        if (activation.timestamp < previousTimestamp)
            throwInvalidOpForkSchedule("timestamps out of order");
        if (index != 0 && activation.timestamp == previousTimestamp)
            throwInvalidOpForkSchedule("duplicate timestamp");

        // The baseline is the anchor; every later activation must be the next
        // fork exactly (op-node checkFork: a set fork's prior fork must be set).
        // Contiguity forces order to strictly increase, so no `seenForks` scan.
        if (index != 0 && order != previousOrder + 1)
            throwInvalidOpForkSchedule("forks out of protocol order");

        previousOrder = order;
        previousTimestamp = activation.timestamp;
    }
}

inline std::string serializeScheduleRecords(std::span<const OpForkActivationRecord> activations)
{
    std::string canonical;
    for (std::size_t index = 0; index < activations.size(); ++index)
    {
        if (index != 0)
            canonical.push_back(',');
        canonical.append(std::to_string(activations[index].timestamp));
        canonical.push_back(':');
        canonical.append(activations[index].forkName);
    }
    return canonical;
}
}  // namespace detail

// 9 contiguous EL forks are 9 activations; headroom above that covers later
// additions while staying far under c_maxOpForkScheduleBytes.
inline constexpr std::size_t c_maxOpForkActivations = 16;
inline constexpr std::size_t c_maxOpForkScheduleBytes = 512;

inline std::vector<OpForkActivationRecord> parseOpForkSchedule(std::string_view canonical)
{
    const auto trimmed = detail::trimAscii(canonical);
    if (trimmed.empty())
        throwInvalidOpForkSchedule("empty schedule");
    if (trimmed.size() > c_maxOpForkScheduleBytes)
        throwInvalidOpForkSchedule("schedule too long");

    std::vector<OpForkActivationRecord> activations;
    std::string_view remaining{trimmed};
    while (!remaining.empty())
    {
        if (activations.size() >= c_maxOpForkActivations)
            throwInvalidOpForkSchedule("too many activations");

        const auto comma = remaining.find(',');
        const auto entry = remaining.substr(0, comma);
        const auto colon = entry.find(':');
        if (colon == std::string_view::npos)
            throwInvalidOpForkSchedule("invalid activation entry");

        OpForkActivationRecord record;
        record.timestamp = detail::parseTimestamp(entry.substr(0, colon));
        record.forkName = detail::normalizeForkName(entry.substr(colon + 1));
        activations.push_back(std::move(record));

        if (comma == std::string_view::npos)
            break;
        remaining.remove_prefix(comma + 1);
        if (remaining.empty())
            throwInvalidOpForkSchedule("trailing comma");
    }

    detail::validateScheduleRecords(activations);
    return activations;
}

inline std::string canonicalOpForkSchedule(std::span<const OpForkActivationRecord> activations)
{
    detail::validateScheduleRecords(activations);
    return detail::serializeScheduleRecords(activations);
}

inline crypto::HashType keccakOpForkScheduleHash(std::string_view canonical)
{
    const auto activations = parseOpForkSchedule(canonical);
    const auto normalized = detail::serializeScheduleRecords(activations);
    return crypto::keccak256Hash(
        bytesConstRef(reinterpret_cast<const byte*>(normalized.data()), normalized.size()));
}

/// Hash an ALREADY-normalized canonical schedule text — the output of
/// canonicalOpForkSchedule (e.g. the `.schedule` member buildOpForkScheduleMetadata just
/// normalized). Unlike keccakOpForkScheduleHash this does not parse/normalize again:
/// re-validating text that is normalized by construction is pure waste on the genesis
/// write path.
inline crypto::HashType keccakNormalizedOpForkScheduleHash(std::string_view normalizedCanonical)
{
    return crypto::keccak256Hash(bytesConstRef(
        reinterpret_cast<const byte*>(normalizedCanonical.data()), normalizedCanonical.size()));
}
}  // namespace bcos::ledger
