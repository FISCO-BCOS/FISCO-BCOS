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
#include <type_traits>

using namespace bcos::crypto::hasher;

namespace bcos::test
{

using HashType = std::array<std::byte, 32>;

BOOST_FIXTURE_TEST_SUITE(HasherTest, TestPromptFixture) BOOST_AUTO_TEST_CASE(testSHA256)
{
    std::string a = "arg";

    openssl::OpenSSL_SHA3_256_Hasher hash1;
    hash1.update(a);
    hash1.update("abcdefg");
    hash1.update(100);
    auto h1 = final(hash1);

    openssl::OpenSSL_SHA3_256_Hasher hash2;
    hash2.update(a);
    char s[] = "abcdefg";
    hash2.update(s);
    auto b = 100;
    hash2.update(b);
    auto h2 = final(hash2);

    BOOST_CHECK_EQUAL(h1, h2);
}

BOOST_AUTO_TEST_CASE(opensslSHA3)
{
    std::string a = "str123456789012345678901234567890";
    std::string_view view = a;
    bcos::h256 h(100);
    std::span<byte const> hView(h.data(), h.size);

    openssl::OpenSSL_SHA3_256_Hasher hasher1;
    hasher1.update(100);
    hasher1.update(a);
    hasher1.update(view);
    hasher1.update(hView);
    hasher1.update("bbbc");

    auto hash = final(hasher1);

    decltype(hash) emptyHash;
    emptyHash.fill(std::byte('0'));
    BOOST_CHECK_NE(hash, emptyHash);

    std::string a1 = "str123456789012345678901234567890";
    view = a1;
    char by[] = "bbbc";
    int be = 100;

    openssl::OpenSSL_SHA3_256_Hasher hasher2;
    hasher2.update(be);
    hasher2.update(a1);
    hasher2.update(view);
    hasher2.update(std::span((const byte*)h.data(), h.size));
    hasher2.update(by);

    auto hash2 = final(hasher2);

    BOOST_CHECK_EQUAL(hash, hash2);
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