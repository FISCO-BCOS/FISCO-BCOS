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
 * @brief : Implement of RateLimiter
 * @file: RateLimiter.cpp
 * @author: yujiechen
 * @date: 2020-04-15
 */
#include "RateLimiter.h"
#include <thread>

using namespace dev;
using namespace dev::flowlimit;

RateLimiter::RateLimiter(uint64_t const& _maxQPS) : m_maxQPS(_maxQPS)
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
    // without wait: don't fetch permits after timeout
    // wait: fetch permits after timeout
    if (waitAvailableTime > 0 && !_fetchPermitsWhenRequireWait)
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
