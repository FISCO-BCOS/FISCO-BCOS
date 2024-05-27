/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file RateLimiterManager.cpp
 * @author: octopus
 * @date 2022-06-30
 */

#include "bcos-gateway/Common.h"
#include "bcos-utilities/BoostLog.h"
#include <bcos-gateway/libratelimit/RateLimiterManager.h>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimiter;

const std::string RateLimiterManager::TOTAL_OUTGOING_KEY = "total-outgoing-key";

void RateLimiterManager::initP2pBasicMsgTypes()
{
    /** ref: enum GatewayMessageType */
    m_p2pBasicMsgTypes.fill(false);
    m_modulesWithoutLimit.fill(false);
    for (uint16_t type : {GatewayMessageType::Heartbeat, GatewayMessageType::Handshake,
             GatewayMessageType::RequestNodeStatus, GatewayMessageType::ResponseNodeStatus,
             GatewayMessageType::SyncNodeSeq, GatewayMessageType::RouterTableSyncSeq,
             GatewayMessageType::RouterTableResponse, GatewayMessageType::RouterTableRequest})
    {
        m_p2pBasicMsgTypes.at(type) = true;
    }
}

bcos::ratelimiter::RateLimiterInterface::Ptr RateLimiterManager::getRateLimiter(const std::string& _rateLimiterKey)
{
    std::shared_lock lock(x_rateLimiters);
    auto it = m_rateLimiters.find(_rateLimiterKey);
    if (it != m_rateLimiters.end())
    {
        return it->second;
    }

    return nullptr;
}

std::pair<bool, bcos::ratelimiter::RateLimiterInterface::Ptr> RateLimiterManager::registerRateLimiter(
    const std::string& _rateLimiterKey, bcos::ratelimiter::RateLimiterInterface::Ptr _rateLimiter)
{
    bool result = false;
    bcos::ratelimiter::RateLimiterInterface::Ptr oldRateLimiter = nullptr;
    {
        std::unique_lock lock(x_rateLimiters);

        auto it = m_rateLimiters.find(_rateLimiterKey);
        if (it == m_rateLimiters.end())
        {
            result = true;
            m_rateLimiters[_rateLimiterKey] = std::move(_rateLimiter);
        }
        else
        {
            oldRateLimiter = it->second;
        }
    }

    RATELIMIT_MGR_LOG(INFO) << LOG_BADGE("registerRateLimiter") << LOG_DESC("register ratelimiter")
                            << LOG_KV("rateLimiterKey", _rateLimiterKey)
                            << LOG_KV("result", result);


    return {result, oldRateLimiter};
}

bool RateLimiterManager::removeRateLimiter(const std::string& _rateLimiterKey)
{
    bool result = false;
    {
        std::unique_lock lock(x_rateLimiters);
        result = (m_rateLimiters.erase(_rateLimiterKey) > 0);
    }

    RATELIMIT_MGR_LOG(INFO) << LOG_BADGE("removeRateLimiter")
                            << LOG_KV("rateLimiterKey", _rateLimiterKey)
                            << LOG_KV("result", result);
    return result;
}

bcos::ratelimiter::RateLimiterInterface::Ptr RateLimiterManager::getGroupRateLimiter(const std::string& _group)
{
    if (!m_enableOutGroupRateLimit)
    {
        return nullptr;
    }

    const std::string& rateLimiterKey = _group;

    auto rateLimiter = getRateLimiter(rateLimiterKey);
    if (rateLimiter != nullptr)
    {
        return rateLimiter;
    }

    // rete limiter not exist, create it
    int64_t groupOutgoingBwLimit = -1;
    int32_t timeWindowS = m_rateLimiterConfig.timeWindowSec;
    int32_t timeWindowMS = toMillisecond(m_rateLimiterConfig.timeWindowSec);
    bool allowExceedMaxPermitSize = m_rateLimiterConfig.allowExceedMaxPermitSize;

    auto it = m_rateLimiterConfig.group2BwLimit.find(_group);
    if (it != m_rateLimiterConfig.group2BwLimit.end())
    {
        groupOutgoingBwLimit = it->second;
    }
    else if (m_rateLimiterConfig.groupOutgoingBwLimit > 0)
    {
        groupOutgoingBwLimit = m_rateLimiterConfig.groupOutgoingBwLimit;
    }

    if (groupOutgoingBwLimit > 0)
    {
        RATELIMIT_MGR_LOG(INFO) << LOG_BADGE("getGroupRateLimiter")
                                << LOG_DESC("group rate limiter not exist")
                                << LOG_KV("rateLimiterKey", rateLimiterKey)
                                << LOG_KV("groupOutgoingBwLimit", groupOutgoingBwLimit)
                                << LOG_KV("timeWindowS", timeWindowS)
                                << LOG_KV("enableDistributedRatelimit",
                                       m_rateLimiterConfig.enableDistributedRatelimit);

        if (m_rateLimiterConfig.enableDistributedRatelimit)
        {
            rateLimiter = m_rateLimiterFactory->buildDistributedRateLimiter(
                m_rateLimiterFactory->toTokenKey(_group), groupOutgoingBwLimit * timeWindowS,
                timeWindowS, allowExceedMaxPermitSize,
                m_rateLimiterConfig.enableDistributedRateLimitCache,
                m_rateLimiterConfig.distributedRateLimitCachePercent);
        }
        else
        {
            rateLimiter = m_rateLimiterFactory->buildTimeWindowRateLimiter(
                groupOutgoingBwLimit * timeWindowS, timeWindowMS, allowExceedMaxPermitSize);
        }

        auto result = registerRateLimiter(_group, rateLimiter);
        rateLimiter = (result.first ? rateLimiter : result.second);
    }

    return rateLimiter;
}

bcos::ratelimiter::RateLimiterInterface::Ptr RateLimiterManager::getInRateLimiter(
    const std::string& _connIP, uint16_t _packageType)
{
    // // TODO:check package valid
    // if (_packageType == 0)
    // {
    //     RATELIMIT_MGR_LOG(ERROR) << LOG_BADGE("getInRateLimiter") << LOG_DESC("pkg type is
    //     invalid")
    //                              << LOG_KV("packageType", _packageType)
    //                              << LOG_KV("connIP", _connIP);
    // }

    // not enable msg type limit
    if (!m_rateLimiterConfig.enableInP2pBasicMsgLimit())
    {
        return nullptr;
    }

    // not basic msg type
    if (!isP2pBasicMsgType(_packageType))
    {
        return nullptr;
    }

    const std::string& inKey = _connIP + "_" + std::to_string(_packageType);
    auto rateLimiter = getRateLimiter(inKey);
    if (rateLimiter != nullptr)
    {
        return rateLimiter;
    }

    int32_t timeWindowS = m_rateLimiterConfig.timeWindowSec;
    int32_t timeWindowMS = toMillisecond(m_rateLimiterConfig.timeWindowSec);
    bool allowExceedMaxPermitSize = m_rateLimiterConfig.allowExceedMaxPermitSize;
    int64_t QPS = m_rateLimiterConfig.p2pBasicMsgQPS;

    rateLimiter = m_rateLimiterFactory->buildTimeWindowRateLimiter(
        QPS * timeWindowS, timeWindowMS, allowExceedMaxPermitSize);

    auto result = registerRateLimiter(inKey, rateLimiter);
    return result.first ? rateLimiter : result.second;
}

bcos::ratelimiter::RateLimiterInterface::Ptr RateLimiterManager::getInRateLimiter(
    const std::string& _groupID, uint16_t _moduleID, bool /***/)
{
    if (!m_enableInRateLimit)
    {
        return nullptr;
    }

    if (!m_rateLimiterConfig.enableInP2pModuleMsgLimit(_moduleID))
    {
        return nullptr;
    }

    const std::string& inKey = _groupID + "_" + std::to_string(_moduleID);
    auto rateLimiter = getRateLimiter(inKey);
    if (rateLimiter != nullptr)
    {
        return rateLimiter;
    }

    int32_t timeWindowS = m_rateLimiterConfig.timeWindowSec;
    int32_t timeWindowMS = toMillisecond(m_rateLimiterConfig.timeWindowSec);
    bool allowExceedMaxPermitSize = m_rateLimiterConfig.allowExceedMaxPermitSize;
    int64_t QPS = m_rateLimiterConfig.p2pModuleMsgQPS;
    auto enableDistributedRatelimit = m_rateLimiterConfig.enableDistributedRatelimit;

    auto tempQPS = m_rateLimiterConfig.moduleMsg2QPS.at(_moduleID);
    if (tempQPS > 0)
    {
        QPS = tempQPS;
    }

    RATELIMIT_MGR_LOG(INFO) << LOG_BADGE("getInRateLimiter")
                            << LOG_DESC("in rate limiter not exist") << LOG_KV("inKey", inKey)
                            << LOG_KV("QPS", QPS) << LOG_KV("timeWindowS", timeWindowS)
                            << LOG_KV("enableDistributedRatelimit",
                                   m_rateLimiterConfig.enableDistributedRatelimit);

    if (enableDistributedRatelimit)
    {
        rateLimiter = m_rateLimiterFactory->buildDistributedRateLimiter(
            m_rateLimiterFactory->toTokenKey(inKey), QPS * timeWindowS, timeWindowS,
            allowExceedMaxPermitSize, m_rateLimiterConfig.enableDistributedRateLimitCache,
            m_rateLimiterConfig.distributedRateLimitCachePercent);
    }
    else
    {
        rateLimiter = m_rateLimiterFactory->buildTimeWindowRateLimiter(
            QPS * timeWindowS, timeWindowMS, allowExceedMaxPermitSize);
    }

    auto result = registerRateLimiter(inKey, rateLimiter);
    return result.first ? rateLimiter : result.second;
}

bcos::ratelimiter::RateLimiterInterface::Ptr RateLimiterManager::getConnRateLimiter(const std::string& _connIP)
{
    if (!m_enableOutConRateLimit)
    {
        return nullptr;
    }

    const std::string& rateLimiterKey = _connIP;

    auto rateLimiter = getRateLimiter(rateLimiterKey);
    if (rateLimiter != nullptr)
    {
        return rateLimiter;
    }

    int64_t connOutgoingBwLimit = -1;
    int32_t timeWindowS = m_rateLimiterConfig.timeWindowSec;
    int32_t timeWindowMS = toMillisecond(m_rateLimiterConfig.timeWindowSec);
    bool allowExceedMaxPermitSize = m_rateLimiterConfig.allowExceedMaxPermitSize;

    auto it = m_rateLimiterConfig.ip2BwLimit.find(_connIP);
    if (it != m_rateLimiterConfig.ip2BwLimit.end())
    {
        connOutgoingBwLimit = it->second;
    }
    else if (m_rateLimiterConfig.connOutgoingBwLimit > 0)
    {
        connOutgoingBwLimit = m_rateLimiterConfig.connOutgoingBwLimit;
    }

    if (connOutgoingBwLimit > 0)
    {
        RATELIMIT_MGR_LOG(INFO) << LOG_BADGE("getConnRateLimiter")
                                << LOG_DESC("conn rate limiter not exist")
                                << LOG_KV("rateLimiterKey", rateLimiterKey)
                                << LOG_KV("connOutgoingBwLimit", connOutgoingBwLimit)
                                << LOG_KV("timeWindowS", timeWindowS);

        rateLimiter = m_rateLimiterFactory->buildTimeWindowRateLimiter(
            connOutgoingBwLimit * timeWindowS, timeWindowMS, allowExceedMaxPermitSize);
        auto result = registerRateLimiter(rateLimiterKey, rateLimiter);
        rateLimiter = (result.first ? rateLimiter : result.second);
    }

    return rateLimiter;
}