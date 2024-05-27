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
 * @brief : Implement of TokenBucketRateLimiter
 * @file: TokenBucketRateLimiter.h
 * @author: yujiechen
 * @date: 2020-04-15
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/ratelimiter/RateLimiterInterface.h>

namespace bcos
{
namespace ratelimiter
{

class TokenBucketRateLimiter : public RateLimiterInterface
{
public:
    using Ptr = std::shared_ptr<TokenBucketRateLimiter>;
    using ConstPtr = std::shared_ptr<const TokenBucketRateLimiter>;
    using UniquePtr = std::unique_ptr<const TokenBucketRateLimiter>;

public:
    TokenBucketRateLimiter(int64_t _maxQPS);

    TokenBucketRateLimiter(TokenBucketRateLimiter&&) = delete;
    TokenBucketRateLimiter(const TokenBucketRateLimiter&) = delete;
    TokenBucketRateLimiter& operator=(const TokenBucketRateLimiter&) = delete;
    TokenBucketRateLimiter& operator=(TokenBucketRateLimiter&&) = delete;

    ~TokenBucketRateLimiter() override = default;

public:
    /**
     * @brief
     *
     * @param _requiredPermits
     */
    bool acquire(int64_t _requiredPermits) override;

    /**
     * @brief
     *
     * @param _requiredPermits
     * @return true
     * @return false
     */
    bool tryAcquire(int64_t _requiredPermits) override;

    /**
     * @brief
     *
     * @return
     */
    void rollback(int64_t _requiredPermits) override;

public:
    int64_t maxQPS() const { return m_maxQPS; }

    void setMaxPermitsSize(int64_t const& _maxPermitsSize);
    void setBurstTimeInterval(int64_t const& _burstInterval);
    void setMaxBurstReqNum(int64_t const& _maxBurstReqNum);

protected:
    int64_t fetchPermitsAndGetWaitTime(
        int64_t _requiredPermits, bool _fetchPermitsWhenRequireWait, int64_t _now);

    void updatePermits(int64_t _now);

    void updateCurrentStoredPermits(int64_t _requiredPermits);

private:
    mutable bcos::Mutex m_mutex;

    // the max QPS
    int64_t m_maxQPS;

    // stored permits
    std::atomic<int64_t> m_currentStoredPermits = 0;

    // the interval time to update storedPermits
    double m_permitsUpdateInterval;
    int64_t m_lastPermitsUpdateTime;
    int64_t m_maxPermits = 0;

    // the current burstReqNum, every m_burstTimeInterval is refreshed to 0
    std::atomic<int64_t> m_burstReqNum = {0};
    // the max burst num during m_burstTimeInterval
    int64_t m_maxBurstReqNum = 0;
    // default burst interval is 1s
    uint64_t m_burstTimeInterval = 1000000;
    std::atomic<int64_t> m_futureBurstResetTime;
};

}  // namespace ratelimiter
}  // namespace bcos