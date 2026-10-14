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
 * @file main.cpp
 * @author: yujiechen, jimmyshi
 * @date 2021-02-24
 */
#define BOOST_TEST_MODULE FISCO_BCOS_Tests
#define BOOST_TEST_MAIN

// Library mode: the target links Boost::unit_test_framework, which provides the framework
// and main(). Including the header-only implementation as well defines the same symbols a
// second time, which lld rejects (GNU ld only tolerated it because it never pulled the
// archive member). See bcos-devp2p/test/main.cpp.
#include <boost/test/unit_test.hpp>
