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
 * @brief : Implement of TimeWindowRateLimiter
 * @file: TimeWindowRateLimiter.h
 * @author: octopuswang
 * @date: 2023-01-30
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/ObjectCounter.h>
#include <bcos-utilities/ratelimiter/RateLimiterInterface.h>
#include <mutex>

namespace bcos
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
}  // namespace bcos