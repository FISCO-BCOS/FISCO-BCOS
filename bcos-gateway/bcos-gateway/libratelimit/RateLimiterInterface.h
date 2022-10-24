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
 * @file RateLimiterInterface.h
 * @author: octopus
 * @date 2022-06-30
 */

#pragma once

#include <memory>

namespace bcos
{
namespace gateway
{
namespace ratelimiter
{

class RateLimiterInterface
{
public:
    using Ptr = std::shared_ptr<RateLimiterInterface>;
    using ConstPtr = std::shared_ptr<const RateLimiterInterface>;
    using UniquePtr = std::unique_ptr<RateLimiterInterface>;

public:
    RateLimiterInterface() = default;
    RateLimiterInterface(RateLimiterInterface&&) = default;
    RateLimiterInterface(const RateLimiterInterface&) = default;
    RateLimiterInterface& operator=(const RateLimiterInterface&) = default;
    RateLimiterInterface& operator=(RateLimiterInterface&&) = default;

    virtual ~RateLimiterInterface() = default;

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
     * @return
     */
    virtual void rollback(int64_t _requiredPermits) = 0;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos