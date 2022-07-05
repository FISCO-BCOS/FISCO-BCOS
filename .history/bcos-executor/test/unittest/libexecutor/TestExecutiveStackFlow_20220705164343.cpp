/*
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief interface definition of ExecutiveFlow
 * @file ExecutiveStackFlow.h
 * @author: jimmyshi
 * @date: 2022-03-22
 */

#pragma once

#include "../../../src/executive/ExecutiveFlowInterface.h"
#include "../../../src/executive/ExecutiveState.h"
#include "../mock/MockExecutiveFactory.h"
#include <tbb/concurrent_unordered_map.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <stack>


using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace bcos
{
namespace test
{
struct ExecutiveStackFlowFixture
{
    ExecutiveStackFlowFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(TestExecutiveStackFlow, ExecutiveStackFlowFixture)
BOOST_AUTO_TEST_CASE()
BOOST_AUTO_TEST_SUITE_END()


}  // namespace test
}  // namespace bcos
