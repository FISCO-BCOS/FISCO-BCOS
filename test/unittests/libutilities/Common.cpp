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
 * @brief: unit test for Common.* of libutilities
 *
 * @file Common.cpp
 * @author: yujiechen
 * @date 2018-08-24
 */


#include <libutilities/Common.h>
#include <libutilities/Exceptions.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <unistd.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(DevcoreCommonTest, TestOutputHelperFixture)
/// test Arith Calculations
BOOST_AUTO_TEST_CASE(testArithCal)
{
    ///=========test u2s==================
    u256 u_bigint("343894723987432");
    bigint c_end = bigint(1) << 256;
    BOOST_CHECK(u2s(u_bigint) == u_bigint);
    u_bigint = Invalid256;
    BOOST_CHECK(u2s(u_bigint) < s256(0));
    u_bigint = u256("0xa170d8e0ae1b57d7ecc121f6fe5ceb03c1267801ff720edd2f8463e7effac6c6");
    BOOST_CHECK(u2s(u_bigint) < s256(0));
    BOOST_CHECK(u2s(u_bigint) == s256(-(c_end - u_bigint)));
    u_bigint = u256("0x7170d8e0ae1b57d7ecc121f6fe5ceb03c1267801ff720edd2f8463e7effac6c6");
    BOOST_CHECK(u2s(u_bigint) == u_bigint);
    ///=========test s2u==================
    s256 s_bigint("0x7170d8e0ae1b57d7ecc121f6fe5ceb03c1267801ff720edd2f8463e7effac6c6");
    BOOST_CHECK(s2u(s_bigint) == s_bigint);
    s_bigint = s256("0xf170d8e0ae1b57d7ecc121f6fe5ceb03c1267801ff720edd2f8463e7effac6c6");
    BOOST_CHECK(s2u(s_bigint) == u256(c_end + s_bigint));
    ///=========test exp10==================
    BOOST_CHECK(exp10<1>() == u256(10));
    BOOST_CHECK(exp10<9>() == u256(1000000000));
    BOOST_CHECK(exp10<0>() == u256(1));
}
/// test utcTime
BOOST_AUTO_TEST_CASE(testUtcTime)
{
    uint64_t old_time = utcTime();
    usleep(1000);
    BOOST_CHECK(old_time < utcTime());
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
