/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief A/B equivalence tests for the limb-level toBigEndian/fromBigEndian fast path against
 *        the original byte-shift loops (kept here verbatim as the oracle).
 * @file DataConvertFastPathTest.cpp
 */
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/FixedBytes.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <array>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace bcos;

namespace bcos::test
{
// the fast path must engage exactly on 64-bit-limb platforms — a silent dispatch failure (like
// the unsigned-vs-size_t NTTP mismatch GCC rejects) must be a compile error, not a slowdown
static_assert(detail::LimbReadable<u256> == (sizeof(boost::multiprecision::limb_type) == 8));
static_assert(detail::LimbReadable<u160> == (sizeof(boost::multiprecision::limb_type) == 8));
static_assert(detail::LimbReadable<u512> == (sizeof(boost::multiprecision::limb_type) == 8));

namespace
{
// The pre-fast-path implementations, copied verbatim: the semantic oracle, including the
// container-decides-width / silent-truncation behaviour on both directions.
template <class T, class Out>
void referenceToBigEndian(T _val, Out& o_out)
{
    for (auto i = o_out.size(); i != 0; _val >>= 8, i--)
    {
        T v = _val & (T)0xff;
        o_out[i - 1] = (typename Out::value_type)(uint8_t)v;
    }
}

template <class T, class In>
T referenceFromBigEndian(In const& _bytes)
{
    T ret = (T)0;
    for (auto i : _bytes)
        ret = (T)((ret << 8) | (byte)(typename std::make_unsigned<decltype(i)>::type)i);
    return ret;
}

// Deterministic value corpus: boundaries plus randoms of varied bit width (real workloads are
// mostly 64-128 bit values, and the partial-top-limb edges need coverage).
template <class T>
std::vector<T> valueCorpus(unsigned bits, unsigned randomCount)
{
    std::mt19937_64 rng(0xF15C0BC05u + bits);
    std::vector<T> corpus{T(0), T(1), T(0xff), T(1) << (bits - 1), ~T(0), ~T(0) - 1,
        T(std::numeric_limits<uint64_t>::max()), T(1) << 64, (T(1) << 64) - 1};
    for (unsigned n = 0; n < randomCount; ++n)
    {
        T val = 0;
        for (unsigned filled = 0; filled < bits; filled += 64)
        {
            val <<= 64;
            val |= T(rng());
        }
        unsigned width = 1 + (unsigned)(rng() % bits);
        corpus.push_back(val >> (bits - width));
    }
    return corpus;
}

constexpr std::array<size_t, 16> c_containerSizes{
    0, 1, 2, 7, 8, 9, 19, 20, 21, 31, 32, 33, 40, 64, 72, 80};

template <class T>
void checkToBigEndianEquivalence(unsigned bits)
{
    for (auto const& val : valueCorpus<T>(bits, 200))
    {
        for (auto size : c_containerSizes)
        {
            bytes refBytes(size, 0xaa);
            bytes fastBytes(size, 0xbb);
            referenceToBigEndian(val, refBytes);
            toBigEndian(val, fastBytes);
            BOOST_REQUIRE(refBytes == fastBytes);

            std::string refString(size, 'x');
            std::string fastString(size, 'y');
            referenceToBigEndian(val, refString);
            toBigEndian(val, fastString);
            BOOST_REQUIRE(refString == fastString);
        }
        // std::array is the FixedBytes lane (exact width, contiguous whole-limb stores)
        if constexpr (std::is_same_v<T, u256>)
        {
            std::array<uint8_t, 32> refArray{};
            std::array<uint8_t, 32> fastArray{};
            referenceToBigEndian(val, refArray);
            toBigEndian(val, fastArray);
            BOOST_REQUIRE(refArray == fastArray);
        }
    }
}

template <class T>
void checkFromBigEndianEquivalence(unsigned bits)
{
    std::mt19937_64 rng(0xBEEF0000u + bits);
    for (auto size : c_containerSizes)
    {
        for (unsigned n = 0; n < 50; ++n)
        {
            bytes input(size);
            for (auto& byteValue : input)
            {
                byteValue = (uint8_t)rng();
            }
            BOOST_REQUIRE(fromBigEndian<T>(input) == referenceFromBigEndian<T>(input));

            std::string inputString(input.begin(), input.end());
            BOOST_REQUIRE(fromBigEndian<T>(inputString) == referenceFromBigEndian<T>(inputString));

            bytesConstRef inputRef(input.data(), input.size());
            BOOST_REQUIRE(fromBigEndian<T>(inputRef) == referenceFromBigEndian<T>(inputRef));
        }
        bytes allOnes(size, 0xff);
        BOOST_REQUIRE(fromBigEndian<T>(allOnes) == referenceFromBigEndian<T>(allOnes));
    }
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(DataConvertFastPathTests, TestPromptFixture)

BOOST_AUTO_TEST_CASE(toBigEndianEquivalence)
{
    checkToBigEndianEquivalence<u256>(256);
    checkToBigEndianEquivalence<u160>(160);
    checkToBigEndianEquivalence<u512>(512);
}

BOOST_AUTO_TEST_CASE(fromBigEndianEquivalence)
{
    checkFromBigEndianEquivalence<u256>(256);
    checkFromBigEndianEquivalence<u160>(160);
    checkFromBigEndianEquivalence<u512>(512);
}

BOOST_AUTO_TEST_CASE(roundTripAndNamedOverloads)
{
    for (auto const& val : valueCorpus<u256>(256, 200))
    {
        bytes encoded = toBigEndian(val);
        BOOST_REQUIRE_EQUAL(encoded.size(), 32);
        bytes reference(32, 0);
        referenceToBigEndian(val, reference);
        BOOST_REQUIRE(encoded == reference);
        BOOST_REQUIRE(fromBigEndian<u256>(encoded) == val);
    }
    for (auto const& val : valueCorpus<u160>(160, 200))
    {
        bytes encoded = toBigEndian(val);
        BOOST_REQUIRE_EQUAL(encoded.size(), 20);
        bytes reference(20, 0);
        referenceToBigEndian(val, reference);
        BOOST_REQUIRE(encoded == reference);
        BOOST_REQUIRE(fromBigEndian<u160>(encoded) == val);
    }
}

BOOST_AUTO_TEST_CASE(fixedBytesImplicitBridge)
{
    // FixedBytes.h:179 (u256 -> h256 ctor) and :199 (h256 -> u256 operator) route through the
    // same templates; the round trip must stay the identity on full-width values
    for (auto const& val : valueCorpus<u256>(256, 200))
    {
        h256 asHash(val);
        BOOST_REQUIRE((u256)asHash == val);
    }
}

BOOST_AUTO_TEST_CASE(cArrayInput)
{
    // the fromEvmC shape: fromBigEndian over a raw uint8_t[32]
    std::mt19937_64 rng(0xCAFE);
    for (unsigned n = 0; n < 200; ++n)
    {
        uint8_t raw[32];
        for (auto& byteValue : raw)
        {
            byteValue = (uint8_t)rng();
        }
        BOOST_REQUIRE(fromBigEndian<u256>(raw) == referenceFromBigEndian<u256>(raw));
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
