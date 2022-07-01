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
#include <bcos-gateway/libratelimit/GroupBWRateLimiter.h>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimit;

/**
 * @brief
 *
 * @param _group
 * @param _moduleID
 * @param _requiredPermits
 */
void GroupBWRateLimiter::acquire(
    const std::string& _group, uint16_t _moduleID, int64_t _requiredPermits)
{
    if (m_moduleWhiteList.isModuleExist(_moduleID))
    {
        // This module message does not limit traffic
        return;
    }

    auto ratelimiter = getGroupRateLimiter(_group);
    if (!ratelimiter)
    {
        // Message of the group does not limit traffic
        return;
    }

    ratelimiter->acquire(_requiredPermits);
}

/**
 * @brief
 *
 * @param _group
 * @param _moduleID
 * @param _requiredPermits
 * @return true
 * @return false
 */
bool GroupBWRateLimiter::tryAcquire(
    const std::string& _group, uint16_t _moduleID, int64_t _requiredPermits)
{
    if (m_moduleWhiteList.isModuleExist(_moduleID))
    {
        // This module message does not limit traffic
        return true;
    }

    auto ratelimiter = getGroupRateLimiter(_group);
    if (!ratelimiter)
    {
        // Message of the group does not limit traffic
        return true;
    }

    return ratelimiter->tryAcquire(_requiredPermits);
}

/**
 * @brief
 *
 * @param _group
 * @param _moduleID
 * @param _requiredPermits
 * @return int64_t
 */
int64_t GroupBWRateLimiter::acquireWithoutWait(
    const std::string& _group, uint16_t _moduleID, int64_t _requiredPermits)
{
    if (m_moduleWhiteList.isModuleExist(_moduleID))
    {
        // This module message does not limit traffic
        return 0;
    }

    auto ratelimiter = getGroupRateLimiter(_group);
    if (!ratelimiter)
    {
        // Message of the group does not limit traffic
        return 0;
    }

    return ratelimiter->acquireWithoutWait(_requiredPermits);
}

BWRateLimiterInterface::Ptr GroupBWRateLimiter::getGroupRateLimiter(const std::string& _group)
{
    auto it = m_group2RateLimiter.find(_group);
    if (it != m_group2RateLimiter.end())
    {
        return it->second;
    }

    return nullptr;
}

bool GroupBWRateLimiter::registerGroupRateLimiter(
    const std::string& _group, BWRateLimiterInterface::Ptr _rateLimiter)
{
    auto result = m_group2RateLimiter.try_emplace(_group, _rateLimiter);

    RATELIMIT_LOG(INFO) << LOG_BADGE("registerGroupRateLimiter") << LOG_KV("group", _group)
                        << LOG_KV("result", result.second);

    return result.second;
}

bool GroupBWRateLimiter::removeGroupRateLimiter(const std::string& _group)
{
    RATELIMIT_LOG(INFO) << LOG_BADGE("removeGroupRateLimiter") << LOG_KV("group", _group);

    return m_group2RateLimiter.erase(_group) > 0;
}
