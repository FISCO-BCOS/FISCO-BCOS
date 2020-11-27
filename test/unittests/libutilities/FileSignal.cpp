/**
 *  Copyright (C) 2020 FISCO BCOS.
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
 * @file FileSignal.cpp
 * @author: jimmyshi
 * @date 2019-07-10
 */

#include <libutilities/FileSignal.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <fstream>

using namespace bcos;
using namespace std;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(FileSignal, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(FileSignal)
{
    bool called = false;
    function<void(void)> caller = [&]() { called = true; };
    string file = ".file_signal_test";

    // check the file is not exist
    BOOST_CHECK(!boost::filesystem::exists(file));

    // check caller is not triggered as the file is not exist
    bcos::FileSignal::callIfFileExist(file, caller);
    BOOST_CHECK(!called);

    // create the file
    ofstream fs(file);
    fs << "";
    fs.close();

    // check the file is exist
    BOOST_CHECK(boost::filesystem::exists(file));

    // check caller is triggered as the file is exist
    bcos::FileSignal::callIfFileExist(file, caller);
    BOOST_CHECK(called);

    // check the file is deleted after trigger
    BOOST_CHECK(!boost::filesystem::exists(file));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
