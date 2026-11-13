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
 * @file FilterRequest.cpp
 * @author: kyonGuo
 * @date 2024/4/11
 */
#include "bcos-rpc/filter/FilterRequest.h"
#include "bcos-rpc/filter/Common.h"
#include "bcos-rpc/jsonrpc/Common.h"
#include "bcos-rpc/util.h"

#include <memory>

using namespace bcos;
using namespace bcos::rpc;

void FilterRequest::fromJson(const Json::Value& jParams, protocol::BlockNumber latest,
    protocol::BlockNumber safeDepth, protocol::BlockNumber finalizedDepth,
    std::optional<protocol::BlockNumber> forkchoiceSafe,
    std::optional<protocol::BlockNumber> forkchoiceFinalized, bool failClosedOnMissingForkchoice)
{
    // Every criterion of an Ethereum filter is optional, and the two ways to bound a query are
    // alternatives: either a {fromBlock, toBlock} range (each defaulting to "latest") or a
    // single blockHash. The old code required fromBlock/toBlock, which rejected exactly what the
    // common client shapes send — ethers' queryFilter(event) and web3.js'
    // getPastLogs({address}) omit the range, and a blockHash-only request carries no range by
    // definition, so the blockHash fast path in FilterSystem::getLogsImpl was unreachable from
    // any spec-conformant caller.
    //
    // A "blockHash" key is read in both casings: the JSON-RPC field is blockHash, but older
    // builds of this file only ever looked at the all-lowercase spelling, and the Web3 entry
    // hands the request object over verbatim (no key normalisation), so clients written against
    // that build send blockhash. Accept both rather than silently ignoring one.
    auto const blockHashMember = [&jParams]() -> const Json::Value* {
        for (char const* key : {"blockHash", "blockhash"})
        {
            if (jParams.isMember(key) && !jParams[key].isNull())
            {
                return std::addressof(jParams[key]);
            }
        }
        return nullptr;
    }();

    // A blockHash-only request needs no range; resolve it before the range defaults so that a
    // missing fromBlock/toBlock cannot reject it. EIP-234 makes blockHash an alternative to the
    // {fromBlock, toBlock} pair rather than an addition to it, so a request carrying both is
    // rejected. Resolving the range and then letting getLogsImpl prefer blockHash would answer a
    // single block's logs where the caller asked for a range -- a plausible-looking smaller
    // result with no error, which is the failure shape this PR exists to remove.
    if (blockHashMember != nullptr)
    {
        auto const hasRangeField = [&jParams](char const* key) {
            return jParams.isMember(key) && !jParams[key].isNull();
        };
        if (hasRangeField("fromBlock") || hasRangeField("toBlock"))
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParamsCode(),
                "blockHash is mutually exclusive with the fromBlock/toBlock range"));
        }
        m_blockHash = blockHashMember->asString();
    }

    // `address` and `topics` are optional in the Ethereum filter spec: an omitted (or null)
    // field matches every address / every topic position. LogMatcher already treats an empty
    // address set or an empty/absent topic position as "match all", so the only thing the old
    // required-field check achieved was rejecting valid requests (eth_getLogs without a
    // `topics` key is what most SDKs and `cast logs` send). Parse each field only when present.
    auto const hasAddress = jParams.isMember("address") && !jParams["address"].isNull();
    auto const hasTopics = jParams.isMember("topics") && !jParams["topics"].isNull();

    // prase fromBlock / toBlock. Both default to "latest" when omitted or null, which is what
    // the spec says and what clients that pass only a criterion (or only a blockHash) rely on:
    // getBlockNumberByTag maps an empty tag to (latest, isLatest=true).
    auto const blockTagOf = [&jParams](char const* key) -> std::string {
        if (!jParams.isMember(key) || jParams[key].isNull())
        {
            return {};
        }
        return jParams[key].asString();
    };

    // prase fromBlock
    std::tie(m_fromBlock, m_fromIsLatest) =
        getBlockNumberByTag(latest, blockTagOf("fromBlock"), safeDepth, finalizedDepth,
            forkchoiceSafe, forkchoiceFinalized, failClosedOnMissingForkchoice);

    // prase toBlock
    std::tie(m_toBlock, m_toIsLatest) =
        getBlockNumberByTag(latest, blockTagOf("toBlock"), safeDepth, finalizedDepth,
            forkchoiceSafe, forkchoiceFinalized, failClosedOnMissingForkchoice);

    // prase address
    if (hasAddress)
    {
        auto& jAddresses = jParams["address"];
        if (jAddresses.isArray())
        {
            for (Json::Value::ArrayIndex index = 0; index < jAddresses.size(); ++index)
            {
                addAddress(jAddresses[index].asString());
            }
        }
        else
        {
            addAddress(jAddresses.asString());
        }
    }

    // prase topics
    if (hasTopics)
    {
        auto& jTopics = jParams["topics"];
        if (jTopics.size() > EVENT_LOG_TOPICS_MAX_INDEX)
        {
            FILTER_LOG(ERROR) << LOG_BADGE("fromJson") << LOG_DESC("exceed max topics")
                              << LOG_KV("topicsSize", jTopics.size());
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParamsCode(), "exceed max topics"));
        }
        resizeTopic(jTopics.size());
        for (Json::Value::ArrayIndex index = 0; index < jTopics.size(); ++index)
        {
            auto& jIndex = jTopics[index];
            if (jIndex.isNull())
            {
                continue;
            }

            if (jIndex.isArray())
            {  // array topics
                for (Json::Value::ArrayIndex innerIndex = 0; innerIndex < jIndex.size();
                    ++innerIndex)
                {
                    addTopic(index, jIndex[innerIndex].asString());
                }
            }
            else
            {  // single topic, string value
                addTopic(index, jIndex.asString());
            }
        }
    }

    // The blockHash was parsed above, before the range defaults.
}

bool FilterRequest::checkBlockRange()
{
    if (fromBlock() < 0 || toBlock() < 0)
    {
        return false;
    }
    if (fromIsLatest() && toIsLatest())
    {
        return true;
    }
    if (!fromIsLatest() && !toIsLatest() && toBlock() >= fromBlock())
    {
        return true;
    }
    if (!fromIsLatest() && toIsLatest())
    {
        return true;
    }
    return false;
}