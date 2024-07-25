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

#include <bcos-rpc/Common.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/util.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/exception/diagnostic_information.hpp>

using namespace bcos;
using namespace bcos::rpc;

// return (actual block number, isLatest block)
std::tuple<protocol::BlockNumber, bool> bcos::rpc::getBlockNumberByTag(
    protocol::BlockNumber latest, std::string_view blockTag)
{
    if (blockTag.data() == nullptr || blockTag.empty())
    {
        return std::make_tuple(latest, true);
    }
    if (blockTag == EarliestBlock)
    {
        return std::make_tuple(0, false);
    }
    else if (blockTag == LatestBlock || blockTag == SafeBlock || blockTag == FinalizedBlock ||
             blockTag == PendingBlock)
    {
        return std::make_tuple(latest, true);
    }
    else
    {
        static const std::regex hexRegex("^0x[0-9a-fA-F]+$");
        if (std::regex_match(blockTag.data(), hexRegex))
        {
            auto blockNumber = fromQuantity(std::string(blockTag));
            return std::make_tuple(blockNumber, false);
        }
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "Invalid Block Number: " + std::string(blockTag)));
    }
}