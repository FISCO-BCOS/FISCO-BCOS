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
 * @file DistributedBWRateLimiter.cpp
 * @author: octopus
 * @date 2022-06-30
 */

#include <bcos-gateway/libratelimit/DistributedBWRateLimiter.h>
#include <bcos-utilities/Common.h>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimit;

/**
 * @brief acquire permits
 *
 * @param _requiredPermits
 * @return void
 */
void DistributedBWRateLimiter::acquire(int64_t _requiredPermits)
{
    std::ignore = _requiredPermits;
}

/**
 * @brief
 *
 * @param _requiredPermits
 * @return true
 * @return false
 */
bool DistributedBWRateLimiter::tryAcquire(int64_t _requiredPermits)
{
    std::ignore = _requiredPermits;
    return true;
}

/**
 * @brief
 *
 * @param _requiredPermits
 * @return int64_t
 */
int64_t DistributedBWRateLimiter::acquireWithoutWait(int64_t _requiredPermits)
{
    std::ignore = _requiredPermits;
    return 0;
}

/**
 * @brief
 *
 * @return int64_t
 */
void DistributedBWRateLimiter::rollback(int64_t _requiredPermits)
{
    std::ignore = _requiredPermits;
    return;
}
