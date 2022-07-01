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
 * @brief : Implement of  BWRateLimiter
 * @file:  BWRateLimiter.cpp
 * @author: yujiechen
 * @date: 2020-04-15
 */
#include <bcos-gateway/Common.h>
#include <bcos-gateway/libratelimit/BWRateLimiter.h>
#include <thread>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimit;

BWRateLimiter::BWRateLimiter(int64_t _maxQPS)
  : m_maxQPS(_maxQPS),
    m_permitsUpdateInterval((double)1000000 / (double)m_maxQPS),
    m_lastPermitsUpdateTime(utcSteadyTimeUs()),
    m_maxPermits(m_maxQPS),
    m_futureBurstResetTime(m_lastPermitsUpdateTime + m_burstTimeInterval)
{
    RATELIMIT_LOG(INFO) << LOG_BADGE("[NEWOBJ][BWRateLimiter]")
                        << LOG_KV("permitsUpdateInterval", m_permitsUpdateInterval)
                        << LOG_KV("maxPermits", m_maxPermits);
}

void BWRateLimiter::setMaxPermitsSize(int64_t const& _maxPermitsSize)
{
    m_maxPermits = _maxPermitsSize;

    RATELIMIT_LOG(INFO) << LOG_BADGE("setMaxPermitsSize") << LOG_DESC("setMaxPermitsSize")
                        << LOG_KV("maxPermitsSize", m_maxPermits);
}

void BWRateLimiter::setBurstTimeInterval(int64_t const& _burstInterval)
{
    m_burstTimeInterval = _burstInterval;

    RATELIMIT_LOG(INFO) << LOG_BADGE("setBurstTimeInterval")
                        << LOG_KV("burstTimeInterval", m_burstTimeInterval);
}

void BWRateLimiter::setMaxBurstReqNum(int64_t const& _maxBurstReqNum)
{
    m_maxBurstReqNum = _maxBurstReqNum;

    RATELIMIT_LOG(INFO) << LOG_BADGE("setMaxBurstReqNum")
                        << LOG_KV("maxBurstReqNum", m_maxBurstReqNum);
}

bool BWRateLimiter::tryAcquire(int64_t _requiredPermits)
{
    int64_t waitTime = fetchPermitsAndGetWaitTime(_requiredPermits, false, utcSteadyTimeUs());
    return (waitTime == 0);
}

void BWRateLimiter::acquire(int64_t _requiredPermits)
{
    int64_t waitTime = fetchPermitsAndGetWaitTime(_requiredPermits, false, utcSteadyTimeUs());
    if (waitTime > 0)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(waitTime));
    }
    return;
}

int64_t BWRateLimiter::acquireWithoutWait(int64_t _requiredPermits)
{
    int64_t waitTime = fetchPermitsAndGetWaitTime(_requiredPermits, false, utcSteadyTimeUs());
    return waitTime;
}

/**
 * @brief
 *
 * @return int64_t
 */
void BWRateLimiter::rollback(int64_t _requiredPermits)
{
    Guard l(m_mutex);
    m_currentStoredPermits += _requiredPermits;
    if (m_currentStoredPermits > m_maxPermits)
    {
        m_currentStoredPermits = m_maxPermits;
    }
    return;
}

int64_t BWRateLimiter::fetchPermitsAndGetWaitTime(
    int64_t _requiredPermits, bool _fetchPermitsWhenRequireWait, int64_t _now)
{
    Guard l(m_mutex);
    // has remaining permits, handle the request directly
    if (m_currentStoredPermits > _requiredPermits)
    {
        m_currentStoredPermits -= _requiredPermits;
        return 0;
    }

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

void BWRateLimiter::updateCurrentStoredPermits(int64_t _requiredPermits)
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

void BWRateLimiter::updatePermits(int64_t _now)
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
