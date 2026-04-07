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
 * @brief Regression tests for FIB-110: GC backpressure
 * @file FIB110_GCBackpressureTest.cpp
 * @date 2026-04-07
 */

#include "bcos-transaction-scheduler/GC.h"
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos::scheduler_v1;

BOOST_AUTO_TEST_SUITE(FIB110_GCBackpressureTest)

BOOST_AUTO_TEST_CASE(MaxPendingGCConstant)
{
    BOOST_CHECK_EQUAL(GC::MAX_PENDING_GC, 64u);
}

BOOST_AUTO_TEST_CASE(NormalCollectWorks)
{
    // Create a shared_ptr, collect it via GC - should not crash
    auto ptr = std::make_shared<int>(42);
    BOOST_CHECK_EQUAL(ptr.use_count(), 1);
    GC::collect(std::move(ptr));
    // ptr is moved, should be null now
    BOOST_CHECK(!ptr);
}

BOOST_AUTO_TEST_CASE(CollectMultipleResources)
{
    auto ptr1 = std::make_shared<int>(1);
    auto ptr2 = std::make_shared<std::string>("hello");
    GC::collect(std::move(ptr1), std::move(ptr2));
    BOOST_CHECK(!ptr1);
    BOOST_CHECK(!ptr2);
}

BOOST_AUTO_TEST_SUITE_END()
