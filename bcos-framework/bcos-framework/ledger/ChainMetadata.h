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
 * @file ChainMetadata.h
 * @brief Chain-level metadata helpers, including the OP fork-schedule triple.
 */
#pragma once

#include "OpForkScheduleCodec.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Task.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace bcos::ledger
{
inline constexpr std::string_view OP_FORK_SCHEDULE_KEY = "op_fork_schedule";
inline constexpr std::string_view OP_FORK_SCHEDULE_HASH_KEY = "op_fork_schedule_hash";
inline constexpr std::string_view OP_FORK_SCHEDULE_GENESIS_KEY = "op_fork_schedule_genesis";

struct OpForkScheduleMetadata
{
    std::string schedule;
    crypto::HashType scheduleHash;
    crypto::HashType genesisHash;
};

struct OpForkScheduleMetadataRows
{
    std::optional<std::string> schedule;
    std::optional<std::string> scheduleHash;
    std::optional<std::string> genesisHash;
};

[[nodiscard]] inline bool opForkScheduleMetadataRowsAbsent(
    OpForkScheduleMetadataRows const& rows) noexcept
{
    return !rows.schedule.has_value() && !rows.scheduleHash.has_value() &&
           !rows.genesisHash.has_value();
}

[[nodiscard]] inline bool opForkScheduleMetadataRowsPartial(
    OpForkScheduleMetadataRows const& rows) noexcept
{
    const bool any =
        rows.schedule.has_value() || rows.scheduleHash.has_value() || rows.genesisHash.has_value();
    const bool all =
        rows.schedule.has_value() && rows.scheduleHash.has_value() && rows.genesisHash.has_value();
    return any && !all;
}

[[nodiscard]] inline crypto::HashType parseOpForkScheduleHexHash(std::string_view hex)
{
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
        hex.remove_prefix(2);
    if (hex.size() != 64)
        throwInvalidOpForkSchedule("op fork schedule hash hex must be 64 characters");
    try
    {
        return crypto::HashType{std::string(hex)};
    }
    catch (bcos::BadHexCharacter const&)
    {
        throwInvalidOpForkSchedule("op fork schedule hash hex is invalid");
    }
}

[[nodiscard]] inline OpForkScheduleMetadata buildOpForkScheduleMetadata(
    std::string_view canonical, crypto::HashType const& genesisHash)
{
    auto normalized = canonicalOpForkSchedule(parseOpForkSchedule(canonical));
    // normalized is normalized by construction — hash it directly instead of paying a
    // second parse+normalize inside keccakOpForkScheduleHash.
    const auto scheduleHash = keccakNormalizedOpForkScheduleHash(normalized);
    return OpForkScheduleMetadata{
        .schedule = std::move(normalized),
        .scheduleHash = scheduleHash,
        .genesisHash = genesisHash,
    };
}

[[nodiscard]] inline OpForkScheduleMetadata validateOpForkScheduleMetadataRows(
    OpForkScheduleMetadataRows const& rows, crypto::HashType const& expectedGenesisHash)
{
    if (opForkScheduleMetadataRowsAbsent(rows))
    {
        throwInvalidOpForkSchedule("op fork schedule metadata is absent");
    }
    if (opForkScheduleMetadataRowsPartial(rows))
    {
        throwInvalidOpForkSchedule("partial op fork schedule metadata triple");
    }

    auto metadata =
        buildOpForkScheduleMetadata(*rows.schedule, parseOpForkScheduleHexHash(*rows.genesisHash));
    if (metadata.genesisHash != expectedGenesisHash)
    {
        throwInvalidOpForkSchedule("op fork schedule genesis binding mismatch");
    }
    if (metadata.scheduleHash != parseOpForkScheduleHexHash(*rows.scheduleHash))
    {
        throwInvalidOpForkSchedule("op fork schedule hash mismatch");
    }
    return metadata;
}

// Fail-closed. Never fall back to feature flags when stored metadata exists but
// hash or genesis binding mismatches. Boot must pass the hash from
// readOpForkScheduleMetadata (or the live genesis header); do not skip that check.
[[nodiscard]] inline std::string resolveOpForkScheduleCanonical(
    std::optional<OpForkScheduleMetadata> const& stored,
    std::optional<std::string> const& genesisCanonical, bool featureOpJovian,
    crypto::HashType const& expectedGenesisHash)
{
    if (stored.has_value())
    {
        if (stored->genesisHash != expectedGenesisHash)
        {
            throwInvalidOpForkSchedule("op fork schedule genesis binding mismatch");
        }
        if (keccakOpForkScheduleHash(stored->schedule) != stored->scheduleHash)
        {
            throwInvalidOpForkSchedule("op fork schedule hash mismatch");
        }
        return canonicalOpForkSchedule(parseOpForkSchedule(stored->schedule));
    }
    if (genesisCanonical.has_value())
    {
        return canonicalOpForkSchedule(parseOpForkSchedule(*genesisCanonical));
    }
    return std::string(legacyOpForkScheduleCanonical(featureOpJovian));
}

/// True when on-chain metadata exists and the genesis `canonical` is a
/// different schedule after both sides are normalized. Missing genesis is not
/// a divergence. Compare parsed identity, not raw ASCII (`00:isthmus` == `0:isthmus`).
[[nodiscard]] inline bool storedOpForkScheduleDivergesFromGenesis(
    std::string_view storedCanonical, std::optional<std::string> const& genesisCanonical)
{
    if (!genesisCanonical.has_value())
    {
        return false;
    }
    return canonicalOpForkSchedule(parseOpForkSchedule(storedCanonical)) !=
           canonicalOpForkSchedule(parseOpForkSchedule(*genesisCanonical));
}

namespace detail
{
inline executor_v1::StateKeyView opForkScheduleMetadataKey(std::string_view key)
{
    return executor_v1::StateKeyView(SYS_CHAIN_METADATA, key);
}
}  // namespace detail

template <class Storage>
task::Task<OpForkScheduleMetadataRows> readOpForkScheduleMetadataRows(Storage& storage)
{
    const auto entries = co_await storage2::readSome(
        storage, std::array{detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_KEY),
                     detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_HASH_KEY),
                     detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_GENESIS_KEY)});

    OpForkScheduleMetadataRows rows;
    if (entries[0])
    {
        rows.schedule = entries[0]->get();
    }
    if (entries[1])
    {
        rows.scheduleHash = entries[1]->get();
    }
    if (entries[2])
    {
        rows.genesisHash = entries[2]->get();
    }
    co_return rows;
}

template <class Storage>
task::Task<std::optional<OpForkScheduleMetadata>> readOpForkScheduleMetadata(
    Storage& storage, crypto::HashType const& expectedGenesisHash)
{
    auto rows = co_await readOpForkScheduleMetadataRows(storage);
    if (opForkScheduleMetadataRowsAbsent(rows))
    {
        co_return std::nullopt;
    }
    co_return validateOpForkScheduleMetadataRows(std::move(rows), expectedGenesisHash);
}

template <class Storage>
task::Task<void> writeOpForkScheduleMetadata(
    Storage& storage, OpForkScheduleMetadata const& metadata)
{
    storage::Entry scheduleEntry;
    scheduleEntry.set(metadata.schedule);
    co_await storage2::writeOne(
        storage, detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_KEY), std::move(scheduleEntry));

    storage::Entry hashEntry;
    hashEntry.set(metadata.scheduleHash.hex());
    co_await storage2::writeOne(storage,
        detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_HASH_KEY), std::move(hashEntry));

    storage::Entry genesisEntry;
    genesisEntry.set(metadata.genesisHash.hex());
    co_await storage2::writeOne(storage,
        detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_GENESIS_KEY), std::move(genesisEntry));
}
}  // namespace bcos::ledger
