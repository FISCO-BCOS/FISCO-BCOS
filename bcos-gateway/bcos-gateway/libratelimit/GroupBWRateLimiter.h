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

#include "bcos-gateway/libratelimit/BWRateLimiterInterface.h"
#include "bcos-gateway/libratelimit/GroupBWRateLimiterInterface.h"
#include "bcos-gateway/libratelimit/ModuleWhiteList.h"
#include <bcos-utilities/Common.h>
#include <unordered_map>

namespace bcos
{
namespace gateway
{
namespace ratelimit
{

class GroupBWRateLimiter : public GroupBWRateLimiterInterface
{
public:
    using Ptr = std::shared_ptr<GroupBWRateLimiter>;
    using ConstPtr = std::shared_ptr<const GroupBWRateLimiter>;
    using UniquePtr = std::unique_ptr<GroupBWRateLimiter>;

public:
    GroupBWRateLimiter() = default;
    GroupBWRateLimiter(GroupBWRateLimiter&&) = delete;
    GroupBWRateLimiter(const GroupBWRateLimiter&) = delete;
    GroupBWRateLimiter& operator=(const GroupBWRateLimiter&) = delete;
    GroupBWRateLimiter& operator=(GroupBWRateLimiter&&) = delete;

    ~GroupBWRateLimiter() override {}

public:
    /**
     * @brief
     *
     * @param _group
     * @param _moduleID
     * @param _requiredPermits
     */
    void acquire(const std::string& _group, uint16_t _moduleID, int64_t _requiredPermits) override;

    /**
     * @brief
     *
     * @param _group
     * @param _moduleID
     * @param _requiredPermits
     * @return true
     * @return false
     */
    bool tryAcquire(
        const std::string& _group, uint16_t _moduleID, int64_t _requiredPermits) override;

    /**
     * @brief
     *
     * @param _group
     * @param _moduleID
     * @param _requiredPermits
     * @return int64_t
     */
    int64_t acquireWithoutWait(
        const std::string& _group, uint16_t _moduleID, int64_t _requiredPermits) override;

public:
    bool registerGroupRateLimiter(
        const std::string& _group, BWRateLimiterInterface::Ptr _rateLimiter);
    bool removeGroupRateLimiter(const std::string& _group);

    BWRateLimiterInterface::Ptr getGroupRateLimiter(const std::string& _group);

    ModuleWhiteList& moduleWhiteList() { return m_moduleWhiteList; }
    const ModuleWhiteList& moduleWhiteList() const { return m_moduleWhiteList; }

    void setModuleWhiteList(ModuleWhiteList _moduleWhiteList)
    {
        m_moduleWhiteList = _moduleWhiteList;
    }

public:
    // group => ratelimiter
    std::unordered_map<std::string, BWRateLimiterInterface::Ptr> m_group2RateLimiter;

    ModuleWhiteList m_moduleWhiteList;
};

}  // namespace ratelimit
}  // namespace gateway
}  // namespace bcos
