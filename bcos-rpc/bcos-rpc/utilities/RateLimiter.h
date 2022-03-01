/**
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
 * @file: RateLimiter.h
 * @author: yujiechen
 * @date: 2020-04-15
 */
#pragma once
#include <bcos-utilities/Common.h>

#define RATELIMIT_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("RateLimiter")

namespace bcos
{
class RateLimiter
{
public:
    using Ptr = std::shared_ptr<RateLimiter>;
    RateLimiter(int64_t const& _maxQPS);
    virtual ~RateLimiter() {}
    // acquire permits
    virtual int64_t acquire(int64_t const& _requiredPermits = 1, bool const& _wait = false,
        bool const& _fetchPermitsWhenRequireWait = false, int64_t const& _now = utcSteadyTimeUs());

    // acquire the permits without wait
    virtual int64_t acquireWithoutWait(int64_t _requiredPermits = 1)
    {
        return acquire(_requiredPermits, false, true);
    }

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

    int64_t const& maxQPS() { return m_maxQPS; }

protected:
    int64_t fetchPermitsAndGetWaitTime(int64_t const& _requiredPermits,
        bool const& _fetchPermitsWhenRequireWait, int64_t const& _now);
    void updatePermits(int64_t const& _now);

    void updateCurrentStoredPermits(int64_t const& _requiredPermits);

protected:
    mutable bcos::Mutex m_mutex;

    // the max QPS
    int64_t m_maxQPS;

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
}  // namespace bcos