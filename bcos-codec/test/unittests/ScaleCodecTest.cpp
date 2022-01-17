/*
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
 */
#include "bcos-codec/scale/Scale.h"
#include "bcos-codec/scale/ScaleDecoderStream.h"
#include "bcos-codec/scale/ScaleEncoderStream.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::codec::scale;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ScaleTest, TestPromptFixture)
/**
 * @given collection of N of type uint8_t
 * @when encode array and decode back
 * @then given equal array
 */
template <int N, typename Array = std::array<uint8_t, N>>
void testArray()
{
    for (auto value :
        {0b0000'0000, 0b0011'0011, 0b0101'0101, 0b1010'1010, 0b1100'1100, 0b1111'1111})
    {
        Array testee;
        std::fill(testee.begin(), testee.end(), value);

        auto data = encode(testee);
        auto decoded_res = decode<Array>((data));
        BOOST_CHECK(testee == decoded_res);
    }
}
BOOST_AUTO_TEST_CASE(ArrayTest)
{
    testArray<0>();
    testArray<127>();
    testArray<128>();
    testArray<255>();
    testArray<256>();
    testArray<999>();
}

struct ThreeBooleans
{
    bool b1 = false;
    bool b2 = false;
    bool b3 = false;
};

template <class Stream, typename = std::enable_if_t<Stream::is_decoder_stream>>
Stream& operator>>(Stream& s, ThreeBooleans& v)
{
    return s >> v.b1 >> v.b2 >> v.b3;
}
BOOST_AUTO_TEST_CASE(BoolTest)
{
    ScaleEncoderStream s1;
    s1 << true;
    std::vector<byte> data = {0x1};
    BOOST_CHECK(s1.data() == data);
    ScaleEncoderStream s2;
    s2 << false;
    data = {0x0};
    BOOST_CHECK(s2.data() == data);

    // exception case
    data = bytes{0, 1, 2};
    BOOST_CHECK_THROW(decode<ThreeBooleans>((data)), ScaleDecodeException);

    data = bytes{0, 1, 0};
    auto result = decode<ThreeBooleans>((data));
    BOOST_CHECK(result.b1 == false);
    BOOST_CHECK(result.b2 == true);
    BOOST_CHECK(result.b3 == false);
}
BOOST_AUTO_TEST_CASE(pairTest)
{
    uint8_t v1 = 1;
    uint32_t v2 = 2;
    ScaleEncoderStream s;
    s << std::make_pair(v1, v2);
    bytes data = {1, 2, 0, 0, 0};
    BOOST_CHECK(s.data() == data);

    data = {1, 2, 0, 0, 0};
    ScaleDecoderStream s2((data));
    using pair_type = std::pair<uint8_t, uint32_t>;
    pair_type pair{};
    s2 >> pair;
    BOOST_CHECK(pair.first == 1);
    BOOST_CHECK(pair.second == 2);
}

template <class T, class F>
void testMap(std::vector<T>& t_v, std::vector<F>& f_v, size_t _size = 1)
{
    std::map<T, F> m;
    for (size_t i = 0; i < _size; i++)
    {
        m.insert(std::make_pair(t_v[i], f_v[i]));
    }
    ScaleEncoderStream s;
    s << m;
    bytes encodeBytes = s.data();

    ScaleDecoderStream s2((encodeBytes));
    std::map<T, F> newMap;
    s2 >> newMap;

    BOOST_CHECK(_size == newMap.size());
    bool allEqual = std::equal(m.begin(), m.end(), newMap.begin(), [&](auto& pair1, auto& pair2) {
        return pair1.first == pair2.first && pair1.second == pair2.second;
    });
    BOOST_CHECK(allEqual);
}

BOOST_AUTO_TEST_CASE(mapTest)
{
    // string 2 string
    {
        std::vector<std::string> v1{"test1", "test2"};
        std::vector<std::string> v2{"test3", "test4"};
        testMap<std::string, std::string>(v1, v2, v1.size());
    }
    // string 2 bytes
    {
        std::vector<std::string> v1{"test1", "test2"};
        std::vector<bytes> v2{asBytes("test3"), asBytes("test4")};
        testMap<std::string, bytes>(v1, v2, v1.size());
    }
    // s256 2 u256
    {
        std::vector<s256> v1{0, -1, -256123};
        std::vector<u256> v2{0, 1, 256123};
        testMap<s256, u256>(v1, v2, v1.size());
    }

    // bool 2 vector
    {
        std::vector<bool> v1{true, false};
        std::vector<bytes> v2_1 = {asBytes("test1"), asBytes("test2")};
        std::vector<bytes> v2_2 = {asBytes("test3"), asBytes("test4")};
        std::vector<std::vector<bytes>> v2{v2_1, v2_2};
        testMap<bool, std::vector<bytes>>(v1, v2, v1.size());
    }

    // bytes 2 map
    {
        std::vector<bytes> v1{asBytes("test1"), asBytes("test2")};

        std::map<Address, bool> v2_1;
        v2_1.insert(std::make_pair(Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2"), true));
        v2_1.insert(std::make_pair(Address("0x120f853b49838bd3e9466c85a4cc3428c960dde2"), true));
        std::map<Address, bool> v2_2;
        v2_2.insert(std::make_pair(Address("0x420f853b49838bd3e9466c85a4cc3428c960d123"), false));
        v2_2.insert(std::make_pair(Address("0x420f853b49838bd3e9466c85a4cc3428c960d456"), true));
        std::vector<std::map<Address, bool>> v2{v2_1, v2_2};
        testMap<bytes, std::map<Address, bool>>(v1, v2, v1.size());
    }

    // map 2 map
    {
        std::map<u256, bytes> v1_1;
        v1_1.insert(std::make_pair(123, asBytes("test1")));
        std::map<u256, bytes> v1_2;
        v1_2.insert(std::make_pair(456, asBytes("test2")));
        std::vector<std::map<u256, bytes>> v1{v1_1, v1_2};

        std::map<s256, bytes> v2_1;
        v2_1.insert(std::make_pair(-789, asBytes("test3")));
        std::map<s256, bytes> v2_2;
        v2_2.insert(std::make_pair(-987, asBytes("test4")));
        std::vector<std::map<s256, bytes>> v2{v2_1, v2_2};
        testMap<std::map<u256, bytes>, std::map<s256, bytes>>(v1, v2, v1.size());
    }
}

BOOST_AUTO_TEST_CASE(testString)
{
    std::string v = "asdadad";
    ScaleEncoderStream s{};
    s << v;
    bytes data = {28, 'a', 's', 'd', 'a', 'd', 'a', 'd'};
    BOOST_CHECK(s.data() == data);

    std::string v2 = "asdadad";
    ScaleEncoderStream s2;
    s2 << v2;
    data = {28, 'a', 's', 'd', 'a', 'd', 'a', 'd'};
    BOOST_CHECK(s.data() == data);

    data = {28, 'a', 's', 'd', 'a', 'd', 'a', 'd'};
    ScaleDecoderStream s3((data));
    s3 >> v;
    BOOST_CHECK(v == "asdadad");
}
BOOST_AUTO_TEST_CASE(testTuple)
{
    {
        uint8_t v1 = 1;
        uint32_t v2 = 2;
        uint8_t v3 = 3;
        bytes expectedBytes = {1, 2, 0, 0, 0, 3};
        ScaleEncoderStream s;
        s << std::make_tuple(v1, v2, v3);
        BOOST_CHECK(s.data() == expectedBytes);
    }

    {
        bytes data = {1, 2, 0, 0, 0, 3};
        ScaleDecoderStream s2((data));
        using tuple_type = std::tuple<uint8_t, uint32_t, uint8_t>;
        tuple_type tuple{};
        s2 >> tuple;
        auto&& [v1, v2, v3] = tuple;
        BOOST_CHECK(v1 == 1);
        BOOST_CHECK(v2 == 2);
        BOOST_CHECK(v3 == 3);
    }

    using tuple_type_t = std::tuple<uint8_t, uint16_t, uint8_t, uint32_t>;
    tuple_type_t tuple = std::make_tuple(uint8_t(1), uint16_t(3), uint8_t(2), uint32_t(4));
    auto encodeBytes = encode(tuple);
    auto decodResult = decode<tuple_type_t>((encodeBytes));
    BOOST_CHECK(decodResult == tuple);
}

struct TestStruct
{
    std::string a;
    int b;
    inline bool operator==(const TestStruct& rhs) const { return a == rhs.a && b == rhs.b; }
};
template <class Stream, typename = std::enable_if_t<Stream::is_encoder_stream>>
Stream& operator<<(Stream& s, const TestStruct& test_struct)
{
    return s << test_struct.a << test_struct.b;
}

template <class Stream, typename = std::enable_if_t<Stream::is_decoder_stream>>
Stream& operator>>(Stream& s, TestStruct& test_struct)
{
    return s >> test_struct.a >> test_struct.b;
}

BOOST_AUTO_TEST_CASE(ScaleConvenienceFuncsTest)
{
    TestStruct s1{"some_string", 42};
    auto encodedData = encode((s1));
    auto decodedResult = decode<TestStruct>((encodedData));
    BOOST_CHECK(decodedResult == s1);

    std::string expectedString = "some_string";
    int expectedInt = 42;
    encodedData = encode(expectedString, expectedInt);
    decodedResult = decode<TestStruct>(encodedData);
    BOOST_CHECK(decodedResult.a == expectedString);
    BOOST_CHECK(decodedResult.b == expectedInt);
}

BOOST_AUTO_TEST_CASE(EncodeOptionalTest)
{
    // most simple case
    {
        ScaleEncoderStream s;
        s << boost::optional<uint8_t>{boost::none};
        BOOST_CHECK(s.data() == (bytes{0}));
    }
    // encode existing uint8_t
    {
        ScaleEncoderStream s;
        s << boost::optional<uint8_t>{1};
        BOOST_CHECK(s.data() == (bytes{1, 1}));
    }

    {
        // encode negative int8_t
        ScaleEncoderStream s;
        s << boost::optional<int8_t>{-1};
        BOOST_CHECK(s.data() == (bytes{1, 255}));
    }

    // encode non-existing uint16_t
    {
        ScaleEncoderStream s;
        s << boost::optional<uint16_t>{boost::none};
        BOOST_CHECK(s.data() == (bytes{0}));
    }
    // encode existing uint16_t
    {
        ScaleEncoderStream s;
        s << boost::optional<uint16_t>{511};
        BOOST_CHECK(s.data() == (bytes{1, 255, 1}));
    }
    // encode existing uint32_t
    {
        ScaleEncoderStream s;
        s << boost::optional<uint32_t>{67305985};
        BOOST_CHECK(s.data() == (bytes{1, 1, 2, 3, 4}));
    }
}

BOOST_AUTO_TEST_CASE(DecodeOptionalTest)
{
    auto data = bytes{0,  // first value
        1, 1,             // second value
        1, 255,           // third value
        0,                // fourth value
        1, 255, 1,        // fifth value
        1, 1, 2, 3, 4};   // sixth value

    auto stream = ScaleDecoderStream{data};
    // decode nullopt uint8_t
    {
        boost::optional<uint8_t> opt;
        stream >> opt;
        BOOST_CHECK(opt.has_value() == false);
    }
    // decode optional uint8_t
    {
        boost::optional<uint8_t> opt;
        stream >> opt;
        BOOST_CHECK(opt.has_value() == true);
        BOOST_CHECK(*opt == 1);
    }
    // decode optional negative int8_t
    {
        boost::optional<int8_t> opt;
        stream >> opt;
        BOOST_CHECK(opt.has_value() == true);
        BOOST_CHECK(*opt == -1);
    }
    // decode nullopt uint16_t
    // it requires 1 zero byte just like any other nullopt
    {
        boost::optional<uint16_t> opt;
        stream >> opt;
        BOOST_CHECK(opt.has_value() == false);
    }
    // decode optional uint16_t
    {
        boost::optional<uint16_t> opt;
        stream >> opt;
        BOOST_CHECK(opt.has_value() == true);
        BOOST_CHECK(*opt == 511);
    }
    // decode optional uint32_t
    {
        boost::optional<uint32_t> opt;
        stream >> opt;
        BOOST_CHECK(opt.has_value() == true);
        BOOST_CHECK(*opt == 67305985);
    }
}

struct FourOptBools
{
    boost::optional<bool> b1;
    boost::optional<bool> b2;
    boost::optional<bool> b3;
    boost::optional<bool> b4;
};
template <class Stream, typename = std::enable_if_t<Stream::is_decoder_stream>>
Stream& operator>>(Stream& s, FourOptBools& v)
{
    return s >> v.b1 >> v.b2 >> v.b3 >> v.b4;
}

BOOST_AUTO_TEST_CASE(encodeOptionalBoolSuccessTest)
{
    std::vector<boost::optional<bool>> values = {true, false, boost::none};
    ScaleEncoderStream s;
    for (auto&& v : values)
    {
        s << v;
    }
    BOOST_CHECK(s.data() == (bytes{1, 2, 0}));
    auto data = bytes{0, 1, 2, 3};
    BOOST_CHECK_THROW(decode<FourOptBools>(data), ScaleDecodeException);
    data = bytes{0, 1, 2, 1};
    using optbool = boost::optional<bool>;
    auto res = decode<FourOptBools>(data);
    BOOST_CHECK(res.b1 == boost::none);
    BOOST_CHECK(res.b2 == optbool(true));
    BOOST_CHECK(res.b3 == optbool(false));
    BOOST_CHECK(res.b4 == optbool(true));
}

BOOST_AUTO_TEST_CASE(scaleDecodeStreamTest)
{
    auto data = bytes{0, 1, 2};
    auto stream = ScaleDecoderStream{data};

    for (size_t i = 0; i < data.size(); i++)
    {
        uint8_t byteData = 255u;
        byteData = stream.nextByte();
        BOOST_CHECK(byteData == data.at(i));
    }
    BOOST_CHECK_THROW(stream.nextByte(), std::exception);
    data = bytes{0, 1};
    auto stream2 = ScaleDecoderStream{data};
    BOOST_CHECK(stream2.hasMore(0) == true);
    BOOST_CHECK(stream2.hasMore(1) == true);
    BOOST_CHECK(stream2.hasMore(2) == true);
    BOOST_CHECK(stream2.hasMore(3) == false);
    stream2.nextByte();
    BOOST_CHECK(stream2.hasMore(1) == true);
    BOOST_CHECK(stream2.hasMore(2) == false);
    stream2.nextByte();
    BOOST_CHECK(stream2.hasMore(1) == false);
    BOOST_CHECK_THROW(stream2.nextByte(), std::exception);
}


std::pair<CompactInteger, bytes> makeCompactPair(CompactInteger v, bytes m)
{
    return std::make_pair(CompactInteger(std::move(v)), std::move(m));
}

void testCompactEncodeAndDecode(std::pair<CompactInteger, bytes> _compactData)
{
    ScaleEncoderStream s;
    // encode
    const auto& [value, match] = _compactData;
    s << value;
    BOOST_CHECK(s.data() == match);
    // decode
    ScaleDecoderStream s2(gsl::make_span(match));
    CompactInteger v{};
    s2 >> v;
    BOOST_CHECK(v == value);
}

BOOST_AUTO_TEST_CASE(scaleCompactTest)
{
    auto compactPair = makeCompactPair(0, {0});
    testCompactEncodeAndDecode(compactPair);
    compactPair = makeCompactPair(1, {4});
    testCompactEncodeAndDecode(compactPair);
    compactPair = makeCompactPair(63, {252});
    testCompactEncodeAndDecode(compactPair);
    compactPair = makeCompactPair(64, {1, 1});
    testCompactEncodeAndDecode(compactPair);
    compactPair = makeCompactPair(255, {253, 3});
    testCompactEncodeAndDecode(compactPair);
    compactPair = makeCompactPair(511, {253, 7});
    testCompactEncodeAndDecode(compactPair);
    compactPair = makeCompactPair(16383, {253, 255});
    testCompactEncodeAndDecode(compactPair);
    compactPair = makeCompactPair(16384, {2, 0, 1, 0});
    testCompactEncodeAndDecode(compactPair);
    compactPair = makeCompactPair(65535, {254, 255, 3, 0});
    testCompactEncodeAndDecode(compactPair);
    compactPair = makeCompactPair(1073741823ul, {254, 255, 255, 255});
    testCompactEncodeAndDecode(compactPair);
    compactPair = makeCompactPair(CompactInteger("1234567890123456789012345678901234567890"),
        {0b110111, 210, 10, 63, 206, 150, 95, 188, 172, 184, 243, 219, 192, 117, 32, 201, 160, 3});
    testCompactEncodeAndDecode(compactPair);
    compactPair = makeCompactPair(1073741824, {3, 0, 0, 0, 64});
    testCompactEncodeAndDecode(compactPair);
    compactPair =
        makeCompactPair(CompactInteger("224945689727159819140526925384299092943484855915095831"
                                       "655037778630591879033574393515952034305194542857496045"
                                       "531676044756160413302774714984450425759043258192756735"),
            *fromHexString("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                           "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                           "FFFF"));
    testCompactEncodeAndDecode(compactPair);
}

BOOST_AUTO_TEST_CASE(testScaleEncodeFail)
{
    CompactInteger v(-1);
    ScaleEncoderStream out{};
    BOOST_CHECK_THROW((out << v), std::exception);
    BOOST_CHECK(out.data().size() == 0);  // nothing was written to buffer

    CompactInteger v2(
        "224945689727159819140526925384299092943484855915095831"
        "655037778630591879033574393515952034305194542857496045"
        "531676044756160413302774714984450425759043258192756736");  // 2^536

    ScaleEncoderStream out2;
    BOOST_CHECK_THROW((out2 << v2), std::exception);  // value is too big, it is not encoded
    BOOST_CHECK(out2.data().size() == 0);             // nothing was written to buffer

    auto data = bytes{255, 255, 255, 255};
    BOOST_CHECK_THROW(decode<CompactInteger>(data), ScaleDecodeException);
}

std::pair<boost::variant<uint8_t, uint32_t>, bytes> makeVariantPair(
    boost::variant<uint8_t, uint32_t> v, bytes m)
{
    return std::pair<boost::variant<uint8_t, uint32_t>, bytes>(std::move(v), std::move(m));
}

BOOST_AUTO_TEST_CASE(testScaleVariant)
{
    // encode uint8_t
    ScaleEncoderStream s;
    auto variantPair = makeVariantPair(uint8_t(1), {0, 1});
    const auto& [value, match] = variantPair;
    s << value;
    BOOST_CHECK(s.data() == match);
    // decode uint8_t
    ScaleDecoderStream s2(match);
    boost::variant<uint8_t, uint32_t> val{};
    s2 >> val;
    BOOST_CHECK(boost::get<uint8_t>(val) == 1);

    // encode uint32_t
    variantPair = makeVariantPair(uint32_t(2), {1, 2, 0, 0, 0});
    ScaleEncoderStream s3;
    const auto& [value2, match2] = variantPair;
    s3 << value2;
    BOOST_CHECK(s3.data() == match2);
    ScaleDecoderStream s4(match2);
    s4 >> val;
    BOOST_CHECK(boost::get<uint32_t>(val) == 2);
}


template <typename T>
std::pair<T, bytes> makeMatchPair(T value, const bytes& match)
{
    return std::make_pair(value, match);
}

template <typename T>
void testFixedWidthInteger(std::pair<T, bytes> const& _matchPair, bool _check = true)
{
    auto [value, match] = _matchPair;
    ScaleEncoderStream s;
    s << value;
    if (_check)
    {
        BOOST_CHECK(s.data() == match);
        ScaleDecoderStream s2(match);
        T v;
        s2 >> v;
        BOOST_CHECK(v == value);
    }
    std::cout << "##### value:" << std::to_string(value) << ", data:" << *toHexString(s.data())
              << std::endl;
}

BOOST_AUTO_TEST_CASE(testFixedWidthIntegerCase)
{
    // Int8Test
    std::cout << "##### int8_t test" << std::endl;
    auto fixedWidthIntegerInt8 = makeMatchPair<int8_t>(0, {0});
    testFixedWidthInteger(fixedWidthIntegerInt8);
    fixedWidthIntegerInt8 = makeMatchPair<int8_t>(-1, {255});
    testFixedWidthInteger(fixedWidthIntegerInt8);
    fixedWidthIntegerInt8 = makeMatchPair<int8_t>(-128, {128});
    testFixedWidthInteger(fixedWidthIntegerInt8);
    fixedWidthIntegerInt8 = makeMatchPair<int8_t>(-127, {129});
    testFixedWidthInteger(fixedWidthIntegerInt8);
    fixedWidthIntegerInt8 = makeMatchPair<int8_t>(123, {123});
    testFixedWidthInteger(fixedWidthIntegerInt8);
    fixedWidthIntegerInt8 = makeMatchPair<int8_t>(-15, {241});
    testFixedWidthInteger(fixedWidthIntegerInt8);
    // UInt8Test
    std::cout << "##### uint8_t test" << std::endl;
    auto fixedWidthIntegerUInt8 = makeMatchPair<uint8_t>(0, {0});
    testFixedWidthInteger(fixedWidthIntegerUInt8);
    fixedWidthIntegerUInt8 = makeMatchPair<uint8_t>(234, {234});
    testFixedWidthInteger(fixedWidthIntegerUInt8);
    fixedWidthIntegerUInt8 = makeMatchPair<uint8_t>(255, {255});
    testFixedWidthInteger(fixedWidthIntegerUInt8);
    fixedWidthIntegerUInt8 = makeMatchPair<uint8_t>(129, {255});
    testFixedWidthInteger(fixedWidthIntegerUInt8, false);
    fixedWidthIntegerUInt8 = makeMatchPair<uint8_t>(132, {255});
    testFixedWidthInteger(fixedWidthIntegerUInt8, false);
    fixedWidthIntegerUInt8 = makeMatchPair<uint8_t>(128, {255});
    testFixedWidthInteger(fixedWidthIntegerUInt8, false);
    fixedWidthIntegerUInt8 = makeMatchPair<uint8_t>(244, {255});
    testFixedWidthInteger(fixedWidthIntegerUInt8, false);
    // Int16Test
    std::cout << "##### int16_t test" << std::endl;
    auto fixedWidthIntegerInt16 = makeMatchPair<int16_t>(0, {});
    testFixedWidthInteger(fixedWidthIntegerInt16, false);
    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(-32767, {1, 128});
    testFixedWidthInteger(fixedWidthIntegerInt16);
    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(-3, {});
    testFixedWidthInteger(fixedWidthIntegerInt16, false);
    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(3, {});
    testFixedWidthInteger(fixedWidthIntegerInt16, false);
    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(128, {});
    testFixedWidthInteger(fixedWidthIntegerInt16, false);
    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(-128, {});
    testFixedWidthInteger(fixedWidthIntegerInt16, false);

    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(-1, {255, 255});
    testFixedWidthInteger(fixedWidthIntegerInt16);
    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(32767, {255, 127});
    testFixedWidthInteger(fixedWidthIntegerInt16);
    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(12345, {57, 48});
    testFixedWidthInteger(fixedWidthIntegerInt16);
    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(-12345, {199, 207});
    testFixedWidthInteger(fixedWidthIntegerInt16);
    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(255, {});
    testFixedWidthInteger(fixedWidthIntegerInt16, false);
    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(252, {});
    testFixedWidthInteger(fixedWidthIntegerInt16, false);
    fixedWidthIntegerInt16 = makeMatchPair<int16_t>(244, {});
    testFixedWidthInteger(fixedWidthIntegerInt16, false);
    // UInt16Test
    std::cout << "##### uint16_t test" << std::endl;
    auto fixedWidthIntegerUInt16 = makeMatchPair<uint16_t>(32767, {255, 127});
    testFixedWidthInteger(fixedWidthIntegerUInt16);
    fixedWidthIntegerUInt16 = makeMatchPair<uint16_t>(65535, {});
    testFixedWidthInteger(fixedWidthIntegerUInt16, false);
    fixedWidthIntegerUInt16 = makeMatchPair<uint16_t>(0, {});
    testFixedWidthInteger(fixedWidthIntegerUInt16, false);
    fixedWidthIntegerUInt16 = makeMatchPair<uint16_t>(1, {});
    testFixedWidthInteger(fixedWidthIntegerUInt16, false);

    fixedWidthIntegerUInt16 = makeMatchPair<uint16_t>(128, {});
    testFixedWidthInteger(fixedWidthIntegerUInt16, false);
    fixedWidthIntegerUInt16 = makeMatchPair<uint16_t>(255, {});
    testFixedWidthInteger(fixedWidthIntegerUInt16, false);
    fixedWidthIntegerUInt16 = makeMatchPair<uint16_t>(256, {});
    testFixedWidthInteger(fixedWidthIntegerUInt16, false);
    fixedWidthIntegerUInt16 = makeMatchPair<uint16_t>(12345, {57, 48});
    testFixedWidthInteger(fixedWidthIntegerUInt16);
    // Int32Test

    std::cout << "##### int32_t test" << std::endl;
    auto fixedWidthIntegerInt32 = makeMatchPair<int32_t>(2147483647l, {255, 255, 255, 127});
    testFixedWidthInteger(fixedWidthIntegerInt32);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(0, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(-3, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(3, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(252, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(-252, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(255, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(-255, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(256, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(-256, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(257, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(-257, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(65535, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(-65535, {});
    testFixedWidthInteger(fixedWidthIntegerInt32, false);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(-1, {255, 255, 255, 255});
    testFixedWidthInteger(fixedWidthIntegerInt32);
    fixedWidthIntegerInt32 = makeMatchPair<int32_t>(1, {1, 0, 0, 0});
    testFixedWidthInteger(fixedWidthIntegerInt32);
    // Uint32Test
    std::cout << "##### uint32_t test" << std::endl;
    auto fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(16909060ul, {4, 3, 2, 1});
    testFixedWidthInteger(fixedWidthIntegerUInt32);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(0, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(1, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(2, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(127, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(128, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(129, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(255, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(256, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(257, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(65535, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(65536, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(65537, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);
    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(2147483647ul, {});
    testFixedWidthInteger(fixedWidthIntegerUInt32, false);

    fixedWidthIntegerUInt32 = makeMatchPair<uint32_t>(67305985, {1, 2, 3, 4});
    testFixedWidthInteger(fixedWidthIntegerUInt32);
    // Int64Test
    std::cout << "##### int64_t test" << std::endl;
    auto fixedWidthIntegerInt64 =
        makeMatchPair<int64_t>(578437695752307201ll, {1, 2, 3, 4, 5, 6, 7, 8});
    testFixedWidthInteger(fixedWidthIntegerInt64);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-1, {255, 255, 255, 255, 255, 255, 255, 255});
    testFixedWidthInteger(fixedWidthIntegerInt64);

    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-1, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(1, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-127, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(127, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-128, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(128, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(129, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-129, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-255, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(255, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(256, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-256, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(257, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-257, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(65535, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-65535, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);

    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(65536, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-65536, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(65537, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-65537, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-2147483647, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(2147483647, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-2147483648, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(2147483648, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-2147483649, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(2147483649, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(67305985, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(-67305985, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);
    fixedWidthIntegerInt64 = makeMatchPair<int64_t>(0, {});
    testFixedWidthInteger(fixedWidthIntegerInt64, false);


    // UInt64Test
    std::cout << "##### uint64_t test" << std::endl;
    auto fixedWidthIntegerUInt64 =
        makeMatchPair<uint64_t>(578437695752307201ull, {1, 2, 3, 4, 5, 6, 7, 8});
    testFixedWidthInteger(fixedWidthIntegerUInt64);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(0, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(1, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(127, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(128, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(129, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(255, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(256, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(257, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(65535, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(65536, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(65537, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(2147483647, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(2147483648, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
    fixedWidthIntegerUInt64 = makeMatchPair<uint64_t>(2147483649, {});
    testFixedWidthInteger(fixedWidthIntegerUInt64, false);
}

BOOST_AUTO_TEST_CASE(testCollections)
{
    {
        // 80 items of value 1
        bytes collection(80, 1);
        auto match = bytes{65, 1};  // header
        match.insert(match.end(), collection.begin(), collection.end());
        ScaleEncoderStream s;
        s << collection;
        auto&& out = s.data();
        BOOST_CHECK(out.size() == 82);
        BOOST_CHECK(out == match);
    }

    {
        // test uint16_t collections
        std::vector<uint16_t> collectionInt16 = {1, 2, 3, 4};
        ScaleEncoderStream s;
        s << collectionInt16;
        auto&& out = s.data();
        // clang-format off
  BOOST_CHECK(out ==
          (bytes{
              16,  // header
            1, 0,  // first item
            2, 0,  // second item
            3, 0,  // third item
            4, 0  // fourth item
              }));
}
{
    // test uint32_t collections
    std::vector<uint32_t> collectionUint32 = {50462976, 117835012, 185207048,
                                      252579084};
  ScaleEncoderStream s;
  s << collectionUint32;
  auto &&out = s.data();
  // clang-format off
  BOOST_CHECK(out ==
            (bytes{
                    16,                // header
                    0, 1, 2, 3,        // first item
                    4, 5, 6, 7,        // second item
                    8, 9, 0xA, 0xB,    // third item
                    0xC, 0xD, 0xE, 0xF // fourth item
            }));
}
{
    // test uint64_t collections
std::vector<uint64_t> collection = {506097522914230528ull,
                                      1084818905618843912ull};
  ScaleEncoderStream s;
  s << collection;
  auto &&out = s.data();
  // clang-format off
  BOOST_CHECK(out ==
            (bytes{
                    8,                // header
                    0, 1, 2, 3,        // first item
                    4, 5, 6, 7,        // second item
                    8, 9, 0xA, 0xB,    // third item
                    0xC, 0xD, 0xE, 0xF // fourth item
            }));
}

// test uint16_t collections
{
std::vector<uint16_t> collection;
  auto length = 16384;
  collection.reserve(length);
  for (auto i = 0; i < length; ++i) {
    collection.push_back(i % 256);
  }
  ScaleEncoderStream s;
  s << collection;
  auto &&out = s.data();
  BOOST_CHECK((size_t)out.size() == (size_t)(length * 2 + 4));
  // header takes 4 byte,
  // first 4 bytes represent le-encoded value 2^16 + 2
  // which is compact-encoded value 2^14 = 16384
  auto stream = ScaleDecoderStream(gsl::make_span(out));
  CompactInteger res;
  stream >> res;
  BOOST_CHECK(res == 16384);
  // now only 32768 bytes left in stream
  BOOST_CHECK(stream.hasMore(32768)== true);
  BOOST_CHECK(stream.hasMore(32769)== false);
  for (auto i = 0; i < length; ++i) {
    uint8_t data = 0u;
    stream >> data;
    BOOST_CHECK(data == i % 256);
    stream >> data;
    BOOST_CHECK(data == 0);
  }
  BOOST_CHECK(stream.hasMore(1) == false);
}

{
// test very long collections
/**
 * @given very long collection of items of type uint8_t containing 2^20 items
 * this number takes ~ 1 Mb of data
 * where collection[i]  == i % 256
 * @when encodeCollection is applied
 * @then obtain byte array of length 1048576 + 4 bytes (header) bytes
 * where first bytes repreent header, other are data itself
 * where each byte after header == i%256
 */
auto length = 1048576;  // 2^20
  std::vector<uint8_t> collection;
  collection.reserve(length);

  for (auto i = 0; i < length; ++i) {
    collection.push_back(i % 256);
  }
  ScaleEncoderStream s;
  s << collection;
  auto &&out = s.data();
  BOOST_CHECK((size_t)out.size() == (size_t)(length + 4));
  // header takes 4 bytes,
  // first byte == (4-4) + 3 = 3,
  // which means that number of items requires 4 bytes
  // 3 next bytes are 0, and the last 4-th == 2^6 == 64
  // which is compact-encoded value 2^14 = 16384
  auto stream = ScaleDecoderStream(gsl::make_span(out));
  CompactInteger bi;
  stream >> bi;
  BOOST_CHECK(bi == 1048576);

  // now only 1048576 bytes left in stream
  BOOST_CHECK(stream.hasMore(1048576) == true);
  BOOST_CHECK(stream.hasMore(1048576 + 1) == false);

  for (auto i = 0; i < length; ++i) {
    uint8_t data{0u};
    stream >> data;
    BOOST_CHECK(data == i % 256);
  }
  BOOST_CHECK(stream.hasMore(1) ==false);
}
}

template<typename T>
void printData(T const& _data)
{
    ScaleEncoderStream encoder;
    encoder << _data;
    auto &&out = encoder.data();
    T decodedNumber;
    ScaleDecoderStream decoder(gsl::make_span(out));
    decoder >> decodedNumber;
    BOOST_CHECK(_data == decodedNumber);
    std::cout << "#### value:" << _data << ", encoded:" << *toHexString(encoder.data()) << std::endl;
}
BOOST_AUTO_TEST_CASE(testU256)
{
    u256 number = 3453456346534;
    ScaleEncoderStream encoder;
    // encode
    encoder << number;
    // decode
    u256 decodedNumber;
    auto &&out = encoder.data();
    ScaleDecoderStream decoder(gsl::make_span(out));
    decoder >> decodedNumber;
    std::cout << "#### number:" << number << ", decodedNumber:" << decodedNumber << std::endl;
    BOOST_CHECK(number == decodedNumber);

    CompactInteger number2("123");
    ScaleEncoderStream encoder2;
    encoder2 << number2;
    auto &&out2 = encoder2.data();
    ScaleDecoderStream decoder2(gsl::make_span(out2));
    CompactInteger decodedNumber2;
    decoder2 >> decodedNumber2;
    std::cout << "#### number2:" << number2 << ", decodedNumber2:" << decodedNumber2 << std::endl;
    BOOST_CHECK(number2 == decodedNumber2);

    std::cout << "##### u256 test" << std::endl;
    printData((u256)0);
    printData((u256)1);
    printData((u256)127);
    printData((u256)128);
    printData((u256)129);
    printData((u256)255);
    printData((u256)256);
    printData((u256)257);
    printData((u256)65535);
    printData((u256)65536);
    printData((u256)65537);
    printData((u256)2147483647);
    printData((u256)2147483648);
    printData((u256)2147483649);
    printData((u256)123123122147483649);
    std::cout << "##### u256 test end" << std::endl;
}
BOOST_AUTO_TEST_CASE(tests256)
{
    s256 number = 3453456346534;
    ScaleEncoderStream encoder;
    // encode
    encoder << number;
    // decode
    s256 decodedNumber;
    auto &&out = encoder.data();
    ScaleDecoderStream decoder(gsl::make_span(out));
    decoder >> decodedNumber;
    std::cout << "#### number:" << number << ", decodedNumber:" << decodedNumber << std::endl;
    BOOST_CHECK(number == decodedNumber);


    s256 number2 = -3453456346534;
    ScaleEncoderStream encoder2;
    // encode
    encoder2 << number2;
    // decode
    s256 decodedNumber2;
    auto &&out2 = encoder2.data();
    ScaleDecoderStream decoder2(gsl::make_span(out2));
    decoder2 >> decodedNumber2;
    std::cout << "#### number2:" << number2 << ", decodedNumber2:" << decodedNumber2 << std::endl;
    BOOST_CHECK(number2 == decodedNumber2);

    std::cout << "##### s256 test" << std::endl;
    printData((s256)0);
    printData((s256)1);
    printData((s256)127);
    printData((s256)128);
    printData((s256)129);
    printData((s256)255);
    printData((s256)256);
    printData((s256)257);
    printData((s256)65535);
    printData((s256)65536);
    printData((s256)65537);
    printData((s256)2147483647);
    printData((s256)2147483648);
    printData((s256)2147483649);
    printData((s256)123123122147483649);

    printData((s256)-1);
    printData((s256)-127);
    printData((s256)-128);
    printData((s256)-129);
    printData((s256)-255);
    printData((s256)-256);
    printData((s256)-257);
    printData((s256)-65535);
    printData((s256)-65536);
    printData((s256)-65537);
    printData((s256)-2147483647);
    printData((s256)-2147483648);
    printData((s256)-2147483649);
    printData((s256)-123123122147483649);
    std::cout << "##### s256 test end" << std::endl;
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
