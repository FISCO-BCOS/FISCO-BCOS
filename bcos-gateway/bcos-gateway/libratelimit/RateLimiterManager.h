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
 * @file RateLimiterManager.h
 * @author: octopus
 * @date 2022-06-30
 */

#pragma once

#include "bcos-gateway/libratelimit/ModuleWhiteList.h"
#include "bcos-gateway/libratelimit/RateLimiterFactory.h"
#include <bcos-gateway/GatewayConfig.h>
#include <bcos-utilities/Common.h>
#include <shared_mutex>
#include <unordered_map>

namespace bcos
{
namespace gateway
{
namespace ratelimiter
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
    RateLimiterManager(const GatewayConfig::RateLimiterConfig& _rateLimiterConfig)
      : m_rateLimiterConfig(_rateLimiterConfig)
    {}

public:
    RateLimiterInterface::Ptr getRateLimiter(const std::string& _rateLimiterKey);

    bool registerRateLimiter(
        const std::string& _rateLimiterKey, RateLimiterInterface::Ptr _rateLimiter);
    bool removeRateLimiter(const std::string& _rateLimiterKey);

    RateLimiterInterface::Ptr getGroupRateLimiter(const std::string& _group);
    RateLimiterInterface::Ptr getConnRateLimiter(const std::string& _connIP);

public:
    ratelimiter::RateLimiterFactory::Ptr rateLimiterFactory() const { return m_rateLimiterFactory; }
    void setRateLimiterFactory(ratelimiter::RateLimiterFactory::Ptr& _rateLimiterFactory)
    {
        m_rateLimiterFactory = _rateLimiterFactory;
    }

    const std::set<uint16_t>& modulesWithoutLimit() const { return m_modulesWithoutLimit; }
    void setModulesWithoutLimit(std::set<uint16_t> _modulesWithoutLimit)
    {
        m_modulesWithoutLimit = std::move(_modulesWithoutLimit);
    }

    void setRateLimiterConfig(const GatewayConfig::RateLimiterConfig& _rateLimiterConfig)
    {
        m_rateLimiterConfig = _rateLimiterConfig;
    }
    const GatewayConfig::RateLimiterConfig& rateLimiterConfig() const
    {
        return m_rateLimiterConfig;
    }

private:
    //   factory for RateLimiterInterface
    ratelimiter::RateLimiterFactory::Ptr m_rateLimiterFactory;

    // lock for m_group2RateLimiter
    mutable std::shared_mutex x_rateLimiters;
    // group/ip => ratelimiter
    std::unordered_map<std::string, RateLimiterInterface::Ptr> m_rateLimiters;

    // the message of modules that do not limit bandwidth
    std::set<uint16_t> m_modulesWithoutLimit;

    GatewayConfig::RateLimiterConfig m_rateLimiterConfig;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos
