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
 * @file DistributedBWRateLimiter.h
 * @author: octopus
 * @date 2022-06-30
 */

#pragma once

#include <bcos-gateway/libratelimit/BWRateLimiterInterface.h>
#include <bcos-utilities/Common.h>

namespace bcos
{
namespace gateway
{
namespace ratelimit
{

/**
 * @brief
 * Distributed limited bandwidth
 */

class DistributedBWRateLimiter : public BWRateLimiterInterface
{
public:
    using Ptr = std::shared_ptr<DistributedBWRateLimiter>;
    using ConstPtr = std::shared_ptr<const DistributedBWRateLimiter>;
    using UniquePtr = std::unique_ptr<const DistributedBWRateLimiter>;

public:
    DistributedBWRateLimiter(int64_t _maxQPS);

    DistributedBWRateLimiter(DistributedBWRateLimiter&&) = delete;
    DistributedBWRateLimiter(const DistributedBWRateLimiter&) = delete;
    DistributedBWRateLimiter& operator=(const DistributedBWRateLimiter&) = delete;
    DistributedBWRateLimiter& operator=(DistributedBWRateLimiter&&) = delete;

public:
    ~DistributedBWRateLimiter() override {}

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
     * @param _requiredPermits
     * @return int64_t
     */
    int64_t acquireWithoutWait(int64_t _requiredPermits) override;

    /**
     * @brief
     *
     * @return int64_t
     */
    void rollback(int64_t _requiredPermits) override;
};

}  // namespace ratelimit
}  // namespace gateway
}  // namespace bcos
