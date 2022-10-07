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
 * @file DistributedRateLimiter.h
 * @author: octopus
 * @date 2022-06-30
 */

#pragma once

#include <bcos-gateway/libratelimit/RateLimiterInterface.h>
#include <bcos-utilities/Common.h>

namespace bcos
{
namespace gateway
{
namespace ratelimiter
{

class DistributedRateLimiter : public RateLimiterInterface
{
public:
    using Ptr = std::shared_ptr<DistributedRateLimiter>;
    using ConstPtr = std::shared_ptr<const DistributedRateLimiter>;
    using UniquePtr = std::unique_ptr<const DistributedRateLimiter>;

public:
    DistributedRateLimiter(int64_t _maxQPS);

    DistributedRateLimiter(DistributedRateLimiter&&) = delete;
    DistributedRateLimiter(const DistributedRateLimiter&) = delete;
    DistributedRateLimiter& operator=(const DistributedRateLimiter&) = delete;
    DistributedRateLimiter& operator=(DistributedRateLimiter&&) = delete;

public:
    ~DistributedRateLimiter() override {}

public:
    /**
     * @brief acquire permits
     *
     * @param _requiredPermits
     * @return void
     */
    void acquire(int64_t _requiredPermits) override;

    /**
     * @brief
     *
     * @param _requiredPermits
     * @return true
     * @return false
     */
    bool tryAcquire(int64_t _requiredPermits) override;


    /**
     * @brief
     *
     * @return
     */
    void rollback(int64_t _requiredPermits) override;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos
