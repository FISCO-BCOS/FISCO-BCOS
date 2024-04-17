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
 */
/**
 * @brief : Implement of  TimeWindowRateLimiter
 * @file:  TimeWindowRateLimiter.cpp
 * @author: octopuswang
 * @date: 2023-01-30
 */
#include "bcos-utilities/BoostLog.h"
#include <bcos-utilities/ratelimiter/TimeWindowRateLimiter.h>
#include <chrono>
#include <thread>
#include <utility>

using namespace bcos;
using namespace bcos::ratelimiter;

bool TimeWindowRateLimiter::tryAcquire(int64_t _requiredPermits)
{
    if (std::cmp_greater(_requiredPermits, m_maxPermitsSize))
    {
        if (m_allowExceedMaxPermitSize)
        {
            return true;
        }

        // Notice: the acquire amount exceeded the maximum, it will never succeed
        RATELIMIT_LOG(WARNING) << LOG_DESC("try acquire exceeded the maximum")
                               << LOG_KV("requiredPermits", _requiredPermits)
                               << LOG_KV("maxPermitsSize", m_maxPermitsSize);
        return false;
    }

    Guard guard(m_mutex);
    auto nowTimeMS = utcSteadyTime();
    if (nowTimeMS - m_lastPermitsUpdateTime >= m_timeWindowMS)
    {
        // Reset the count info of the new time window
        m_lastPermitsUpdateTime = nowTimeMS;
        m_currentPermitsSize = m_maxPermitsSize;
    }

    if (std::cmp_greater_equal(m_currentPermitsSize, _requiredPermits))
    {
        m_currentPermitsSize -= _requiredPermits;
        return true;
    }

    return false;
}

bool TimeWindowRateLimiter::acquire(int64_t _requiredPermits)
{
    if (std::cmp_greater(_requiredPermits, m_maxPermitsSize))
    {
        if (m_allowExceedMaxPermitSize)
        {
            return true;
        }

        // Notice: the acquire amount exceeded the maximum, it will never succeed
        RATELIMIT_LOG(WARNING) << LOG_DESC("acquire exceeded the maximum")
                               << LOG_KV("requiredPermits", _requiredPermits)
                               << LOG_KV("maxPermitsSize", m_maxPermitsSize);
        return false;
    }

    while (!tryAcquire(_requiredPermits))
    {
        // sleep for tryAcquire success
        auto nowTimeMS = utcSteadyTime();

        auto tempTimeMS = nowTimeMS - m_lastPermitsUpdateTime;
        if (tempTimeMS > 0 && tempTimeMS < m_timeWindowMS)
        {
            auto sleepMS = m_timeWindowMS - tempTimeMS;
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMS));
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return true;
}

void TimeWindowRateLimiter::rollback(int64_t _requiredPermits)
{
    Guard guard(m_mutex);
    m_currentPermitsSize += _requiredPermits;
    if (m_currentPermitsSize > m_maxPermitsSize)
    {
        m_currentPermitsSize = m_maxPermitsSize;
    }
}
