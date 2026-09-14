/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file util.cpp
 * @author: jdkuang
 * @date 2024/4/24
 */

#include "bcos-rpc/Common.h"
#include "bcos-rpc/jsonrpc/Common.h"
#include "bcos-rpc/util.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-ledger/Ledger.h>
#include <boost/regex.hpp>
#include <limits>
#include <optional>
#include <string_view>

using namespace bcos;
using namespace bcos::rpc;

// return (actual block number, isLatest block)
std::tuple<protocol::BlockNumber, bool> bcos::rpc::getBlockNumberByTag(
    protocol::BlockNumber latest, std::string_view blockTag, protocol::BlockNumber safeDepth,
    protocol::BlockNumber finalizedDepth,
    std::optional<protocol::BlockNumber> forkchoiceSafe,
    std::optional<protocol::BlockNumber> forkchoiceFinalized,
    bool failClosedOnMissingForkchoice)
{
    if (blockTag.data() == nullptr || blockTag.empty())
    {
        return std::make_tuple(latest, true);
    }
    if (blockTag == EarliestBlock)
    {
        return std::make_tuple(0, false);
    }
    // safe / finalized: forkchoice value first, then latest - depth (clamped at 0). Default
    // depth 0 makes them "latest" (isLatest = true) — byte-identical to pre-upgrade behaviour;
    // a configured depth turns them into committed historical blocks. On the engine lane an
    // unset forkchoice value is a not-found result, never the unsafe tip (op-node treats a
    // wrong safe/finalized as immutable, a not-found as "not yet known").
    if (blockTag == SafeBlock)
    {
        if (forkchoiceSafe.has_value())
        {
            return std::make_tuple(*forkchoiceSafe, std::cmp_equal(latest, *forkchoiceSafe));
        }
        if (failClosedOnMissingForkchoice)
        {
            BOOST_THROW_EXCEPTION(bcos::ledger::NotFoundBlockHeader{});
        }
        auto const number = (std::max)(latest - safeDepth, protocol::BlockNumber{0});
        return std::make_tuple(number, std::cmp_equal(latest, number));
    }
    if (blockTag == FinalizedBlock)
    {
        if (forkchoiceFinalized.has_value())
        {
            return std::make_tuple(*forkchoiceFinalized, std::cmp_equal(latest, *forkchoiceFinalized));
        }
        if (failClosedOnMissingForkchoice)
        {
            BOOST_THROW_EXCEPTION(bcos::ledger::NotFoundBlockHeader{});
        }
        auto const number = (std::max)(latest - finalizedDepth, protocol::BlockNumber{0});
        return std::make_tuple(number, std::cmp_equal(latest, number));
    }
    if (blockTag == LatestBlock || blockTag == PendingBlock)
    {
        return std::make_tuple(latest, true);
    }

    const static boost::regex hexRegex("^0x[0-9a-fA-F]+$");
    if (boost::regex_match(blockTag.begin(), blockTag.end(), hexRegex))
    {
        // Reject a quantity that overflows the signed block-number range: fromQuantity
        // returns uint64, so a value above INT64_MAX would wrap to a negative height on
        // conversion (and one above UINT64_MAX throws std::invalid_argument) — neither is
        // a valid block tag; report InvalidParams, not a silent wrong number.
        uint64_t raw = 0;
        try
        {
            raw = fromQuantity(std::string(blockTag));
        }
        catch (...)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "Invalid Block Number: " + std::string(blockTag)));
        }
        if (raw > static_cast<uint64_t>(std::numeric_limits<protocol::BlockNumber>::max()))
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "Invalid Block Number: " + std::string(blockTag)));
        }
        return std::make_tuple(static_cast<protocol::BlockNumber>(raw), false);
    }
    BOOST_THROW_EXCEPTION(
        JsonRpcException(InvalidParams, "Invalid Block Number: " + std::string(blockTag)));
}