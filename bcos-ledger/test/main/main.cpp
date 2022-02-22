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
 * @file main.cpp
 * @author: kyonRay
 * @date 2021-05-14
 */

#define BOOST_TEST_NO_MAIN

#include <boost/test/included/unit_test.hpp>
#include <boost/test/unit_test.hpp>

int main(int argc, const char* argv[])
{
    auto fakeInit = [](int, char**) -> boost::unit_test::test_suite* { return nullptr; };
    int result = boost::unit_test::unit_test_main(fakeInit, argc, const_cast<char**>(argv));
    return result;
}
