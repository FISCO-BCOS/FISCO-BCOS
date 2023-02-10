

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
 * @file RateLimiterFactory.h
 * @author: octopus
 * @date 2022-09-30
 */

#pragma once

#include "bcos-gateway/libratelimit/TimeWindowRateLimiter.h"
#include "bcos-utilities/BoostLog.h"
#include <bcos-gateway/libratelimit/DistributedRateLimiter.h>
#include <bcos-gateway/libratelimit/RateLimiterInterface.h>
#include <bcos-gateway/libratelimit/TokenBucketRateLimiter.h>
#include <sw/redis++/redis++.h>

namespace bcos
{
namespace gateway
{
namespace ratelimiter
{

class RateLimiterFactory
{
public:
    using Ptr = std::shared_ptr<RateLimiterFactory>;
    using ConstPtr = std::shared_ptr<const RateLimiterFactory>;
    using UniquePtr = std::unique_ptr<const RateLimiterFactory>;

    // constructor
    RateLimiterFactory() = default;
    RateLimiterFactory(const RateLimiterFactory&) = delete;
    RateLimiterFactory(RateLimiterFactory&&) = delete;
    RateLimiterFactory& operator=(const RateLimiterFactory&) = delete;
    RateLimiterFactory& operator=(RateLimiterFactory&&) = delete;
    ~RateLimiterFactory() = default;

    // redis interface, for distributed rate limiter
    void setRedis(std::shared_ptr<sw::redis::Redis>& _redis) { m_redis = _redis; }
    std::shared_ptr<sw::redis::Redis> redis() const { return m_redis; }

    static std::string toTokenKey(const std::string& _baseKey)
    {
        return "FISCO-BCOS 3.0 Gateway RateLimiter: " + _baseKey;
    }

    // time window rate limiter
    RateLimiterInterface::Ptr buildTimeWindowRateLimiter(
        int64_t _maxPermits, int32_t _timeWindowMS = 1000, bool _allowExceedMaxPermitSize = false)
    {
        BCOS_LOG(INFO) << LOG_BADGE("buildTimeWindowRateLimiter")
                       << LOG_KV("maxPermits", _maxPermits) << LOG_KV("timeWindowMS", _timeWindowMS)
                       << LOG_KV("allowExceedMaxPermitSize", _allowExceedMaxPermitSize);

        auto rateLimiter = std::make_shared<TimeWindowRateLimiter>(
            _maxPermits, _timeWindowMS, _allowExceedMaxPermitSize);
        (void)m_redis;
        return rateLimiter;
    }

    // token bucket rate limiter
    [[deprecated("use buildTimeWindowRateLimiter")]] RateLimiterInterface::Ptr
    buildTokenBucketRateLimiter(int64_t _maxPermits)
    {
        BCOS_LOG(INFO) << LOG_BADGE("buildTokenBucketRateLimiter")
                       << LOG_KV("maxPermits", _maxPermits);

        auto rateLimiter = std::make_shared<TokenBucketRateLimiter>(_maxPermits);
        (void)m_redis;
        return rateLimiter;
    }

    // redis distributed rate limiter
    RateLimiterInterface::Ptr buildDistributedRateLimiter(const std::string& _distributedKey,
        int64_t _maxPermitsSize, int32_t _intervalSec, bool _allowExceedMaxPermitSize,
        bool _enableLocalCache, int32_t _localCachePercent)
    {
        BCOS_LOG(INFO) << LOG_BADGE("buildDistributedRateLimiter")
                       << LOG_KV("distributedKey", _distributedKey)
                       << LOG_KV("maxPermitsSize", _maxPermitsSize)
                       << LOG_KV("allowExceedMaxPermitSize", _allowExceedMaxPermitSize)
                       << LOG_KV("intervalSec", _intervalSec)
                       << LOG_KV("enableLocalCache", _enableLocalCache)
                       << LOG_KV("localCachePercent", _localCachePercent);

        auto rateLimiter =
            std::make_shared<DistributedRateLimiter>(m_redis, _distributedKey, _maxPermitsSize,
                _allowExceedMaxPermitSize, _intervalSec, _enableLocalCache, _localCachePercent);
        return rateLimiter;
    }

private:
    std::shared_ptr<sw::redis::Redis> m_redis = nullptr;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos