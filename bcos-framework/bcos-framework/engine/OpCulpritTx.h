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
 * @file OpCulpritTx.h
 * @brief Wire format for the OP build-loop poisoned-tx eviction tag.
 *
 * `SchedulerInterface::executeBlock` only carries `bcos::Error` (a string).
 * `OpConsensusError::txHash` does not survive that boundary. The producer
 * (OpConsensusError two-arg ctor / OpScheduler catch) appends a trailing
 * `[tx=0x<64 hex>]` so `buildOpPayload` can evict the sealed culprit.
 */

#pragma once

#include <bcos-utilities/FixedBytes.h>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::engine
{
inline constexpr std::string_view c_opCulpritTxTag = "[tx=0x";

inline std::string formatOpCulpritTag(h256 const& txHash)
{
    return std::string(c_opCulpritTxTag) + txHash.hex() + "]";
}

inline std::string appendOpCulpritTag(std::string message, h256 const& txHash)
{
    message.push_back(' ');
    message += formatOpCulpritTag(txHash);
    return message;
}

inline std::optional<h256> parseOpCulpritHash(std::string_view message)
{
    auto const pos = message.rfind(c_opCulpritTxTag);
    if (pos == std::string_view::npos)
    {
        return std::nullopt;
    }
    auto const hex = message.substr(pos + c_opCulpritTxTag.size(), 64);
    if (hex.size() != 64)
    {
        return std::nullopt;
    }
    try
    {
        return h256(std::string(hex), h256::FromHex);
    }
    catch (...)
    {
        return std::nullopt;
    }
}
}  // namespace bcos::engine
