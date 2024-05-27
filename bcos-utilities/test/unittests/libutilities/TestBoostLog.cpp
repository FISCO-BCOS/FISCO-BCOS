/**
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
 * @file TestBoostLog
 * @author: xingqiangbai
 * @date: 2021-01-18
 */

#include "bcos-utilities/BoostLog.h"
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <thread>
using namespace bcos;
using namespace std;

namespace bcos
{
namespace test
{
BOOST_AUTO_TEST_CASE(testCreatFileColletctor)
{
    boost::filesystem::path targetDir("./testCreatFileColletctor");
    auto collector = bcos::log::make_collector(targetDir, 0, 0, 0, false);
    BOOST_CHECK(collector != nullptr);
}
}  // namespace test
}  // namespace bcos
