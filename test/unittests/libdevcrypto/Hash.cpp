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
 * @author: chaychen
 * @date 2018
 */

#include "libdevcrypto/CryptoInterface.h"
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
BOOST_FIXTURE_TEST_SUITE(SM_Hash, SM_CryptoTestFixture)
BOOST_AUTO_TEST_CASE(SM_testEmptySHA3)
{
    std::string ts = EmptyHash.hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b"));

    ts = crypto::Hash("abcde").hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("afe4ccac5ab7d52bcae36373676215368baf52d3905e1fecbe369cc120e97628"));

    h256 emptySHA3(fromHex("1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b"));
    BOOST_REQUIRE_EQUAL(emptySHA3, EmptyHash);
}

BOOST_AUTO_TEST_CASE(SM_testSha3General)
{
    BOOST_REQUIRE_EQUAL(
        crypto::Hash(""), h256("1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b"));
    BOOST_REQUIRE_EQUAL(crypto::Hash("hello"),
        h256("becbbfaae6548b8bf0cfcad5a27183cd1be6093b1cceccc303d9c61d0a645268"));
}

/// test sha3Secure and
BOOST_AUTO_TEST_CASE(SM_testSha3CommonFunc)
{
    std::string content = "abcd";
    bytes content_bytes(content.begin(), content.end());
    // test sha3Secure
    SecureFixedHash<32> sec_sha3 = sha3Secure(ref(content_bytes));
    SecureFixedHash<32> copyed_sha3 = sec_sha3;
    // const byte* ptr = copyed_sha3.data();
    /// const byte* p_sec = sec_sha3.data();
    BOOST_CHECK(copyed_sha3.data() != sec_sha3.data());
    BOOST_CHECK(sec_sha3.data());
#if 0
    // test sha3 with SecureFixedHash input
    BOOST_CHECK(crypto::Hash(sec_sha3).data());
#endif
    // test sha3Mac
    h256 egressMac(crypto::Hash("+++"));
    bytes magic{0x22, 0x40, 0x08, 0x91};
    sm3mac(egressMac.ref(), &magic, egressMac.ref());
    BOOST_CHECK(
        toHex(egressMac) == "c58b0b390d16cdfc1b81cbf01614aa3d8ec3f47c81cd0e29cd0a61718a38ec83");
}

// test sha2
BOOST_AUTO_TEST_CASE(SM_testSha256)
{
    const std::string plainText = "123456ABC+";
    const std::string cipherText =
        "0x68b5bae5fe19851624298fd1e9b4d788627ac27c13aad3240102ffd292a17911";
    bytes bs;
    for (size_t i = 0; i < plainText.length(); i++)
    {
        bs.push_back((byte)plainText[i]);
    }
    bytesConstRef bsConst(&bs);
    BOOST_CHECK(toJS(sm3(bsConst)) == cipherText);
}

BOOST_AUTO_TEST_CASE(SM_testRipemd160)
{
    const std::string plainText = "123456ABC+";
    const std::string cipherText = "0x74204bedd818292adc1127f9bb24bafd75468b62";
    bytes bs;
    for (size_t i = 0; i < plainText.length(); i++)
    {
        bs.push_back((byte)plainText[i]);
    }
    bytesConstRef bsConst(&bs);
    BOOST_CHECK(toJS(ripemd160(bsConst)) == cipherText);
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(Hash, TestOutputHelperFixture)
// test sha3
BOOST_AUTO_TEST_CASE(testEmptySHA3)
{
    std::string ts = EmptyHash.hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));

    ts = crypto::Hash("abcde").hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("6377c7e66081cb65e473c1b95db5195a27d04a7108b468890224bedbe1a8a6eb"));

    h256 emptySHA3(fromHex("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
    BOOST_REQUIRE_EQUAL(emptySHA3, EmptyHash);
}

BOOST_AUTO_TEST_CASE(testSha3General)
{
    BOOST_REQUIRE_EQUAL(
        crypto::Hash(""), h256("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
    BOOST_REQUIRE_EQUAL(crypto::Hash("hello"),
        h256("1c8aff950685c2ed4bc3174f3472287b56d9517b9c948127319a09a7a36deac8"));
}

/// test sha3Secure and
BOOST_AUTO_TEST_CASE(testSha3CommonFunc)
{
    std::string content = "abcd";
    bytes content_bytes(content.begin(), content.end());
    // test sha3Secure
    SecureFixedHash<32> sec_sha3 = sha3Secure(ref(content_bytes));
    SecureFixedHash<32> copyed_sha3 = sec_sha3;
    BOOST_CHECK(copyed_sha3.data() != sec_sha3.data());
    BOOST_CHECK(sec_sha3.data());
#if 0
    // test sha3 with SecureFixedHash input
    BOOST_CHECK(crypto::Hash(sec_sha3).data());
#endif
    // test sha3Mac
    h256 egressMac(crypto::Hash("+++"));
    bytes magic{0x22, 0x40, 0x08, 0x91};
    sha3mac(egressMac.ref(), &magic, egressMac.ref());
    BOOST_CHECK(toHex(egressMac) ==
                "75759ba49fdef48a80840b669"
                "9c4cc25ecb5e60f5dd0bf889381084ca6fc4199");
}
// test sha2
BOOST_AUTO_TEST_CASE(testSha256)
{
    const std::string plainText = "123456ABC+";
    const std::string cipherText =
        "0x2218be4abd327ca929399fc73314f3d0cdd03cfc98927fabe7cd40f2059efd01";
    bytes bs;
    for (size_t i = 0; i < plainText.length(); i++)
    {
        bs.push_back((byte)plainText[i]);
    }
    bytesConstRef bsConst(&bs);
    BOOST_CHECK(toJS(sha256(bsConst)) == cipherText);
}

BOOST_AUTO_TEST_CASE(testRipemd160)
{
    const std::string plainText = "123456ABC+";
    const std::string cipherText = "0x74204bedd818292adc1127f9bb24bafd75468b62";
    bytes bs;
    for (size_t i = 0; i < plainText.length(); i++)
    {
        bs.push_back((byte)plainText[i]);
    }
    bytesConstRef bsConst(&bs);
    BOOST_CHECK(toJS(ripemd160(bsConst)) == cipherText);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
