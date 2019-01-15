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
 * @brief Unit tests for the Hash
 * @file Hash.cpp
 * @author: chaychen asherli
 * @date 2018
 */
#include <iostream>

#include <libdevcore/Assertions.h>
#include <libdevcore/CommonJS.h>
#include <libdevcrypto/AES.h>
#include <libdevcrypto/Hash.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>


using namespace dev;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(AES, TestOutputHelperFixture)
// #ifdef FISCO_GM
BOOST_AUTO_TEST_CASE(GM_testAESen)
{
    bytes seed_true = fromHex("01234567012345670123456701234564");
    bytes _plainData = fromHex("12345");
    // h256 key = dev::sha3(&seed);
    BOOST_CHECK_THROW(readableKeyBytes("ref(seed)"), std::exception);
    bytes endata = aesCBCEncrypt(ref(_plainData), ref(seed_true));
    bytes dedata = aesCBCDecrypt(ref(endata), ref(seed_true));
    BOOST_CHECK_EQUAL(toHex(_plainData), toHex(dedata));
}
// #endif
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev