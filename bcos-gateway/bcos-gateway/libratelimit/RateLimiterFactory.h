

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

#include "bcos-utilities/ratelimiter/DistributedRateLimiter.h"
#include "bcos-utilities/ratelimiter/RateLimiterInterface.h"
#include "bcos-utilities/ratelimiter/TimeWindowRateLimiter.h"
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

public:
    RateLimiterFactory() = default;
    RateLimiterFactory(std::shared_ptr<sw::redis::Redis> _redis);
    std::shared_ptr<sw::redis::Redis> redis() const;

    static std::string toTokenKey(const std::string& _baseKey);

    // time window rate limiter
    bcos::ratelimiter::RateLimiterInterface::Ptr buildTimeWindowRateLimiter(
        int64_t _maxPermits, int32_t _timeWindowMS = 1000, bool _allowExceedMaxPermitSize = false);

    // redis distributed rate limiter
    bcos::ratelimiter::RateLimiterInterface::Ptr buildDistributedRateLimiter(
        const std::string& _distributedKey, int64_t _maxPermitsSize, int32_t _intervalSec,
        bool _allowExceedMaxPermitSize, bool _enableLocalCache, int32_t _localCachePercent);

private:
    std::shared_ptr<sw::redis::Redis> m_redis = nullptr;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos