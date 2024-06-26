/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file RLPTest.cpp
 * @author: kyonGuo
 * @date 2024/4/7
 */

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <string_view>


using namespace bcos;
using namespace bcos::codec::rlp;
using namespace std::literals;

namespace bcos::test
{
template <typename T>
static std::string staticEncode(T const& in)
{
    bcos::bytes out{};
    bcos::codec::rlp::encode(out, in);
    return toHex(out);
}

template <typename T, typename T2>
static std::string staticEncode(T const& in, T2 const& in2)
{
    bcos::bytes out{};
    bcos::codec::rlp::encode(out, in, in2);
    return toHex(out);
}

template <typename T, typename T2, typename... Args>
static std::string staticEncode(T const& in, T2 const& in2, const Args&... args)
{
    bcos::bytes out{};
    bcos::codec::rlp::encode(out, in, in2, args...);
    return toHex(out);
}

template <typename T>
static T decode(std::string_view hex, int32_t expectedErrorCode = 0)
{
    bcos::bytes bytes = fromHex(hex);
    auto bytesRef = bcos::ref(bytes);
    T result{};
    auto&& error = bcos::codec::rlp::decode(bytesRef, result);
    if (expectedErrorCode == 0)
    {
        BOOST_CHECK(!error);
    }
    else
    {
        BOOST_CHECK_EQUAL(error->errorCode(), expectedErrorCode);
    }
    return result;
}

template <typename T, typename T2>
static std::tuple<T, T2> decode(std::string_view hex, int32_t expectedErrorCode = 0)
{
    bcos::bytes bytes = fromHex(hex);
    auto bytesRef = bcos::ref(bytes);
    T r1{};
    T2 r2{};
    auto&& error = bcos::codec::rlp::decode(bytesRef, r1, r2);
    if (expectedErrorCode == 0)
    {
        BOOST_CHECK(!error);
    }
    else
    {
        BOOST_CHECK_EQUAL(error->errorCode(), expectedErrorCode);
    }
    return {std::move(r1), std::move(r2)};
}

template <typename T, typename T2, typename... Args>
static bcos::Error::UniquePtr decode(std::string_view hex, T& r1, T2& r2, Args&... r3)
{
    bcos::bytes bytes = fromHex(hex);
    auto bytesRef = bcos::ref(bytes);
    auto&& error = bcos::codec::rlp::decode(bytesRef, r1, r2, r3...);
    return error;
}

// template <typename T, typename T2, typename... Args>
// static std::tuple<T, T2, Args...> decodeSuccess(std::string_view hex)
// {
//     std::tuple<T, T2, Args...> tuple;
//     auto error = decode(hex, std::get<0>(tuple), std::get<1>(tuple), std::get<2>(tuple));
//     // auto error = decode()
// }

template <typename T>
static bool checkVectorEqual(std::vector<T> const& v1, std::vector<T> const& v2)
{
    if (v1.size() != v2.size())
    {
        return false;
    }
    for (size_t i = 0; i < v1.size(); i++)
    {
        if (v1[i] != v2[i])
        {
            return false;
        }
    }
    return true;
}

// double test based on https://codechain-io.github.io/rlp-debugger/
BOOST_FIXTURE_TEST_SUITE(RLPTest, test::TestPromptFixture)
BOOST_AUTO_TEST_CASE(stringsEncode)
{
    BOOST_CHECK_EQUAL(staticEncode("dog"sv), "83646f67");
    BOOST_CHECK_EQUAL(staticEncode("cat"sv), "83636174");
    BOOST_CHECK_EQUAL(staticEncode("doge"sv), "84646f6765");
    BOOST_CHECK_EQUAL(staticEncode(""sv), "80");
    BOOST_CHECK_EQUAL(staticEncode(fromHex("7B"sv)), "7b");
    BOOST_CHECK_EQUAL(staticEncode(fromHex("80"sv)), "8180");
    BOOST_CHECK_EQUAL(staticEncode(fromHex("ABBA"sv)), "82abba");
    BOOST_CHECK_EQUAL(staticEncode("中国加油"sv), "8ce4b8ade59bbde58aa0e6b2b9");
    // clang-format off
    BOOST_CHECK_EQUAL(
        staticEncode(
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Curabitur mauris magna, suscipit sed vehicula non, iaculis faucibus tortor. Proin suscipit ultricies malesuada. Duis tortor elit, dictum quis tristique eu, ultrices at risus. Morbi a est imperdiet mi ullamcorper aliquet suscipit nec lorem. Aenean quis leo mollis, vulputate elit varius, consequat enim. Nulla ultrices turpis justo, et posuere urna consectetur nec. Proin non convallis metus. Donec tempor ipsum in mauris congue sollicitudin. Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia Curae; Suspendisse convallis sem vel massa faucibus, eget lacinia lacus tempor. Nulla quis ultricies purus. Proin auctor rhoncus nibh condimentum mollis. Aliquam consequat enim at metus luctus, a eleifend purus egestas. Curabitur at nibh metus. Nam bibendum, neque at auctor tristique, lorem libero aliquet arcu, non interdum tellus lectus sit amet eros. Cras rhoncus, metus ac ornare cursus, dolor justo ultrices metus, at ullamcorper volutpat"sv),
        "b904004c6f72656d20697073756d20646f6c6f722073697420616d65742c20636f6e73656374657475722061646970697363696e6720656c69742e20437572616269747572206d6175726973206d61676e612c20737573636970697420736564207665686963756c61206e6f6e2c20696163756c697320666175636962757320746f72746f722e2050726f696e20737573636970697420756c74726963696573206d616c6573756164612e204475697320746f72746f7220656c69742c2064696374756d2071756973207472697374697175652065752c20756c7472696365732061742072697375732e204d6f72626920612065737420696d70657264696574206d6920756c6c616d636f7270657220616c6971756574207375736369706974206e6563206c6f72656d2e2041656e65616e2071756973206c656f206d6f6c6c69732c2076756c70757461746520656c6974207661726975732c20636f6e73657175617420656e696d2e204e756c6c6120756c74726963657320747572706973206a7573746f2c20657420706f73756572652075726e6120636f6e7365637465747572206e65632e2050726f696e206e6f6e20636f6e76616c6c6973206d657475732e20446f6e65632074656d706f7220697073756d20696e206d617572697320636f6e67756520736f6c6c696369747564696e2e20566573746962756c756d20616e746520697073756d207072696d697320696e206661756369627573206f726369206c756374757320657420756c74726963657320706f737565726520637562696c69612043757261653b2053757370656e646973736520636f6e76616c6c69732073656d2076656c206d617373612066617563696275732c2065676574206c6163696e6961206c616375732074656d706f722e204e756c6c61207175697320756c747269636965732070757275732e2050726f696e20617563746f722072686f6e637573206e69626820636f6e64696d656e74756d206d6f6c6c69732e20416c697175616d20636f6e73657175617420656e696d206174206d65747573206c75637475732c206120656c656966656e6420707572757320656765737461732e20437572616269747572206174206e696268206d657475732e204e616d20626962656e64756d2c206e6571756520617420617563746f72207472697374697175652c206c6f72656d206c696265726f20616c697175657420617263752c206e6f6e20696e74657264756d2074656c6c7573206c65637475732073697420616d65742065726f732e20437261732072686f6e6375732c206d65747573206163206f726e617265206375727375732c20646f6c6f72206a7573746f20756c747269636573206d657475732c20617420756c6c616d636f7270657220766f6c7574706174");
    BOOST_CHECK_EQUAL(staticEncode("Lorem ipsum dolor sit amet, consectetur adipisicing elit"sv),
        "b8384c6f72656d20697073756d20646f6c6f722073697420616d65742c20636f6e7365637465747572206164697069736963696e6720656c6974");
    // clang-format on
}

BOOST_AUTO_TEST_CASE(uint64Encode)
{
    BOOST_CHECK_EQUAL(staticEncode(0u), "80");
    BOOST_CHECK_EQUAL(staticEncode(1u), "01");
    BOOST_CHECK_EQUAL(staticEncode(0x7Fu), "7f");
    BOOST_CHECK_EQUAL(staticEncode(0x80u), "8180");
    BOOST_CHECK_EQUAL(staticEncode(0x400u), "820400");
    BOOST_CHECK_EQUAL(staticEncode(0xFFCCB5u), "83ffccb5");
    BOOST_CHECK_EQUAL(staticEncode(0xFFCCB5DDu), "84ffccb5dd");
    BOOST_CHECK_EQUAL(staticEncode(0xFFCCB5DDFFu), "85ffccb5ddff");
    BOOST_CHECK_EQUAL(staticEncode(0xFFCCB5DDFFEEu), "86ffccb5ddffee");
    BOOST_CHECK_EQUAL(staticEncode(0xFFCCB5DDFFEE14u), "87ffccb5ddffee14");
    BOOST_CHECK_EQUAL(staticEncode(0xFFCCB5DDFFEE1483u), "88ffccb5ddffee1483");
}

BOOST_AUTO_TEST_CASE(uint256Encode)
{
    BOOST_CHECK_EQUAL(staticEncode(u256(0u)), "80");
    BOOST_CHECK_EQUAL(staticEncode(u256(1u)), "01");
    BOOST_CHECK_EQUAL(staticEncode(u256(0x7Fu)), "7f");
    BOOST_CHECK_EQUAL(staticEncode(u256(0x80u)), "8180");
    BOOST_CHECK_EQUAL(staticEncode(u256(0x400u)), "820400");
    BOOST_CHECK_EQUAL(staticEncode(u256(0xFFCCB5u)), "83ffccb5");
    BOOST_CHECK_EQUAL(staticEncode(u256(0xFFCCB5DDu)), "84ffccb5dd");
    BOOST_CHECK_EQUAL(staticEncode(u256(0xFFCCB5DDFFu)), "85ffccb5ddff");
    BOOST_CHECK_EQUAL(staticEncode(u256(0xFFCCB5DDFFEEu)), "86ffccb5ddffee");
    BOOST_CHECK_EQUAL(staticEncode(u256(0xFFCCB5DDFFEE14u)), "87ffccb5ddffee14");
    BOOST_CHECK_EQUAL(staticEncode(u256(0xFFCCB5DDFFEE1483u)), "88ffccb5ddffee1483");
    BOOST_CHECK_EQUAL(
        staticEncode(u256("0x10203E405060708090A0B0C0D0E0F2")), "8f10203e405060708090a0b0c0d0e0f2");
    BOOST_CHECK_EQUAL(
        staticEncode(u256("0x0100020003000400050006000700080009000A0B4B000C000D000E01")),
        "9c0100020003000400050006000700080009000a0b4b000c000d000e01");
}

BOOST_AUTO_TEST_CASE(vectorsEncode)
{
    BOOST_CHECK_EQUAL(staticEncode(std::vector<uint64_t>{}), "c0");
    BOOST_CHECK_EQUAL(
        staticEncode(std::vector<uint64_t>{0xFFCCB5, 0xFFC0B5}), "c883ffccb583ffc0b5");
    BOOST_CHECK_EQUAL(
        staticEncode(std::vector<uint32_t>{0xFFCCB5, 0xFFC0B5}), "c883ffccb583ffc0b5");
    BOOST_CHECK_EQUAL(staticEncode(std::vector<std::string>{"cat", "dog"}), "c88363617483646f67");
    BOOST_CHECK_EQUAL(
        staticEncode(std::vector<std::string>{"dog", "god", "cat"}), "cc83646f6783676f6483636174");
    BOOST_CHECK_EQUAL(
        staticEncode("zw"sv, std::vector<uint64_t>({4}), uint16_t(1)), "c6827a77c10401");
    std::vector<std::vector<std::string>> a;
    a.push_back({});
    a.push_back({});
    BOOST_CHECK_EQUAL(staticEncode(a, std::vector<uint64_t>({})), "c4c2c0c0c0");

    std::vector<std::vector<std::string>> a2;
    a2.push_back(std::vector<std::string>{});
    std::vector<std::vector<std::vector<std::string>>> a3;
    a3.push_back(std::vector<std::vector<std::string>>{});
    a3.push_back(a2);
    BOOST_CHECK_EQUAL(staticEncode(std::vector<uint64_t>({}), a2, a3), "c7c0c1c0c3c0c1c0");
}

BOOST_AUTO_TEST_CASE(stringsDecode)
{
    BOOST_CHECK_EQUAL(toHex(decode<bcos::bytes>("00"sv)), "00");
    BOOST_CHECK_EQUAL(
        toHex(decode<bcos::bytes>("8D6F62636465666768696A6B6C6D"sv)), "6f62636465666768696a6b6c6d");
    BOOST_CHECK_EQUAL(toHex(decode<bcos::bytes>("C0", UnexpectedList)), "");
    BOOST_CHECK_EQUAL(decode<std::string>("83646f67"sv), "dog");
    BOOST_CHECK_EQUAL(decode<std::string>("83636174"sv), "cat");
    BOOST_CHECK_EQUAL(decode<std::string>("84646f6765"sv), "doge");
    BOOST_CHECK_EQUAL(decode<std::string>("80"sv), "");
    BOOST_CHECK_EQUAL(decode<std::string>("8ce4b8ade59bbde58aa0e6b2b9"sv), "中国加油");
    // clang-format off
    BOOST_CHECK_EQUAL(decode<std::string>("b904004c6f72656d20697073756d20646f6c6f722073697420616d65742c20636f6e73656374657475722061646970697363696e6720656c69742e20437572616269747572206d6175726973206d61676e612c20737573636970697420736564207665686963756c61206e6f6e2c20696163756c697320666175636962757320746f72746f722e2050726f696e20737573636970697420756c74726963696573206d616c6573756164612e204475697320746f72746f7220656c69742c2064696374756d2071756973207472697374697175652065752c20756c7472696365732061742072697375732e204d6f72626920612065737420696d70657264696574206d6920756c6c616d636f7270657220616c6971756574207375736369706974206e6563206c6f72656d2e2041656e65616e2071756973206c656f206d6f6c6c69732c2076756c70757461746520656c6974207661726975732c20636f6e73657175617420656e696d2e204e756c6c6120756c74726963657320747572706973206a7573746f2c20657420706f73756572652075726e6120636f6e7365637465747572206e65632e2050726f696e206e6f6e20636f6e76616c6c6973206d657475732e20446f6e65632074656d706f7220697073756d20696e206d617572697320636f6e67756520736f6c6c696369747564696e2e20566573746962756c756d20616e746520697073756d207072696d697320696e206661756369627573206f726369206c756374757320657420756c74726963657320706f737565726520637562696c69612043757261653b2053757370656e646973736520636f6e76616c6c69732073656d2076656c206d617373612066617563696275732c2065676574206c6163696e6961206c616375732074656d706f722e204e756c6c61207175697320756c747269636965732070757275732e2050726f696e20617563746f722072686f6e637573206e69626820636f6e64696d656e74756d206d6f6c6c69732e20416c697175616d20636f6e73657175617420656e696d206174206d65747573206c75637475732c206120656c656966656e6420707572757320656765737461732e20437572616269747572206174206e696268206d657475732e204e616d20626962656e64756d2c206e6571756520617420617563746f72207472697374697175652c206c6f72656d206c696265726f20616c697175657420617263752c206e6f6e20696e74657264756d2074656c6c7573206c65637475732073697420616d65742065726f732e20437261732072686f6e6375732c206d65747573206163206f726e617265206375727375732c20646f6c6f72206a7573746f20756c747269636573206d657475732c20617420756c6c616d636f7270657220766f6c7574706174"sv), "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Curabitur mauris magna, suscipit sed vehicula non, iaculis faucibus tortor. Proin suscipit ultricies malesuada. Duis tortor elit, dictum quis tristique eu, ultrices at risus. Morbi a est imperdiet mi ullamcorper aliquet suscipit nec lorem. Aenean quis leo mollis, vulputate elit varius, consequat enim. Nulla ultrices turpis justo, et posuere urna consectetur nec. Proin non convallis metus. Donec tempor ipsum in mauris congue sollicitudin. Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia Curae; Suspendisse convallis sem vel massa faucibus, eget lacinia lacus tempor. Nulla quis ultricies purus. Proin auctor rhoncus nibh condimentum mollis. Aliquam consequat enim at metus luctus, a eleifend purus egestas. Curabitur at nibh metus. Nam bibendum, neque at auctor tristique, lorem libero aliquet arcu, non interdum tellus lectus sit amet eros. Cras rhoncus, metus ac ornare cursus, dolor justo ultrices metus, at ullamcorper volutpat"sv);
    BOOST_CHECK_EQUAL(decode<std::string>("b8384c6f72656d20697073756d20646f6c6f722073697420616d65742c20636f6e7365637465747572206164697069736963696e6720656c6974"sv), "Lorem ipsum dolor sit amet, consectetur adipisicing elit"sv);
    // clang-format on
}

BOOST_AUTO_TEST_CASE(uintDecode)
{
    BOOST_CHECK_EQUAL(decode<uint64_t>("80"sv), 0u);
    BOOST_CHECK_EQUAL(decode<uint64_t>("01"sv), 1u);
    BOOST_CHECK_EQUAL(decode<uint64_t>("7f"sv), 0x7Fu);
    BOOST_CHECK_EQUAL(decode<uint64_t>("8180"sv), 0x80u);
    BOOST_CHECK_EQUAL(decode<uint64_t>("820400"sv), 0x400u);
    BOOST_CHECK_EQUAL(decode<uint64_t>("83ffccb5"sv), 0xFFCCB5u);
    BOOST_CHECK_EQUAL(decode<uint64_t>("84ffccb5dd"sv), 0xFFCCB5DDu);
    BOOST_CHECK_EQUAL(decode<uint64_t>("85CE05050505"sv), 0xCE05050505u);
    BOOST_CHECK_EQUAL(decode<uint64_t>("85ffccb5ddff"sv), 0xFFCCB5DDFFu);
    BOOST_CHECK_EQUAL(decode<uint64_t>("86ffccb5ddffee"sv), 0xFFCCB5DDFFEEu);
    BOOST_CHECK_EQUAL(decode<uint64_t>("87ffccb5ddffee14"sv), 0xFFCCB5DDFFEE14u);
    BOOST_CHECK_EQUAL(decode<uint64_t>("88ffccb5ddffee1483"sv), 0xFFCCB5DDFFEE1483u);

    decode<uint64_t>("C0", UnexpectedList);
    decode<uint64_t>("8105", NonCanonicalSize);
    decode<uint64_t>("B8020004", NonCanonicalSize);
    decode<uint64_t>("8AFFFFFFFFFFFFFFFFFF7C", Overflow);
}

BOOST_AUTO_TEST_CASE(uint256Decode)
{
    BOOST_CHECK_EQUAL(decode<u256>("80"sv), 0u);
    BOOST_CHECK_EQUAL(decode<u256>("01"sv), 1u);
    BOOST_CHECK_EQUAL(decode<u256>("7f"sv), 0x7Fu);
    BOOST_CHECK_EQUAL(decode<u256>("8180"sv), 0x80u);
    BOOST_CHECK_EQUAL(decode<u256>("820400"sv), 0x400u);
    BOOST_CHECK_EQUAL(decode<u256>("83ffccb5"sv), 0xFFCCB5u);
    BOOST_CHECK_EQUAL(decode<u256>("84ffccb5dd"sv), 0xFFCCB5DDu);
    BOOST_CHECK_EQUAL(decode<u256>("85ffccb5ddff"sv), 0xFFCCB5DDFFu);
    BOOST_CHECK_EQUAL(decode<u256>("86ffccb5ddffee"sv), 0xFFCCB5DDFFEEu);
    BOOST_CHECK_EQUAL(decode<u256>("87ffccb5ddffee14"sv), 0xFFCCB5DDFFEE14u);
    BOOST_CHECK_EQUAL(decode<u256>("88ffccb5ddffee1483"sv), 0xFFCCB5DDFFEE1483u);
    BOOST_CHECK_EQUAL(decode<u256>("8AFFFFFFFFFFFFFFFFFF7C"sv), u256("0xFFFFFFFFFFFFFFFFFF7C"));
    BOOST_CHECK_EQUAL(decode<u256>("8f10203e405060708090a0b0c0d0e0f2"sv),
        u256("0x10203E405060708090A0B0C0D0E0F2"));
    BOOST_CHECK_EQUAL(decode<u256>("9c0100020003000400050006000700080009000a0b4b000c000d000e01"sv),
        u256("0x0100020003000400050006000700080009000A0B4B000C000D000E01"));

    decode<u256>("8BFFFFFFFFFFFFFFFFFF7C"sv, InputTooShort);
    decode<u256>("C0"sv, UnexpectedList);
    decode<u256>("8105"sv, NonCanonicalSize);
    decode<u256>("B8020004"sv, NonCanonicalSize);
    decode<u256>(
        "A101000000000000000000000000000000000000008B000000000000000000000000"sv, Overflow);
}

BOOST_AUTO_TEST_CASE(vectorsDecode)
{
    BOOST_CHECK(checkVectorEqual(decode<std::vector<uint64_t>>("c0"sv), std::vector<uint64_t>{}));
    BOOST_CHECK(checkVectorEqual(decode<std::vector<uint64_t>>("c883ffccb583ffc0b5"sv),
        std::vector<uint64_t>({0xFFCCB5, 0xFFC0B5})));
    BOOST_CHECK(checkVectorEqual(decode<std::vector<uint32_t>>("c883ffccb583ffc0b5"sv),
        std::vector<uint32_t>({0xFFCCB5, 0xFFC0B5})));
    BOOST_CHECK(checkVectorEqual(decode<std::vector<std::string>>("c88363617483646f67"sv),
        std::vector<std::string>({"cat", "dog"})));
    BOOST_CHECK(checkVectorEqual(decode<std::vector<std::string>>("cc83646f6783676f6483636174"sv),
        std::vector<std::string>({"dog", "god", "cat"})));
    // multi vector test 1
    {
        std::string zw;
        std::vector<uint64_t> v;
        uint16_t one;
        auto error = decode("c6827a77c10401"sv, zw, v, one);
        BOOST_CHECK(!error);
        BOOST_CHECK_EQUAL(zw, "zw");
        BOOST_CHECK_EQUAL(v.size(), 1u);
        BOOST_CHECK_EQUAL(v[0], 4u);
        BOOST_CHECK_EQUAL(one, 1u);
    }
    // multi vector test 2
    {
        std::vector<std::vector<std::string>> a;
        std::vector<uint64_t> b;
        auto error = decode("c4c2c0c0c0"sv, a, b);
        BOOST_CHECK(!error);
        BOOST_CHECK_EQUAL(a.size(), 2u);
        BOOST_CHECK_EQUAL(a[0].size(), 0u);
        BOOST_CHECK_EQUAL(a[1].size(), 0u);
        BOOST_CHECK_EQUAL(b.size(), 0u);
    }
    // multi vector test 3
    {
        std::vector<uint64_t> b;
        std::vector<std::vector<std::string>> a2;
        std::vector<std::vector<std::vector<std::string>>> a3;
        auto error = decode("c7c0c1c0c3c0c1c0"sv, b, a2, a3);
        BOOST_CHECK(!error);
        BOOST_CHECK_EQUAL(a2.size(), 1u);
        BOOST_CHECK_EQUAL(a2[0].size(), 0u);
        BOOST_CHECK_EQUAL(a3.size(), 2u);
        BOOST_CHECK_EQUAL(a3[0].size(), 0u);
        BOOST_CHECK_EQUAL(a3[1].size(), 1u);
        BOOST_CHECK_EQUAL(a3[1][0].size(), 0u);
        BOOST_CHECK_EQUAL(b.size(), 0u);
    }
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test