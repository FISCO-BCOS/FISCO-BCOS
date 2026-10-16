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
 * @brief Test-local frame format + FrameDecoder for bcos-network's own test suites.
 * @file TestFraming.h
 *
 * bcos-network is a format-agnostic framed transport: the FrameDecoder seam is designed so
 * every consumer brings its own wire format (gateway: libp2p's P2PDecoder; devp2p: the RLPx
 * decoder). The library's own tests must therefore not depend on any production consumer's
 * format — they instantiate the library through the seam with this minimal test format:
 *
 *     [u32 big-endian total frame length][payload ...]     (length includes the 4-byte prefix)
 *
 * A declared length below the prefix size desynchronizes the stream and maps to
 * FrameMeta::Status::ProtocolError; a length beyond the received bytes asks for more data
 * (declaredLength drives the read-loop grow path); frames at or above
 * FRAME_TAKE_BUFFER_THRESHOLD take the buffer-swap path (FrameMeta::takeBuffer), exactly like
 * a production large-frame decoder. The frame CONTENT is opaque to the library, so the tests
 * below reuse the payload shapes of the gateway regressions they were migrated from (random
 * 0xff payloads, P2P-shaped headers that the session must not interpret, ...).
 */
#pragma once

#include "bcos-network/FrameMeta.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <queue>
#include <vector>

namespace bcos::network::test
{
class TestFrameDecoder
{
public:
    static constexpr uint32_t FRAME_HEADER_SIZE = 4;

    // Splits the [length:4][payload] stream. Contract (see FrameMeta.h): bounds-safe on
    // arbitrary attacker-controlled input, never throws.
    FrameMeta tryDecode(const bytesConstRef& buffer)
    {
        FrameMeta meta;
        if (buffer.size() < FRAME_HEADER_SIZE)
        {
            return meta;  // NeedMoreData, declaredLength stays 0 (smaller than any frame)
        }

        auto length = readLength(buffer.data());
        if (length < FRAME_HEADER_SIZE)
        {
            // The declared length cannot even cover the prefix: the stream is desynchronized.
            meta.status = FrameMeta::Status::ProtocolError;
            return meta;
        }
        if (length > buffer.size())
        {
            meta.declaredLength = length;
            return meta;  // NeedMoreData
        }

        meta.status = FrameMeta::Status::Frame;
        meta.consumed = length;
        if (length >= FRAME_TAKE_BUFFER_THRESHOLD)
        {
            // Large frame: leave `frame` empty and let the read loop move the whole receive
            // buffer over (FrameMeta::takeBuffer), instead of copying the frame out.
            meta.takeBuffer = true;
        }
        else
        {
            meta.frame = bytes(buffer.data(), buffer.data() + length);
        }
        return meta;
    }

    static uint32_t readLength(const byte* _data)
    {
        return (uint32_t(_data[0]) << 24) | (uint32_t(_data[1]) << 16) |
               (uint32_t(_data[2]) << 8) | uint32_t(_data[3]);
    }

    static void writeLength(byte* _data, uint32_t _length)
    {
        _data[0] = byte(_length >> 24);
        _data[1] = byte(_length >> 16);
        _data[2] = byte(_length >> 8);
        _data[3] = byte(_length);
    }
};

// Build one wire frame from a payload: [u32 BE total length][payload].
inline bytes buildTestFrame(bytes const& _payload)
{
    bytes frame;
    frame.resize(TestFrameDecoder::FRAME_HEADER_SIZE + _payload.size());
    TestFrameDecoder::writeLength(frame.data(), static_cast<uint32_t>(frame.size()));
    std::copy(_payload.begin(), _payload.end(), frame.begin() + TestFrameDecoder::FRAME_HEADER_SIZE);
    return frame;
}

// A frame whose declared length (< prefix) makes the decoder report ProtocolError. Used to
// drive the read loop's decode-error teardown (the successor of the gateway suite's
// bad-version frame — the test decoder has no protocol fields, so the malformed LENGTH is the
// format-intrinsic error trigger).
inline bytes buildProtocolErrorFrame()
{
    bytes frame(TestFrameDecoder::FRAME_HEADER_SIZE, 0);
    TestFrameDecoder::writeLength(frame.data(), TestFrameDecoder::FRAME_HEADER_SIZE - 1);
    return frame;
}

// Builds `_frameNum` random wire frames, concatenates them into one byte stream and chops that
// stream into random packets (1 ~ 127 bytes) — the read loop must reassemble frames that
// straddle packet boundaries no matter how the TCP segments arrive. Migrated from the gateway
// suite's FakeMessagesBuilder; kept here so the read-loop tests exercise the same shape.
class TestFrameBuilder
{
public:
    using Packet = std::shared_ptr<std::vector<uint8_t>>;

    explicit TestFrameBuilder(std::size_t _frameNum)
    {
        for (std::size_t i = 0; i < _frameNum; ++i)
        {
            // 0 ~ 240 payload bytes: frames are 4 ~ 244 bytes
            uint8_t randPayloadSize = rand() % 0xf1;  // NOLINT(concurrency-*)
            auto frame = buildFrame(randPayloadSize);
            m_sendBuffer.insert(m_sendBuffer.end(), frame->begin(), frame->end());
        }

        // cut the stream randomly into packets
        std::size_t offset = 0;
        while (offset < m_sendBuffer.size())
        {
            uint8_t randPacketSize = (rand() % 0x7f) + 1;  // NOLINT(concurrency-*) 1 ~ 127
            std::size_t packetSize =
                std::min(std::size_t(randPacketSize), m_sendBuffer.size() - offset);
            auto packet = std::make_shared<std::vector<uint8_t>>(
                m_sendBuffer.begin() + offset, m_sendBuffer.begin() + offset + packetSize);
            m_sendPackets.push(packet);
            offset += packetSize;
        }
    }

    Packet nextPacket()
    {
        if (m_sendPackets.empty())
        {
            return nullptr;
        }
        auto packet = m_sendPackets.front();
        m_sendPackets.pop();
        return packet;
    }

    bool isSameSendBuffer(const bytesConstRef& _buffer)
    {
        return _buffer.size() == m_sendBuffer.size() &&
               std::equal(m_sendBuffer.begin(), m_sendBuffer.end(), _buffer.begin());
    }

    size_t sendBufferSize() { return m_sendBuffer.size(); }

private:
    Packet buildFrame(uint8_t _payloadSize)
    {
        // [length:4][payload] where the payload is all 0xff: a frame the tests can verify
        // byte-for-byte after reassembly.
        auto packet = std::make_shared<std::vector<uint8_t>>(
            TestFrameDecoder::FRAME_HEADER_SIZE + _payloadSize, uint8_t(0xff));
        TestFrameDecoder::writeLength(
            packet->data(), static_cast<uint32_t>(packet->size()));
        return packet;
    }

    std::vector<uint8_t> m_sendBuffer;
    std::queue<Packet> m_sendPackets;
};
}  // namespace bcos::network::test
