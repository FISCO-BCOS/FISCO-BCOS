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
 * @file main.cpp
 * @date 2026/8/18
 */
#define BOOST_TEST_MODULE FISCO_BCOS_Tests
#define BOOST_TEST_MAIN

// Library mode, like the other 25 test mains in this repository: the target links
// Boost::unit_test_framework, which provides the framework and main(). Including
// <boost/test/included/unit_test.hpp> as well defines the whole framework in this
// translation unit too, so both sides carry the same symbols. GNU ld hid that by never
// pulling the archive member; lld rejects it ("duplicate symbol:
// boost::unit_test::framework::..."), and the header-only implementation is also the
// slowest way to compile a test main.
#include <boost/test/unit_test.hpp>
