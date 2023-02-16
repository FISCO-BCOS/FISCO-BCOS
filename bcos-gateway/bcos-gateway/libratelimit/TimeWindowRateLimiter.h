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
 * @brief : Implement of TimeWindowRateLimiter
 * @file: TimeWindowRateLimiter.h
 * @author: octopuswang
 * @date: 2023-01-30
 */
#pragma once

#include "bcos-gateway/Common.h"
#include <bcos-gateway/libratelimit/RateLimiterInterface.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ObjectCounter.h>
#include <mutex>

namespace bcos
{
namespace gateway
{
namespace ratelimiter
{

class TimeWindowRateLimiter : public RateLimiterInterface,
                              public bcos::ObjectCounter<TimeWindowRateLimiter>
{
public:
    using Ptr = std::shared_ptr<TimeWindowRateLimiter>;
    using ConstPtr = std::shared_ptr<const TimeWindowRateLimiter>;
    using UniquePtr = std::unique_ptr<const TimeWindowRateLimiter>;

public:
    TimeWindowRateLimiter(uint64_t _maxPermitsSize, uint64_t _timeWindowMS = 1000,
        bool _allowExceedMaxPermitSize = false)
      : m_maxPermitsSize(_maxPermitsSize),
        m_allowExceedMaxPermitSize(_allowExceedMaxPermitSize),
        m_currentPermitsSize(m_maxPermitsSize),
        m_timeWindowMS(_timeWindowMS),
        m_lastPermitsUpdateTime(utcSteadyTime())
    {
        RATELIMIT_LOG(INFO) << LOG_BADGE("[NEWOBJ][TimeWindowRateLimiter]")
                            << LOG_KV("maxPermitsSize", _maxPermitsSize)
                            << LOG_KV("allowExceedMaxPermitSize", _allowExceedMaxPermitSize)
                            << LOG_KV("timeWindowMS", _timeWindowMS);
    }

    TimeWindowRateLimiter(TimeWindowRateLimiter&&) = delete;
    TimeWindowRateLimiter(const TimeWindowRateLimiter&) = delete;
    TimeWindowRateLimiter& operator=(const TimeWindowRateLimiter&) = delete;
    TimeWindowRateLimiter& operator=(TimeWindowRateLimiter&&) = delete;

    ~TimeWindowRateLimiter() override = default;

    uint64_t maxPermitsSize() const { return m_maxPermitsSize; }
    uint64_t currentPermitsSize() const { return m_currentPermitsSize; }
    uint64_t timeWindowMS() const { return m_timeWindowMS; }
    bool allowExceedMaxPermitSize() const { return m_allowExceedMaxPermitSize; }

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


private:
    // lock for all data class member
    std::mutex m_mutex;
    //
    uint64_t m_maxPermitsSize = 0;
    //
    bool m_allowExceedMaxPermitSize = false;
    // stored permits
    uint64_t m_currentPermitsSize = 0;
    // default time window interval is 1s
    uint64_t m_timeWindowMS = 1000;
    //
    uint64_t m_lastPermitsUpdateTime;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos