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
 * @file MessageCodecTest.cpp
 * @brief MessageCodec frame-payload codec + raw-snappy tests.
 * @date 2026/9/2
 */
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-devp2p/rlpx/MessageCodec.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::devp2p::rlpx;

namespace
{
// RLP-encode the message id the same way MessageCodec::encode does.
bytes rlpMessageId(uint8_t _id)
{
    bytes out;
    bcos::codec::rlp::encode(out, static_cast<uint64_t>(_id));
    return out;
}

// Compressed frame payload: RLP(id) || uvarint(declared) || snappy block.
bytes makeCompressedFrame(uint8_t _id, uint64_t _declared, bytes const& _block)
{
    bytes out = rlpMessageId(_id);
    uint64_t value = _declared;
    while (value >= 0x80)
    {
        out.push_back(static_cast<byte>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<byte>(value));
    out.insert(out.end(), _block.begin(), _block.end());
    return out;
}

// Append an snappy literal element (len < 60) for `_data` to `_block`.
void appendLiteral(bytes& _block, std::string const& _data)
{
    BOOST_REQUIRE(_data.size() > 0 && _data.size() < 60);
    _block.push_back(static_cast<byte>((_data.size() - 1) << 2));
    _block.insert(_block.end(), _data.begin(), _data.end());
}
}  // namespace

BOOST_AUTO_TEST_SUITE(MessageCodecTest)

// Uncompressed encode/decode round trips across message ids and payload sizes.
BOOST_AUTO_TEST_CASE(roundTripUncompressed)
{
    MessageCodec codec;
    std::vector<uint8_t> const ids = {0, 1, 0x7f, 0x80, 0xff};
    std::vector<size_t> const sizes = {0, 1, 16, 100, 4096, 100000};
    for (auto id : ids)
    {
        for (auto size : sizes)
        {
            Message msg;
            msg.id = id;
            msg.data.resize(size);
            for (size_t i = 0; i < size; ++i)
            {
                msg.data[i] = static_cast<byte>((i * 31 + id) & 0xff);
            }
            auto frame = codec.encode(msg);
            Message decoded = codec.decode(bytesConstRef(frame.data(), frame.size()));
            BOOST_CHECK_EQUAL(decoded.id, id);
            BOOST_CHECK(decoded.data == msg.data);
        }
    }
}

// Compressed round trips. Sizes straddle the raw-snappy literal length-extension
// branches: <60 (single tag), 61..256 (1 length byte), 257..65536 (2 bytes) and
// >65536 (3 bytes).
BOOST_AUTO_TEST_CASE(roundTripCompressed)
{
    MessageCodec codec;
    codec.enableCompression();
    std::vector<size_t> const sizes = {0, 1, 59, 60, 61, 256, 257, 1000, 65536, 65537};
    for (auto size : sizes)
    {
        Message msg;
        msg.id = 0x11;
        msg.data.resize(size);
        for (size_t i = 0; i < size; ++i)
        {
            msg.data[i] = static_cast<byte>((i * 7 + 3) & 0xff);
        }
        auto frame = codec.encode(msg);
        Message decoded = codec.decode(bytesConstRef(frame.data(), frame.size()));
        BOOST_CHECK_EQUAL(decoded.id, 0x11);
        BOOST_CHECK(decoded.data == msg.data);
    }
}

// A compressed stream using every copy element type (copy-1/2/4), including an
// overlapping back-reference, must decompress exactly as geth/snappy would.
BOOST_AUTO_TEST_CASE(decodeCopyElements)
{
    MessageCodec codec;
    codec.enableCompression();

    // copy-1: literal "abcde" then copy-1 len=5 offset=5 (tag 0x05 0x05).
    {
        bytes block;
        appendLiteral(block, "abcde");
        block.push_back(0x05);  // copy-1 tag: len=5 (dataBits 1), offset hi 0
        block.push_back(0x05);  // offset lo
        auto frame = makeCompressedFrame(1, 10, block);
        Message decoded = codec.decode(bytesConstRef(frame.data(), frame.size()));
        BOOST_CHECK_EQUAL(std::string(decoded.data.begin(), decoded.data.end()), "abcdeabcde");
    }
    // copy-2: literal "abcd" then copy-2 len=8 offset=4 (tag 0x1e, LE offset).
    {
        bytes block;
        appendLiteral(block, "abcd");
        block.push_back(0x1e);  // copy-2 tag: len = 7 + 1 = 8
        block.push_back(0x04);  // offset LE
        block.push_back(0x00);
        auto frame = makeCompressedFrame(2, 12, block);
        Message decoded = codec.decode(bytesConstRef(frame.data(), frame.size()));
        BOOST_CHECK_EQUAL(std::string(decoded.data.begin(), decoded.data.end()), "abcdabcdabcd");
    }
    // copy-4: literal "abcd" then copy-4 len=1 offset=4 (tag 0x03, 4-byte LE).
    {
        bytes block;
        appendLiteral(block, "abcd");
        block.push_back(0x03);  // copy-4 tag: len = 0 + 1 = 1
        block.push_back(0x04);  // offset LE (4 bytes)
        block.push_back(0x00);
        block.push_back(0x00);
        block.push_back(0x00);
        auto frame = makeCompressedFrame(3, 5, block);
        Message decoded = codec.decode(bytesConstRef(frame.data(), frame.size()));
        BOOST_CHECK_EQUAL(std::string(decoded.data.begin(), decoded.data.end()), "abcda");
    }
}

// Malformed inputs must fail closed instead of over-allocating or looping.
BOOST_AUTO_TEST_CASE(decodeFailsClosed)
{
    MessageCodec codec;
    codec.enableCompression();

    // Empty frame data.
    bytes empty;
    BOOST_CHECK_THROW(codec.decode(bytesConstRef(empty.data(), empty.size())), std::runtime_error);

    // Truncated RLP message id (0x81 claims a 1-byte string, none follows).
    bytes badId = {0x81};
    BOOST_CHECK_THROW(codec.decode(bytesConstRef(badId.data(), badId.size())), std::runtime_error);

    // Truncated snappy varint (0x80 has the continuation bit set, nothing follows).
    bytes truncatedVarint = rlpMessageId(1);
    truncatedVarint.push_back(0x80);
    BOOST_CHECK_THROW(codec.decode(bytesConstRef(truncatedVarint.data(), truncatedVarint.size())),
        std::runtime_error);

    // Declared length over the 16 MiB limit.
    auto tooLarge = makeCompressedFrame(1, MessageCodec::kMaxFrameSize + 1, {});
    BOOST_CHECK_THROW(
        codec.decode(bytesConstRef(tooLarge.data(), tooLarge.size())), std::runtime_error);

    // Declared length does not match the decompressed size.
    bytes shortBlock;
    appendLiteral(shortBlock, "abc");
    auto mismatch = makeCompressedFrame(1, 10, shortBlock);
    BOOST_CHECK_THROW(
        codec.decode(bytesConstRef(mismatch.data(), mismatch.size())), std::runtime_error);

    // Block truncated: literal claims 10 bytes, only 3 are present.
    bytes truncatedBlock = {static_cast<byte>((10 - 1) << 2), 'a', 'b', 'c'};
    auto truncated = makeCompressedFrame(1, 10, truncatedBlock);
    BOOST_CHECK_THROW(
        codec.decode(bytesConstRef(truncated.data(), truncated.size())), std::runtime_error);

    // Copy with offset 0 is invalid.
    bytes zeroOffset;
    appendLiteral(zeroOffset, "abc");
    zeroOffset.push_back(0x01);  // copy-1 len=4
    zeroOffset.push_back(0x00);  // offset 0
    auto badOffset = makeCompressedFrame(1, 4, zeroOffset);
    BOOST_CHECK_THROW(
        codec.decode(bytesConstRef(badOffset.data(), badOffset.size())), std::runtime_error);

    // Copy with an offset past the already-decompressed output is invalid.
    bytes pastOffset;
    appendLiteral(pastOffset, "abc");
    pastOffset.push_back(0x01);  // copy-1 len=4
    pastOffset.push_back(0x05);  // offset 5 > 3
    auto badPast = makeCompressedFrame(1, 4, pastOffset);
    BOOST_CHECK_THROW(
        codec.decode(bytesConstRef(badPast.data(), badPast.size())), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
