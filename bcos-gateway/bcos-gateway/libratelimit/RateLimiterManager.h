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
 * @file GroupBWRateLimiter.h
 * @author: octopus
 * @date 2022-06-30
 */

#pragma once

#include "bcos-gateway/libratelimit/BWRateLimiter.h"
#include "bcos-gateway/libratelimit/ModuleWhiteList.h"
#include <bcos-gateway/GatewayConfig.h>
#include <bcos-utilities/Common.h>
#include <shared_mutex>
#include <unordered_map>

namespace bcos
{
namespace gateway
{
namespace ratelimit
{

class RateLimiterManager
{
public:
    using Ptr = std::shared_ptr<RateLimiterManager>;
    using ConstPtr = std::shared_ptr<const RateLimiterManager>;
    using UniquePtr = std::unique_ptr<RateLimiterManager>;

public:
    const static std::string TOTAL_OUTGOING_KEY;

public:
    RateLimiterManager(const GatewayConfig::RateLimitConfig& _rateLimitConfig)
      : m_rateLimitConfig(_rateLimitConfig)
    {}

public:
    BWRateLimiterInterface::Ptr getRateLimiter(const std::string& _rateLimiterKey);

    bool registerRateLimiter(
        const std::string& _rateLimiterKey, BWRateLimiterInterface::Ptr _rateLimiter);
    bool removeRateLimiter(const std::string& _rateLimiterKey);

    BWRateLimiterInterface::Ptr ensureRateLimiterExist(
        const std::string& _rateLimiterKey, int64_t _maxPermit);

public:
    bool registerGroupRateLimiter(
        const std::string& _group, BWRateLimiterInterface::Ptr _rateLimiter);

    bool removeGroupRateLimiter(const std::string& _group);

    BWRateLimiterInterface::Ptr getGroupRateLimiter(const std::string& _group);

    bool registerConnRateLimiter(
        const std::string& _connIP, BWRateLimiterInterface::Ptr _rateLimiter);

    bool removeConnRateLimiter(const std::string& _connIP);

    BWRateLimiterInterface::Ptr getConnRateLimiter(const std::string& _connIP);

public:
    ratelimit::BWRateLimiterFactory::Ptr rateLimiterFactory() const { return m_rateLimiterFactory; }
    void setRateLimiterFactory(ratelimit::BWRateLimiterFactory::Ptr _rateLimiterFactory)
    {
        m_rateLimiterFactory = _rateLimiterFactory;
    }

    const std::set<uint16_t>& modulesWithNoBwLimit() const { return m_modulesWithNoBwLimit; }
    void setModulesWithNoBwLimit(std::set<uint16_t> _modulesWithNoBwLimit)
    {
        m_modulesWithNoBwLimit = std::move(_modulesWithNoBwLimit);
    }

private:
    //   factory for BWRateLimiterInterface
    ratelimit::BWRateLimiterFactory::Ptr m_rateLimiterFactory;

    // lock for m_group2RateLimiter
    mutable std::shared_mutex x_rateLimiters;
    // group/ip => ratelimiter
    std::unordered_map<std::string, BWRateLimiterInterface::Ptr> m_rateLimiters;

    // the message of modules that do not limit bandwidth
    std::set<uint16_t> m_modulesWithNoBwLimit;

    GatewayConfig::RateLimitConfig m_rateLimitConfig;
};

}  // namespace ratelimit
}  // namespace gateway
}  // namespace bcos
