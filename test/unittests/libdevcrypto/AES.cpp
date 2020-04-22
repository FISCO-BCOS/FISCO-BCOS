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

#include "libdevcrypto/AES.h"
#include <libdevcore/Assertions.h>
#include <libdevcore/CommonJS.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>


using namespace dev;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(AES, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testAESString)
{
    const std::string key = "1527F8068E8F577F4CBB38CB8206CEEA";
    const std::string plainData =
        "8398A06D3A238E77EFDF91E5CE5596942C42788065D24A8DFAF911501BE71181A9D0A318A19D5F7B087401233F"
        "C937A5";
    const std::string iv = "081199C91FA3A8F0B31F7EAB053B770F";
    const std::string endata = aesCBCEncrypt(plainData, key, iv);
    const std::string dedata = aesCBCDecrypt(endata, key, iv);

    BOOST_CHECK_EQUAL(plainData, dedata);

    const std::string keyWithoutIv = "00000000000000000000000000000000";
    const std::string plainDataWithoutIv = "2B731E559C35EB31AD86EA0EAA441F19";
    const std::string endataWithoutIv = aesCBCEncrypt(plainDataWithoutIv, keyWithoutIv);
    const std::string dedataWithoutIv = aesCBCDecrypt(endataWithoutIv, keyWithoutIv);

    BOOST_CHECK_EQUAL(plainDataWithoutIv, dedataWithoutIv);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev