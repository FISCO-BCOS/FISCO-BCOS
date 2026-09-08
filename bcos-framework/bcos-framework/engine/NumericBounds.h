/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <cstdint>
#include <limits>

namespace bcos::engine
{

/// Shared u256→uint64 ceiling. Engine validation returns optional; the OP
/// executor throws OpConsensusError — both must use this one comparison (A9-1).
[[nodiscard]] inline bool u256FitsUint64(u256 const& value)
{
    static u256 const kMaxU64(std::numeric_limits<std::uint64_t>::max());
    return value <= kMaxU64;
}

}  // namespace bcos::engine
