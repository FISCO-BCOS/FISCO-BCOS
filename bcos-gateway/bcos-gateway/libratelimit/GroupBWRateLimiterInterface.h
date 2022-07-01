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
 * @file GroupBWRateLimiterInterface.h
 * @author: octopus
 * @date 2022-06-30
 */

#pragma once

#include <bcos-gateway/libratelimit/BWRateLimiterInterface.h>
#include <bcos-utilities/Common.h>
#include <unordered_map>

namespace bcos
{
namespace gateway
{
namespace ratelimit
{

class GroupBWRateLimiterInterface
{
public:
    using Ptr = std::shared_ptr<GroupBWRateLimiterInterface>;
    using ConstPtr = std::shared_ptr<const GroupBWRateLimiterInterface>;
    using UniquePtr = std::unique_ptr<GroupBWRateLimiterInterface>;

public:
    GroupBWRateLimiterInterface() = default;
    GroupBWRateLimiterInterface(GroupBWRateLimiterInterface&&) = default;
    GroupBWRateLimiterInterface(const GroupBWRateLimiterInterface&) = default;
    GroupBWRateLimiterInterface& operator=(const GroupBWRateLimiterInterface&) = default;
    GroupBWRateLimiterInterface& operator=(GroupBWRateLimiterInterface&&) = default;

    virtual ~GroupBWRateLimiterInterface() {}

public:
    /**
     * @brief
     *
     * @param _group
     * @param _moduleID
     * @param _requiredPermits
     */
    virtual void acquire(
        const std::string& _group, uint16_t _moduleID, int64_t _requiredPermits) = 0;

    /**
     * @brief
     *
     * @param _group
     * @param _moduleID
     * @param _requiredPermits
     * @return true
     * @return false
     */
    virtual bool tryAcquire(
        const std::string& _group, uint16_t _moduleID, int64_t _requiredPermits) = 0;

    /**
     * @brief
     *
     * @param _group
     * @param _moduleID
     * @param _requiredPermits
     * @return int64_t
     */
    virtual int64_t acquireWithoutWait(
        const std::string& _group, uint16_t _moduleID, int64_t _requiredPermits) = 0;
        
};

}  // namespace ratelimit
}  // namespace gateway
}  // namespace bcos
