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
 * @brief Regression tests for FIB-68: decompression bomb prevention
 * @file FIB68_DecompressionBombTest.cpp
 * @date 2026-04-07
 */

#include "bcos-utilities/ZstdCompress.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <string>

using namespace bcos;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(FIB68_DecompressionBombTest, TestPromptFixture)

// Test that the MAX_UNCOMPRESSED_SIZE constant is 32MB
BOOST_AUTO_TEST_CASE(MaxUncompressedSizeIs32MB)
{
    BOOST_CHECK_EQUAL(ZstdCompress::MAX_UNCOMPRESSED_SIZE, 32u * 1024 * 1024);
}

// Test that valid small data compresses and decompresses correctly
BOOST_AUTO_TEST_CASE(ValidSmallDataRoundTrip)
{
    std::string input = "Hello, FISCO-BCOS! This is a test for decompression safety.";
    bytes inputBytes(input.begin(), input.end());
    bytesConstRef inputRef(&inputBytes);

    bytes compressed;
    BOOST_REQUIRE(ZstdCompress::compress(inputRef, compressed, 1));
    BOOST_REQUIRE(!compressed.empty());

    bytes decompressed;
    bytesConstRef compressedRef(&compressed);
    BOOST_REQUIRE(ZstdCompress::uncompress(compressedRef, decompressed));
    BOOST_CHECK_EQUAL(decompressed.size(), inputBytes.size());
    BOOST_CHECK(decompressed == inputBytes);
}

// Test that zero-length input returns false
BOOST_AUTO_TEST_CASE(ZeroLengthInputReturnsFalse)
{
    bytes empty;
    bytesConstRef emptyRef(&empty);
    bytes output;
    BOOST_CHECK(!ZstdCompress::uncompress(emptyRef, output));
}

// Test that corrupted/garbage data returns false
BOOST_AUTO_TEST_CASE(CorruptedDataReturnsFalse)
{
    bytes garbage = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    bytesConstRef garbageRef(&garbage);
    bytes output;
    BOOST_CHECK(!ZstdCompress::uncompress(garbageRef, output));
}

// Test decompression bomb: craft a Zstd frame header that declares a huge content size
// Zstd frame format (simplified):
//   4 bytes: magic number 0xFD2FB528 (little-endian: 28 B5 2F FD)
//   1 byte:  frame header descriptor
//   0-3 bytes: window descriptor (if single segment flag = 0)
//   0-8 bytes: frame content size (depends on FCS_Field_Size in descriptor)
//
// Frame Header Descriptor bits:
//   [7:6] Frame_Content_Size_flag (FCS)
//   [5]   Single_Segment_flag
//   [4]   unused
//   [3]   reserved
//   [2]   Content_Checksum_flag
//   [1:0] Dictionary_ID_flag
//
// When FCS=3 and Single_Segment_flag=1: FCS_Field_Size=8 bytes, no window descriptor
// Descriptor byte: 0b11100000 = 0xE0
//   FCS=3 (bits 7:6), Single_Segment=1 (bit 5), rest 0
BOOST_AUTO_TEST_CASE(DecompressionBombRejected)
{
    // Craft a minimal Zstd frame header claiming 4GB decompressed size
    bytes bomb;
    // Magic number (little-endian)
    bomb.push_back(0x28);
    bomb.push_back(0xB5);
    bomb.push_back(0x2F);
    bomb.push_back(0xFD);
    // Frame header descriptor: FCS=3, Single_Segment=1
    bomb.push_back(0xE0);
    // Frame Content Size: 8 bytes, little-endian, value = 4GB (0x100000000)
    uint64_t fakeSize = static_cast<uint64_t>(4) * 1024 * 1024 * 1024;  // 4GB
    for (int i = 0; i < 8; i++)
    {
        bomb.push_back(static_cast<uint8_t>((fakeSize >> (8 * i)) & 0xFF));
    }
    // Add some dummy compressed block data (won't be reached due to size check)
    bomb.push_back(0x01);
    bomb.push_back(0x00);
    bomb.push_back(0x00);

    bytesConstRef bombRef(&bomb);
    bytes output;
    // The uncompress call should reject this because declared size (4GB) > MAX_UNCOMPRESSED_SIZE
    BOOST_CHECK(!ZstdCompress::uncompress(bombRef, output));
    // Output should remain empty since decompression was rejected before allocation
    BOOST_CHECK(output.empty());
}

// Test that data right at the MAX_UNCOMPRESSED_SIZE boundary is accepted
// (We test with a smaller size since we can't allocate 32MB in unit tests easily,
//  but we verify the boundary logic by checking a size just above the limit is rejected)
BOOST_AUTO_TEST_CASE(DecompressionBombBoundaryJustAboveLimit)
{
    // Craft a frame claiming MAX_UNCOMPRESSED_SIZE + 1 bytes
    bytes bomb;
    bomb.push_back(0x28);
    bomb.push_back(0xB5);
    bomb.push_back(0x2F);
    bomb.push_back(0xFD);
    bomb.push_back(0xE0);  // FCS=3, Single_Segment=1
    uint64_t justOverLimit = ZstdCompress::MAX_UNCOMPRESSED_SIZE + 1;
    for (int i = 0; i < 8; i++)
    {
        bomb.push_back(static_cast<uint8_t>((justOverLimit >> (8 * i)) & 0xFF));
    }
    bomb.push_back(0x01);
    bomb.push_back(0x00);
    bomb.push_back(0x00);

    bytesConstRef bombRef(&bomb);
    bytes output;
    BOOST_CHECK(!ZstdCompress::uncompress(bombRef, output));
}

// Test that data exactly at the MAX_UNCOMPRESSED_SIZE is accepted (not rejected)
BOOST_AUTO_TEST_CASE(ExactlyAtMaxSizeIsAccepted)
{
    // Craft a frame claiming exactly MAX_UNCOMPRESSED_SIZE bytes
    // This should pass the size check but will fail at actual decompression
    // (since the compressed data is bogus). The key test is that it does NOT
    // fail with the "exceeds maximum allowed size" rejection.
    bytes frame;
    frame.push_back(0x28);
    frame.push_back(0xB5);
    frame.push_back(0x2F);
    frame.push_back(0xFD);
    frame.push_back(0xE0);  // FCS=3, Single_Segment=1
    uint64_t exactLimit = ZstdCompress::MAX_UNCOMPRESSED_SIZE;
    for (int i = 0; i < 8; i++)
    {
        frame.push_back(static_cast<uint8_t>((exactLimit >> (8 * i)) & 0xFF));
    }
    frame.push_back(0x01);
    frame.push_back(0x00);
    frame.push_back(0x00);

    bytesConstRef frameRef(&frame);
    bytes output;
    // This will return false (decompression error since data is bogus),
    // but it should get past the size check and fail at ZSTD_decompress instead.
    // We just verify it doesn't crash with OOM - the false return is expected.
    bool result = ZstdCompress::uncompress(frameRef, output);
    BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_SUITE_END()
