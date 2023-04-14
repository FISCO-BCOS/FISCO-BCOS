/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @brief : Implement of  TimeWindowRateLimiter
 * @file:  TimeWindowRateLimiter.cpp
 * @author: octopuswang
 * @date: 2023-01-30
 */
#include "bcos-gateway/Common.h"
#include "bcos-utilities/BoostLog.h"
#include <bcos-gateway/libratelimit/TimeWindowRateLimiter.h>
#include <chrono>
#include <thread>
#include <utility>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimiter;

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
