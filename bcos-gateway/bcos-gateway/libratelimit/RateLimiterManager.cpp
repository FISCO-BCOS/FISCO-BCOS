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
 * @file GroupBWRateLimiter.cpp
 * @author: octopus
 * @date 2022-06-30
 */

#include "bcos-gateway/Common.h"
#include <bcos-gateway/libratelimit/RateLimiterManager.h>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimit;

const std::string RateLimiterManager::TOTAL_OUTGOING_KEY = "total-outgoing-key";

BWRateLimiterInterface::Ptr RateLimiterManager::getRateLimiter(const std::string& _rateLimiterKey)
{
    std::shared_lock lock(x_rateLimiters);
    auto it = m_rateLimiters.find(_rateLimiterKey);
    if (it != m_rateLimiters.end())
    {
        return it->second;
    }

    return nullptr;
}

bool RateLimiterManager::registerRateLimiter(
    const std::string& _rateLimiterKey, BWRateLimiterInterface::Ptr _rateLimiter)
{
    if (getGroupRateLimiter(_rateLimiterKey))
    {
        return false;
    }

    RATELIMIT_LOG(INFO) << LOG_BADGE("registerRateLimiter")
                        << LOG_KV("rateLimiterKey", _rateLimiterKey);

    std::unique_lock lock(x_rateLimiters);
    auto result = m_rateLimiters.try_emplace(_rateLimiterKey, _rateLimiter);
    return result.second;
}

bool RateLimiterManager::removeRateLimiter(const std::string& _rateLimiterKey)
{
    RATELIMIT_LOG(INFO) << LOG_BADGE("removeRateLimiter")
                        << LOG_KV("rateLimiterKey", _rateLimiterKey);

    std::unique_lock lock(x_rateLimiters);
    return m_rateLimiters.erase(_rateLimiterKey) > 0;
}

BWRateLimiterInterface::Ptr RateLimiterManager::ensureRateLimiterExist(
    const std::string& _rateLimiterKey, int64_t _maxPermits)
{
    // ratelimiter exist
    auto rateLimiter = getRateLimiter(_rateLimiterKey);
    if (rateLimiter)
    {
        return rateLimiter;
    }

    RATELIMIT_LOG(INFO) << LOG_BADGE("ensureRateLimiterExist")
                        << LOG_KV("rateLimiterKey", _rateLimiterKey)
                        << LOG_KV("maxPermits", _maxPermits);

    // create ratelimiter
    rateLimiter = m_rateLimiterFactory->buildRateLimiter(_maxPermits);

    {
        std::unique_lock lock(x_rateLimiters);
        auto it = m_rateLimiters.find(_rateLimiterKey);
        if (it != m_rateLimiters.end())
        {
            rateLimiter = it->second;
        }
        else
        {
            m_rateLimiters[_rateLimiterKey] = rateLimiter;
        }
    }

    return rateLimiter;
}

bool RateLimiterManager::registerGroupRateLimiter(
    const std::string& _group, BWRateLimiterInterface::Ptr _rateLimiter)
{
    return registerRateLimiter("group-" + _group, _rateLimiter);
}

bool RateLimiterManager::removeGroupRateLimiter(const std::string& _group)
{
    return removeRateLimiter("group-" + _group);
}

BWRateLimiterInterface::Ptr RateLimiterManager::getGroupRateLimiter(const std::string& _group)
{
    std::string rateLimiterKey = "group-" + _group;

    auto rateLimiter = getRateLimiter(rateLimiterKey);

    if (rateLimiter == nullptr)
    {
        int64_t groupOutgoingBwLimit = -1;

        auto it = m_rateLimitConfig.group2BwLimit.find(_group);
        if (it != m_rateLimitConfig.group2BwLimit.end())
        {
            groupOutgoingBwLimit = it->second;
        }
        else if (m_rateLimitConfig.groupOutgoingBwLimit > 0)
        {
            groupOutgoingBwLimit = m_rateLimitConfig.groupOutgoingBwLimit;
        }

        if (groupOutgoingBwLimit > 0)
        {
            rateLimiter = ensureRateLimiterExist(rateLimiterKey, groupOutgoingBwLimit);
        }
    }

    return rateLimiter;
}

bool RateLimiterManager::registerConnRateLimiter(
    const std::string& _connIP, BWRateLimiterInterface::Ptr _rateLimiter)
{
    return registerRateLimiter("ip-" + _connIP, _rateLimiter);
}

bool RateLimiterManager::removeConnRateLimiter(const std::string& _connIP)
{
    return removeRateLimiter("ip-" + _connIP);
}

BWRateLimiterInterface::Ptr RateLimiterManager::getConnRateLimiter(const std::string& _connIP)
{
    std::string rateLimiterKey = "ip-" + _connIP;

    auto rateLimiter = getRateLimiter(rateLimiterKey);

    if (rateLimiter == nullptr && m_rateLimitConfig.connOutgoingBwLimit > 0)
    {
        int64_t connOutgoingBwLimit = -1;

        auto it = m_rateLimitConfig.ip2BwLimit.find(_connIP);
        if (it != m_rateLimitConfig.ip2BwLimit.end())
        {
            connOutgoingBwLimit = it->second;
        }
        else if (m_rateLimitConfig.connOutgoingBwLimit > 0)
        {
            connOutgoingBwLimit = m_rateLimitConfig.connOutgoingBwLimit;
        }

        if (connOutgoingBwLimit > 0)
        {
            rateLimiter = ensureRateLimiterExist(rateLimiterKey, connOutgoingBwLimit);
        }
    }

    return rateLimiter;
}