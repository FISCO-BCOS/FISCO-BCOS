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
 * @file: RateLimiter.h
 * @author: yujiechen
 * @date: 2020-04-15
 */
#pragma once
#include <libdevcore/Common.h>
#include <libdevcore/Guards.h>

#define RATELIMIT_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("RateLimiter")

namespace dev
{
namespace flowlimit
{
class RateLimiter
{
public:
    using Ptr = std::shared_ptr<RateLimiter>;
    RateLimiter(uint64_t const& _maxQPS);
    virtual ~RateLimiter() {}
    // acquire permits
    virtual int64_t acquire(int64_t const& _requiredPermits = 1, bool const& _wait = false,
        bool const& _fetchPermitsWhenRequireWait = false, int64_t const& _now = utcSteadyTimeUs());

    virtual bool tryAcquire(
        uint64_t const& _requiredPermits = 1, int64_t const& _now = utcSteadyTimeUs());
    virtual bool acquireWithBurstSupported(
        uint64_t const& _requiredPermits = 1, int64_t const& _now = utcSteadyTimeUs());

    void setMaxPermitsSize(int64_t const& _maxPermitsSize)
    {
        m_maxPermits = _maxPermitsSize;
        RATELIMIT_LOG(DEBUG) << LOG_DESC("setMaxPermitsSize")
                             << LOG_KV("maxPermitsSize", _maxPermitsSize);
    }

    void setBurstTimeInterval(int64_t const& _burstInterval);
    void setMaxBurstReqNum(int64_t const& _maxBurstReqNum);

protected:
    int64_t fetchPermitsAndGetWaitTime(int64_t const& _requiredPermits,
        bool const& _fetchPermitsWhenRequireWait, int64_t const& _now);
    void updatePermits(int64_t const& _now);

    void updateCurrentStoredPermits(int64_t const& _requiredPermits);

protected:
    mutable dev::Mutex m_mutex;

    // the max QPS
    uint64_t m_maxQPS;

    // stored permits
    int64_t m_currentStoredPermits = 0;

    // the interval time to update storedPermits
    double m_permitsUpdateInterval;
    int64_t m_lastPermitsUpdateTime;
    int64_t m_maxPermits = 0;

    std::atomic<int64_t> m_futureBurstResetTime;
    // the current burstReqNum, every m_burstTimeInterval is refreshed to 0
    std::atomic<int64_t> m_burstReqNum = {0};
    // the max burst num during m_burstTimeInterval
    int64_t m_maxBurstReqNum = 0;
    // default burst interval is 1s
    uint64_t m_burstTimeInterval = 1000000;
};
}  // namespace flowlimit
}  // namespace dev