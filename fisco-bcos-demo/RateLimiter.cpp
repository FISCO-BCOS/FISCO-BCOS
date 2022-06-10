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
 * @brief : Implement of RateLimiter
 * @file: RateLimiter.cpp
 * @author: yujiechen
 * @date: 2020-04-15
 */
#include "RateLimiter.h"
#include <thread>

using namespace bcos;

RateLimiter::RateLimiter(int64_t const& _maxQPS) : m_maxQPS(_maxQPS)
{
    m_permitsUpdateInterval = (double)1000000 / (double)m_maxQPS;
    m_lastPermitsUpdateTime = utcSteadyTimeUs();
    m_futureBurstResetTime = utcSteadyTimeUs() + m_burstTimeInterval;

    m_maxPermits = m_maxQPS;
    RATELIMIT_LOG(INFO) << LOG_DESC("create RateLimiter")
                        << LOG_KV("permitsUpdateInterval", m_permitsUpdateInterval)
                        << LOG_KV("maxQPS", m_maxQPS) << LOG_KV("maxPermits", m_maxPermits);
}

void RateLimiter::setBurstTimeInterval(int64_t const& _burstInterval)
{
    m_burstTimeInterval = _burstInterval;
    RATELIMIT_LOG(INFO) << LOG_DESC("setBurstTimeInterval")
                        << LOG_KV("burstTimeInterval", m_burstTimeInterval);
}

void RateLimiter::setMaxBurstReqNum(int64_t const& _maxBurstReqNum)
{
    m_maxBurstReqNum = _maxBurstReqNum;
    RATELIMIT_LOG(INFO) << LOG_DESC("setMaxBurstReqNum")
                        << LOG_KV("maxBurstReqNum", m_maxBurstReqNum);
}

bool RateLimiter::tryAcquire(uint64_t const& _requiredPermits, int64_t const& _now)
{
    auto waitTime = acquire(_requiredPermits, false, false, _now);
    return (waitTime == 0);
}

int64_t RateLimiter::acquire(int64_t const& _requiredPermits, bool const& _wait,
    bool const& _fetchPermitsWhenRequireWait, int64_t const& _now)
{
    int64_t waitTime =
        fetchPermitsAndGetWaitTime(_requiredPermits, _fetchPermitsWhenRequireWait, _now);
    // if _wait is true(default is false): need sleep
    if (_wait && waitTime > 0)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(waitTime));
    }
    return waitTime;
}

bool RateLimiter::acquireWithBurstSupported(uint64_t const& _requiredPermits, int64_t const& _now)
{
    auto waitTime = fetchPermitsAndGetWaitTime(_requiredPermits, false, _now);
    int64_t waitAvailableTime = _now + waitTime;
    // has enough permits
    if (waitTime == 0)
    {
        return true;
    }

    // should update m_futureBurstResetTime
    if (_now >= m_futureBurstResetTime)
    {
        m_burstReqNum = 0;
        m_futureBurstResetTime = _now + m_burstTimeInterval;
    }
    // has no permits, determine burst now
    m_burstReqNum += _requiredPermits;
    bool reqAccepted = false;
    if (m_burstReqNum <= m_maxBurstReqNum && waitAvailableTime < m_futureBurstResetTime)
    {
        reqAccepted = true;
    }
    // without burst permits
    else
    {
        m_burstReqNum -= _requiredPermits;
    }
    return reqAccepted;
}

int64_t RateLimiter::fetchPermitsAndGetWaitTime(
    int64_t const& _requiredPermits, bool const& _fetchPermitsWhenRequireWait, int64_t const& _now)
{
    // has remaining permits, handle the request directly
    if (m_currentStoredPermits > _requiredPermits)
    {
        m_currentStoredPermits -= _requiredPermits;
        return 0;
    }
    Guard l(m_mutex);
    // update the permits
    updatePermits(_now);
    int64_t waitAvailableTime = m_lastPermitsUpdateTime - _now;
    // _fetchPermitsWhenRequireWait is false: don't fetch permits after timeout
    // _fetchPermitsWhenRequireWait is true: fetch permits after timeout
    if (!_fetchPermitsWhenRequireWait)
    {
        if (waitAvailableTime > 0)
        {
            return waitAvailableTime;
        }
        // Only permits of m_maxQPS can be used in advance
        if ((_requiredPermits - m_currentStoredPermits) >= m_maxQPS)
        {
            // Indicates that the permits was not obtained
            return 1;
        }
    }
    if ((waitAvailableTime > 0 || (_requiredPermits - m_currentStoredPermits) >= m_maxQPS) &&
        !_fetchPermitsWhenRequireWait)
    {
        return waitAvailableTime;
    }
    updateCurrentStoredPermits(_requiredPermits);
    return std::max(waitAvailableTime, (int64_t)0);
}

void RateLimiter::updateCurrentStoredPermits(int64_t const& _requiredPermits)
{
    double waitTime = 0;

    if (_requiredPermits > m_currentStoredPermits)
    {
        waitTime = (_requiredPermits - m_currentStoredPermits) * m_permitsUpdateInterval;
        m_currentStoredPermits = 0;
    }
    else
    {
        m_currentStoredPermits -= _requiredPermits;
    }
    if (waitTime > 0)
    {
        m_lastPermitsUpdateTime += (int64_t)(waitTime);
    }
}

void RateLimiter::updatePermits(int64_t const& _now)
{
    if (_now <= m_lastPermitsUpdateTime)
    {
        return;
    }
    int64_t increasedPermits = (double)(_now - m_lastPermitsUpdateTime) / m_permitsUpdateInterval;
    m_currentStoredPermits = std::min(m_maxPermits, m_currentStoredPermits + increasedPermits);
    // update last permits update time
    if (m_currentStoredPermits == m_maxPermits)
    {
        m_lastPermitsUpdateTime = _now;
    }
    else
    {
        m_lastPermitsUpdateTime += increasedPermits * m_permitsUpdateInterval;
    }
}
