/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file testSchedulerRequiresPool.cpp
 * @brief SchedulerImpl and SchedulerManager refuse a null IOServicePool at construction instead
 * of dereferencing a null strand on the first executeBlock / switch-term post.
 */
#include "bcos-scheduler/src/SchedulerImpl.h"
#include "bcos-scheduler/src/SchedulerManager.h"
#include <boost/test/unit_test.hpp>
#include <stdexcept>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(testSchedulerRequiresPool)

BOOST_AUTO_TEST_CASE(schedulerImplRefusesNullPool)
{
    BOOST_CHECK_THROW(bcos::scheduler::SchedulerImpl(nullptr, nullptr, nullptr, nullptr, nullptr,
                          nullptr, nullptr, nullptr, false, false, 0, 0, nullptr),
        std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(schedulerManagerRefusesNullPool)
{
    BOOST_CHECK_THROW(
        bcos::scheduler::SchedulerManager(0, nullptr, nullptr, nullptr), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
