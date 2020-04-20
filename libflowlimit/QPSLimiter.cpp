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
 * @brief : Implement of QPSLimiter
 * @file: QPSLimiter.cpp
 * @author: yujiechen
 * @date: 2020-04-15
 */
#include "QPSLimiter.h"
#include <thread>

using namespace dev;
using namespace dev::flowlimit;

QPSLimiter::QPSLimiter(uint64_t const& _maxQPS) : m_maxQPS(_maxQPS)
{
    m_permitsUpdateInterval = (double)1000000 / (double)m_maxQPS;
    m_lastPermitsUpdateTime = utcSteadyTimeUs();
    m_futureBrustResetTime = utcSteadyTimeUs() + m_burstTimeInterval;

    m_maxPermits = m_maxQPS * m_cumulativeStatInterval;
    QPSLIMIT_LOG(INFO) << LOG_DESC("create QPSLimiter")
                       << LOG_KV("permitsUpdateInterval", m_permitsUpdateInterval)
                       << LOG_KV("maxQPS", m_maxQPS) << LOG_KV("maxPermits", m_maxPermits);
}

void QPSLimiter::setBrustTimeInterval(int64_t const& _burstInterval)
{
    m_burstTimeInterval = _burstInterval;
    QPSLIMIT_LOG(INFO) << LOG_DESC("setBrustTimeInterval")
                       << LOG_KV("brustTimeInterval", m_burstTimeInterval);
}

void QPSLimiter::setMaxBrustReqNum(int64_t const& _maxBrustReqNum)
{
    m_maxBrustReqNum = _maxBrustReqNum;
    QPSLIMIT_LOG(INFO) << LOG_DESC("setMaxBrustReqNum")
                       << LOG_KV("maxBrustReqNum", m_maxBrustReqNum);
}

bool QPSLimiter::tryAcquire(uint64_t const& _requiredPermits)
{
    auto waitTime = acquire(_requiredPermits, false, false);
    return (waitTime == 0);
}

int64_t QPSLimiter::acquire(
    int64_t const& _requiredPermits, bool const& _wait, bool const& _fetchPermitsWhenRequireWait)
{
    int64_t waitTime = fetchPermitsAndGetWaitTime(_requiredPermits, _fetchPermitsWhenRequireWait);
    // if _wait is true(default is false): need sleep
    if (_wait && waitTime > 0)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(waitTime));
    }
    return waitTime;
}

bool QPSLimiter::acquireWithBrustSupported(uint64_t const& _requiredPermits)
{
    auto waitTime = fetchPermitsAndGetWaitTime(_requiredPermits, false);
    int64_t now = utcSteadyTimeUs();
    int64_t waitAvailableTime = now + waitTime;
    // has enough permits
    if (waitTime == 0)
    {
        return true;
    }
    // has no permits, determine brust now
    m_brustReqNum += _requiredPermits;
    bool reqAccepted = false;
    if (m_brustReqNum < m_maxBrustReqNum && waitAvailableTime < m_futureBrustResetTime)
    {
        reqAccepted = true;
        updateCurrentStoredPermits(_requiredPermits);
    }
    // without brust permits
    else
    {
        m_brustReqNum -= _requiredPermits;
    }
    // should update m_futureBrustResetTime
    if (now >= m_futureBrustResetTime)
    {
        m_brustReqNum = 0;
        m_futureBrustResetTime = m_burstTimeInterval + now;
    }
    return reqAccepted;
}

int64_t QPSLimiter::fetchPermitsAndGetWaitTime(
    int64_t const& _requiredPermits, bool const& _fetchPermitsWhenRequireWait)
{
    // has remaining permits, handle the request directly
    if (m_currentStoredPermits > _requiredPermits)
    {
        m_currentStoredPermits -= _requiredPermits;
        return 0;
    }
    auto now = utcSteadyTimeUs();
    Guard l(m_mutex);
    // update the permits
    updatePermits(now);
    int64_t waitAvailableTime = m_lastPermitsUpdateTime - now;
    // without wait: don't fetch permits after timeout
    // wait: fetch permits after timeout
    if (waitAvailableTime > 0 && !_fetchPermitsWhenRequireWait)
    {
        return waitAvailableTime;
    }
    updateCurrentStoredPermits(_requiredPermits);
    return std::max(waitAvailableTime, (int64_t)0);
}

void QPSLimiter::updateCurrentStoredPermits(int64_t const& _requiredPermits)
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

void QPSLimiter::updatePermits(int64_t const& _now)
{
    if (_now <= m_lastPermitsUpdateTime)
    {
        return;
    }
    int64_t increasedPremits = (double)(_now - m_lastPermitsUpdateTime) / m_permitsUpdateInterval;
    m_currentStoredPermits = std::min(m_maxPermits, m_currentStoredPermits + increasedPremits);
    // update last permits update time
    m_lastPermitsUpdateTime = _now;
}
