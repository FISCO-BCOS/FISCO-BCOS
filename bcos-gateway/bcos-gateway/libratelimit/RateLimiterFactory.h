

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

#include <bcos-gateway/libratelimit/DistributedRateLimiter.h>
#include <bcos-gateway/libratelimit/RateLimiterInterface.h>
#include <bcos-gateway/libratelimit/TokenBucketRateLimiter.h>
#include <sw/redis++/redis++.h>
#include <cstddef>

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
    RateLimiterFactory(std::shared_ptr<sw::redis::Redis> _redis) : m_redis(_redis) {}

public:
    std::shared_ptr<sw::redis::Redis> redis() const { return m_redis; }

public:
    static std::string toTokenKey(const std::string& _baseKey)
    {
        return "FISCO-BCOS 3.0 Gateway RateLimiter: " + _baseKey;
    }

public:
    RateLimiterInterface::Ptr buildTokenBucketRateLimiter(int64_t _maxPermits)
    {
        auto rateLimiter = std::make_shared<TokenBucketRateLimiter>(_maxPermits);
        return rateLimiter;
    }

    RateLimiterInterface::Ptr buildRedisDistributedRateLimiter(const std::string& _key,
        int64_t _maxPermits, int32_t _interval, bool _enableCache, int32_t _cachePercent)
    {
        auto rateLimiter = std::make_shared<DistributedRateLimiter>(
            m_redis, _key, _maxPermits, _interval, _enableCache, _cachePercent);
        return rateLimiter;
    }

private:
    std::shared_ptr<sw::redis::Redis> m_redis = nullptr;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos