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
 * @brief test cases for hasher256
 * @file HasherTest.h
 * @date 2022.04.19
 */
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/algorithm/hex.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/test/unit_test.hpp>
#include <iterator>
#include <string>

using namespace bcos;
using namespace crypto;
using namespace bcos::crypto::hasher;

namespace bcos::test
{

BOOST_FIXTURE_TEST_SUITE(HasherTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testSHA256)
{
    std::string a = "arg";

    openssl::OpenSSL_SHA3_256_Hasher hash1;
    hash1.update(a);
    hash1.update("abcdefg");
    hash1.update(100);

    auto h1 = hash1.final();
    auto h2 = openssl::OpenSSL_SHA3_256_Hasher{}.update(a).update("abcdefg").update(100).final();

    BOOST_CHECK_EQUAL(h1, h2);
}

BOOST_AUTO_TEST_CASE(opensslKeccak256)
{
    std::string str = "hash12345";
    std::string str1 = "hash12345";

    openssl::OpenSSL_SHA3_256_Hasher hash1;
    hash1.update(str1);

    auto h1 = hash1.final();
    auto h2 = openssl::OpenSSL_SHA3_256_Hasher{}.calculate(str1);

    BOOST_CHECK_EQUAL(h1, h2);
}

BOOST_AUTO_TEST_CASE(opensslSHA3)
{
    std::string a = "str123456789012345678901234567890";
    std::string_view view = a;
    bcos::h256 h(100);
    std::span<byte const> hView(h.data(), h.size);

    auto hash = openssl::OpenSSL_SHA3_256_Hasher{}
                    .update(100)
                    .update(a)
                    .update(view)
                    .update(hView)
                    .update("bbbc")
                    .final();

    decltype(hash) emptyHash;
    BOOST_CHECK_NE(hash, emptyHash);

    std::string a1 = "str123456789012345678901234567890";
    view = a1;
    char by[] = "bbbc";
    int be = 100;
    auto hash2 = openssl::OpenSSL_SHA3_256_Hasher{}
                     .update(be)
                     .update(a1)
                     .update(view)
                     .update(std::span((const byte*)h.data(), h.size))
                     .update(by)
                     .final();

    BOOST_CHECK_EQUAL(hash, hash2);

    std::vector<std::string> strList;
    strList.emplace_back("hello world!");

    std::vector<std::vector<std::string>> multiStrList;

    // openssl::OpenSSL_SHA3_256_Hasher{}.update(multiStrList).final();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test

namespace std
{

template <size_t length>
::std::ostream& operator<<(::std::ostream& stream, const std::array<std::byte, length>& hash)
{
    std::string str;
    str.reserve(hash.size() * 2);
    std::span<char> view{(char*)hash.data(), hash.size()};

    boost::algorithm::hex_lower(view.begin(), view.end(), std::back_inserter(str));
    stream << str;
    return stream;
}
}  // namespace std