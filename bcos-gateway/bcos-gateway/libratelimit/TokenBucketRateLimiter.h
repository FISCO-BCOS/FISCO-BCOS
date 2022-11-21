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
 * @brief : Implement of TokenBucketRateLimiter
 * @file: TokenBucketRateLimiter.h
 * @author: yujiechen
 * @date: 2020-04-15
 */
#pragma once

#include <bcos-gateway/libratelimit/RateLimiterInterface.h>
#include <bcos-utilities/Common.h>

namespace bcos
{
namespace gateway
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
    void acquire(int64_t _requiredPermits) override;

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
}  // namespace gateway
}  // namespace bcos