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
 * @file SHA3.cpp
 * @author: jimmyshi
 * @date 2018-08-27
 */


#include <libdevcore/Exceptions.h>
#include <libdevcore/SHA3.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <string>
#include <vector>

using namespace dev;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(SHA3, TestOutputHelperFixture)
/// test micro "asserts"
BOOST_AUTO_TEST_CASE(testEmptySHA3)
{
    std::string ts = EmptySHA3.hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));

    ts = EmptyListSHA3.hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"));

    ts = sha3("abcde").hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("6377c7e66081cb65e473c1b95db5195a27d04a7108b468890224bedbe1a8a6eb"));

    h256 emptySHA3(fromHex("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
    BOOST_REQUIRE_EQUAL(emptySHA3, EmptySHA3);

    h256 emptyListSHA3(fromHex("1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"));
    BOOST_REQUIRE_EQUAL(emptyListSHA3, EmptyListSHA3);
}

BOOST_AUTO_TEST_CASE(testSha3General)
{
    BOOST_REQUIRE_EQUAL(
        sha3(""), h256("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
    BOOST_REQUIRE_EQUAL(
        sha3("hello"), h256("1c8aff950685c2ed4bc3174f3472287b56d9517b9c948127319a09a7a36deac8"));
}

/// test sha3Secure and
BOOST_AUTO_TEST_CASE(testSha3CommonFunc)
{
    std::string content = "abcd";
    bytes content_bytes(content.begin(), content.end());
    // test sha3Secure
    SecureFixedHash<32> sec_sha3 = sha3Secure(ref(content_bytes));
    SecureFixedHash<32> copyed_sha3 = sec_sha3;
    const byte* ptr = copyed_sha3.data();
    const byte* p_sec = sec_sha3.data();
    BOOST_CHECK(copyed_sha3.data() != sec_sha3.data());
    BOOST_CHECK(sec_sha3.data());
    // test sha3 with SecureFixedHash input
    BOOST_CHECK(sha3(sec_sha3).data());
    // test sha3Mac
    h256 egressMac(sha3("+++"));
    bytes magic{0x22, 0x40, 0x08, 0x91};
    sha3mac(egressMac.ref(), &magic, egressMac.ref());
    BOOST_CHECK(toHex(egressMac) ==
                "75759ba49fdef48a80840b669"
                "9c4cc25ecb5e60f5dd0bf889381084ca6fc4199");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
