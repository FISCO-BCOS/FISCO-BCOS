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
#include <array>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace bcos
{
namespace gateway
{
namespace ratelimiter
{

class RateLimiterManager
{
public:
    const static std::string TOTAL_OUTGOING_KEY;

    using Ptr = std::shared_ptr<RateLimiterManager>;
    using ConstPtr = std::shared_ptr<const RateLimiterManager>;
    using UniquePtr = std::unique_ptr<RateLimiterManager>;

    RateLimiterManager(const GatewayConfig::RateLimiterConfig& _rateLimiterConfig)
      : m_rateLimiterConfig(_rateLimiterConfig)
    {
        initP2pBasicMsgTypes();
    }

    bcos::ratelimiter::RateLimiterInterface::Ptr getRateLimiter(const std::string& _rateLimiterKey);

    std::pair<bool, bcos::ratelimiter::RateLimiterInterface::Ptr> registerRateLimiter(
        const std::string& _rateLimiterKey, bcos::ratelimiter::RateLimiterInterface::Ptr _rateLimiter);
    bool removeRateLimiter(const std::string& _rateLimiterKey);

    bcos::ratelimiter::RateLimiterInterface::Ptr getGroupRateLimiter(const std::string& _group);
    bcos::ratelimiter::RateLimiterInterface::Ptr getConnRateLimiter(const std::string& _connIP);

    bcos::ratelimiter::RateLimiterInterface::Ptr getInRateLimiter(const std::string& _connIP, uint16_t _packageType);
    bcos::ratelimiter::RateLimiterInterface::Ptr getInRateLimiter(
        const std::string& _groupID, uint16_t _moduleID, bool /***/);

    ratelimiter::RateLimiterFactory::Ptr rateLimiterFactory() const { return m_rateLimiterFactory; }
    void setRateLimiterFactory(ratelimiter::RateLimiterFactory::Ptr& _rateLimiterFactory)
    {
        m_rateLimiterFactory = _rateLimiterFactory;
    }

    inline const std::array<bool, std::numeric_limits<uint16_t>::max()>& modulesWithoutLimit() const
    {
        return m_modulesWithoutLimit;
    }

    void setRateLimiterConfig(const GatewayConfig::RateLimiterConfig& _rateLimiterConfig)
    {
        m_rateLimiterConfig = _rateLimiterConfig;
    }
    const GatewayConfig::RateLimiterConfig& rateLimiterConfig() const
    {
        return m_rateLimiterConfig;
    }

    void resetModulesWithoutLimit(const std::set<uint16_t>& _modulesWithoutLimit)
    {
        m_modulesWithoutLimit.fill(false);
        for (const auto& moduleID : _modulesWithoutLimit)
        {
            m_modulesWithoutLimit.at(moduleID) = true;
        }
    }

    void resetP2pBasicMsgTypes(const std::set<uint16_t>& _p2pBasicMsgTypes)
    {
        m_p2pBasicMsgTypes.fill(false);
        for (const auto& msgType : _p2pBasicMsgTypes)
        {
            m_p2pBasicMsgTypes.at(msgType) = true;
        }
    }

    bool enableOutGroupRateLimit() const { return m_enableOutGroupRateLimit; }
    bool enableOutConRateLimit() const { return m_enableOutConRateLimit; }
    bool enableInRateLimit() const { return m_enableInRateLimit; }

    void setEnableOutGroupRateLimit(bool _enableOutGroupRateLimit)
    {
        m_enableOutGroupRateLimit = _enableOutGroupRateLimit;
    }
    void setEnableOutConRateLimit(bool _enableOutConRateLimit)
    {
        m_enableOutConRateLimit = _enableOutConRateLimit;
    }
    void setEnableInRateLimit(bool _enableInRateLimit) { m_enableInRateLimit = _enableInRateLimit; }

    //----------------------------------------------------------------------
    void initP2pBasicMsgTypes();

    inline bool isP2pBasicMsgType(uint16_t _type) { return m_p2pBasicMsgTypes.at(_type); }
    //----------------------------------------------------------------------

private:
    bool m_enableOutGroupRateLimit = false;
    bool m_enableOutConRateLimit = false;
    bool m_enableInRateLimit = false;

    //   factory for RateLimiterInterface
    ratelimiter::RateLimiterFactory::Ptr m_rateLimiterFactory;

    // lock for m_group2RateLimiter
    mutable std::shared_mutex x_rateLimiters;
    // group/ip => ratelimiter
    std::unordered_map<std::string, bcos::ratelimiter::RateLimiterInterface::Ptr> m_rateLimiters;

    // the message of modules that do not limit bandwidth
    std::array<bool, std::numeric_limits<uint16_t>::max()> m_modulesWithoutLimit{};

    std::array<bool, std::numeric_limits<uint16_t>::max()> m_p2pBasicMsgTypes{};

    GatewayConfig::RateLimiterConfig m_rateLimiterConfig;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos
