/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief
 *
 * @file Exceptions.cpp
 * @author: yujiechen
 * @date 2018-08-24
 */
#include <iostream>

#include <libdevcore/Assertions.h>
#include <libdevcrypto/Exceptions.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace dev;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(CryptoExceptionsTests, TestOutputHelperFixture)
/// test exceptions

BOOST_AUTO_TEST_CASE(testExceptions)
{
    BOOST_CHECK_THROW(assertThrow(false, dev::crypto::CryptoException, "Throw crypto Exception"),
        dev::crypto::CryptoException);
    BOOST_REQUIRE_NO_THROW(
        assertThrow(true, dev::crypto::CryptoException, "Throw crypto Exception"));
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
