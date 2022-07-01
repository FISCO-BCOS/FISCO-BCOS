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
 * @file BWRateLimiterInterface.h
 * @author: octopus
 * @date 2022-06-30
 */

#pragma once

#include <memory>

namespace bcos
{
namespace gateway
{
namespace ratelimit
{

class BWRateLimiterInterface
{
public:
    using Ptr = std::shared_ptr<BWRateLimiterInterface>;
    using ConstPtr = std::shared_ptr<const BWRateLimiterInterface>;
    using UniquePtr = std::unique_ptr<BWRateLimiterInterface>;

public:
    BWRateLimiterInterface() = default;
    BWRateLimiterInterface(BWRateLimiterInterface&&) = default;
    BWRateLimiterInterface(const BWRateLimiterInterface&) = default;
    BWRateLimiterInterface& operator=(const BWRateLimiterInterface&) = default;
    BWRateLimiterInterface& operator=(BWRateLimiterInterface&&) = default;

    virtual ~BWRateLimiterInterface() {}

public:
    /**
     * @brief acquire permits
     *
     * @param _requiredPermits
     * @return void
     */
    virtual void acquire(int64_t _requiredPermits) = 0;

    /**
     * @brief
     *
     * @param _requiredPermits
     * @return true
     * @return false
     */
    virtual bool tryAcquire(int64_t _requiredPermits) = 0;

    /**
     * @brief
     *
     * @param _requiredPermits
     * @return int64_t
     */
    virtual int64_t acquireWithoutWait(int64_t _requiredPermits) = 0;

    /**
     * @brief
     *
     * @return int64_t
     */
    virtual void rollback(int64_t _requiredPermits) = 0;
};

}  // namespace ratelimit
}  // namespace gateway
}  // namespace bcos