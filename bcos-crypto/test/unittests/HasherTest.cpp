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
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>
#include <iterator>
#include <string>

using namespace bcos::crypto::hasher;

namespace bcos::test
{

using HashType = std::array<std::byte, 32>;

BOOST_FIXTURE_TEST_SUITE(HasherTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(testSHA256)
{
    std::string a = "arg";

    openssl::OpenSSL_SHA3_256_Hasher hash1;
    hash1.update(a);
    hash1.update("abcdefg");
    hash1.update(100);

    std::array<std::byte, 32> h1;
    hash1.final(h1);

    openssl::OpenSSL_SHA3_256_Hasher hash2;
    hash2.update(a);
    char s[] = "abcdefg";
    hash2.update(s);
    auto b = 100;
    hash2.update(b);

    std::array<std::byte, 32> h2;
    hash2.final(h2);

    BOOST_CHECK_EQUAL(h1, h2);
}

BOOST_AUTO_TEST_CASE(opensslSHA3)
{
    std::string a = "str123456789012345678901234567890";
    std::string_view view = a;
    bcos::h256 h(100);
    std::span<byte const> hView(h.data(), h.SIZE);

    openssl::OpenSSL_SHA3_256_Hasher hasher1;
    hasher1.update(100);
    hasher1.update(a);
    hasher1.update(view);
    hasher1.update(hView);
    hasher1.update("bbbc");

    std::array<std::byte, 32> hash;
    hasher1.final(hash);

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
    hasher2.update(std::span((const byte*)h.data(), h.SIZE));
    hasher2.update(by);

    std::array<std::byte, 32> hash2;
    hasher2.final(hash2);

    BOOST_CHECK_EQUAL(hash, hash2);
}

BOOST_AUTO_TEST_CASE(dynamicRange)
{
    bcos::h256 hash;

    int a = 10;
    openssl::OpenSSL_SHA3_256_Hasher hasher;
    hasher.update(a);
    BOOST_CHECK_NO_THROW(hasher.final(hash));

    std::vector<char> hashVec;
    hasher.update(a);
    BOOST_CHECK_NO_THROW(hasher.final(hashVec));

    auto b = bcos::crypto::trivial::DynamicRange<std::vector<char>>;
}

BOOST_AUTO_TEST_CASE(digestPreservesState)
{
    openssl::OpenSSL_Keccak256_Hasher hasher;
    std::string abc = "abc";
    hasher.update(abc);

    std::array<std::byte, 32> firstDigest;
    hasher.digest(firstDigest);
    std::array<std::byte, 32> repeatedDigest;
    hasher.digest(repeatedDigest);
    BOOST_CHECK_EQUAL(firstDigest, repeatedDigest);

    std::string def = "def";
    hasher.update(def);
    bcos::bytes continuedDigest;
    hasher.digest(continuedDigest);

    openssl::OpenSSL_Keccak256_Hasher reference;
    std::string abcdef = "abcdef";
    reference.update(abcdef);
    bcos::bytes expectedDigest;
    reference.final(expectedDigest);
    BOOST_CHECK_EQUAL(continuedDigest, expectedDigest);

    openssl::OpenSSL_Keccak256_Hasher empty;
    bcos::bytes emptyDigest;
    empty.digest(emptyDigest);

    openssl::OpenSSL_Keccak256_Hasher emptyReference;
    bcos::bytes expectedEmptyDigest;
    emptyReference.final(expectedEmptyDigest);
    BOOST_CHECK_EQUAL(emptyDigest, expectedEmptyDigest);
}

BOOST_AUTO_TEST_CASE(spanOutputSizeValidation)
{
    openssl::OpenSSL_Keccak256_Hasher hasher;
    std::array<std::byte, 31> output{};
    BOOST_CHECK_THROW(
        hasher.digest(std::span<std::byte>{output}), boost::wrapexcept<std::invalid_argument>);
    BOOST_CHECK_THROW(
        hasher.final(std::span<std::byte>{output}), boost::wrapexcept<std::invalid_argument>);
}

BOOST_AUTO_TEST_CASE(finalFailurePreservesState)
{
    openssl::OpenSSL_Keccak256_Hasher hasher;
    std::string abc = "abc";
    hasher.update(abc);

    std::array<std::byte, 31> shortOutput{};
    BOOST_CHECK_THROW(hasher.final(shortOutput), boost::wrapexcept<std::runtime_error>);

    std::string def = "def";
    hasher.update(def);
    bcos::bytes continuedDigest;
    hasher.digest(continuedDigest);

    openssl::OpenSSL_Keccak256_Hasher reference;
    std::string abcdef = "abcdef";
    reference.update(abcdef);
    bcos::bytes expectedDigest;
    reference.final(expectedDigest);
    BOOST_CHECK_EQUAL(continuedDigest, expectedDigest);
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
