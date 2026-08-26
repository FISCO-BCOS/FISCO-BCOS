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
 * @file MessageCodec.cpp
 * @brief Message <-> frame payload codec implementation.
 * @date 2026/8/18
 */
#include "MessageCodec.h"

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace bcos::devp2p::rlpx
{
namespace
{
// --- Raw snappy (the wire format geth/erigon/reth/ethrex all use for devp2p
// frame compression). The snappy "raw block" format is:
//
//   raw-block = uvarint(uncompressed-length) || sequence-of-tags
//
// Each tag byte is split as:  type = tag & 3   (LOW two bits)
//                             data = tag >> 2  (high six bits)
//   literal (type 0) : len = data + 1          (data < 60); if data in 60..63,
//                      len = 1 + LE(data-59 length bytes), then the bytes.
//   copy-1  (type 1) : len = (data & 7) + 4, offset = ((data >> 3) << 8) | next
//   copy-2  (type 2) : len = data + 1,      offset = next 2 bytes LE
//   copy-4  (type 3) : len = data + 1,      offset = next 4 bytes LE
//
// This is exactly what the vcpkg snappy port (RawCompress/RawUncompress), Go's
// snappy.Decode (geth) and Rust snap::raw (ethrex) emit/consume.
void appendUvarint(bcos::bytes& _out, uint64_t _value)
{
    while (_value >= 0x80)
    {
        _out.push_back(static_cast<bcos::byte>((_value & 0x7F) | 0x80));
        _value >>= 7;
    }
    _out.push_back(static_cast<bcos::byte>(_value));
}

uint64_t readUvarint(bytesConstRef _data, size_t& _pos)
{
    uint64_t value = 0;
    uint32_t shift = 0;
    while (true)
    {
        if (_pos >= _data.size())
        {
            throw std::runtime_error("MessageCodec: truncated snappy varint");
        }
        uint8_t const b = _data[_pos++];
        value |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
        {
            return value;
        }
        shift += 7;
        if (shift >= 64)
        {
            throw std::runtime_error("MessageCodec: snappy varint overflow");
        }
    }
}

bcos::bytes rawSnappyBlockCompress(bytesConstRef _data)
{
    // Literal-only encoding: always valid, trivially correct for any input. devp2p
    // messages are small; skipping actual back-referencing costs nothing for sync.
    bcos::bytes out;
    size_t const n = _data.size();
    if (n == 0)
    {
        return out;  // empty block
    }
    uint64_t const lenMinus1 = n - 1;
    if (lenMinus1 < 60)
    {
        out.push_back(static_cast<bcos::byte>((lenMinus1 << 2)));  // literal tag
    }
    else
    {
        size_t lenBytes;
        if (lenMinus1 <= 0xFF)
        {
            lenBytes = 1;
        }
        else if (lenMinus1 <= 0xFFFF)
        {
            lenBytes = 2;
        }
        else if (lenMinus1 <= 0xFFFFFF)
        {
            lenBytes = 3;
        }
        else
        {
            lenBytes = 4;
        }
        out.push_back(static_cast<bcos::byte>(((59 + lenBytes) << 2)));  // 60..63
        for (size_t i = 0; i < lenBytes; ++i)
        {
            out.push_back(static_cast<bcos::byte>((lenMinus1 >> (8 * i)) & 0xFF));
        }
    }
    out.insert(out.end(), _data.begin(), _data.end());
    return out;
}

// Decodes one raw block starting at _pos; advances _pos to the end of the block.
bcos::bytes rawSnappyDecompressBlock(bytesConstRef _data, size_t& _pos, size_t _maxOutput)
{
    bcos::bytes out;
    size_t const n = _data.size();
    auto need = [&](size_t k) {
        if (_pos + k > n)
        {
            throw std::runtime_error("MessageCodec: truncated snappy data");
        }
    };
    while (_pos < n)
    {
        uint8_t const tag = _data[_pos++];
        uint8_t const type = tag & 0x3;       // LOW two bits select the element type
        uint8_t const dataBits = tag >> 2;    // high six bits carry len/offset info
        if (type == 0)  // literal
        {
            size_t len = dataBits + 1;
            if (dataBits >= 60)
            {
                size_t const lenBytes = dataBits - 59;  // 60->1, 61->2, 62->3, 63->4
                need(lenBytes);
                uint64_t value = 0;
                for (size_t i = 0; i < lenBytes; ++i)
                {
                    value |= static_cast<uint64_t>(_data[_pos + i]) << (8 * i);
                }
                _pos += lenBytes;
                len = static_cast<size_t>(value) + 1;
            }
            if (out.size() + len > _maxOutput || _pos + len > n)
            {
                throw std::runtime_error("MessageCodec: snappy literal exceeds limits");
            }
            out.insert(out.end(), _data.begin() + _pos, _data.begin() + _pos + len);
            _pos += len;
        }
        else
        {
            size_t len;
            size_t offset;
            if (type == 1)  // copy-1: 3-bit length + 5-bit offset
            {
                len = (dataBits & 0x7) + 4;
                need(1);
                offset = (static_cast<size_t>(dataBits >> 3) << 8) | _data[_pos++];
            }
            else if (type == 2)  // copy-2
            {
                len = dataBits + 1;
                need(2);
                offset = static_cast<size_t>(_data[_pos]) |
                         (static_cast<size_t>(_data[_pos + 1]) << 8);
                _pos += 2;
            }
            else  // copy-4
            {
                len = dataBits + 1;
                need(4);
                offset = static_cast<size_t>(_data[_pos]) |
                         (static_cast<size_t>(_data[_pos + 1]) << 8) |
                         (static_cast<size_t>(_data[_pos + 2]) << 16) |
                         (static_cast<size_t>(_data[_pos + 3]) << 24);
                _pos += 4;
            }
            if (offset == 0 || offset > out.size() || out.size() + len > _maxOutput)
            {
                throw std::runtime_error("MessageCodec: invalid snappy copy");
            }
            for (size_t i = 0; i < len; ++i)
            {
                out.push_back(out[out.size() - offset]);
            }
        }
    }
    return out;
}

bcos::bytes rawSnappyCompress(bytesConstRef _data)
{
    bcos::bytes block = rawSnappyBlockCompress(_data);
    bcos::bytes out;
    appendUvarint(out, _data.size());
    out.insert(out.end(), block.begin(), block.end());
    return out;
}

bcos::bytes rawSnappyDecompress(bytesConstRef _data, size_t _maxOutput)
{
    size_t pos = 0;
    uint64_t const declared = readUvarint(_data, pos);
    if (declared > _maxOutput)
    {
        throw std::runtime_error("MessageCodec: snappy declared length exceeds limits");
    }
    bcos::bytes out = rawSnappyDecompressBlock(_data, pos, _maxOutput);
    if (out.size() != declared)
    {
        throw std::runtime_error("MessageCodec: snappy declared length mismatch");
    }
    return out;
}
}  // namespace

bcos::bytes MessageCodec::encode(Message const& _message) const
{
    bcos::bytes frameData;
    frameData.reserve(_message.data.size() + 1);
    // Encode as a 64-bit value: RLP(uint8_t) hits a missing toCompactBigEndian(byte)
    // overload, and the resulting encoding is identical for ids < 0x80.
    bcos::codec::rlp::encode(frameData, static_cast<uint64_t>(_message.id));
    if (!m_compressionEnabled)
    {
        frameData.insert(frameData.end(), _message.data.begin(), _message.data.end());
    }
    else
    {
        auto compressed = rawSnappyCompress(
            bytesConstRef(_message.data.data(), _message.data.size()));
        frameData.insert(frameData.end(), compressed.begin(), compressed.end());
    }
    std::cerr << "[codec] send id=" << static_cast<int>(_message.id)
              << " compress=" << (m_compressionEnabled ? 1 : 0)
              << " frame=" << bcos::toHexStringWithPrefix(frameData).substr(0, 120)
              << std::endl;
    return frameData;
}

Message MessageCodec::decode(bytesConstRef _frameData) const
{
    if (_frameData.empty())
    {
        throw std::runtime_error("MessageCodec: frame data too short");
    }
    std::cerr << "[codec] recv full=" << bcos::toHexStringWithPrefix(_frameData).substr(0, 200)
              << " compress=" << (m_compressionEnabled ? 1 : 0) << std::endl;
    Message message;
    // The message id is RLP-encoded (RLP(0) == 0x80), so decode it properly.
    {
        bcos::bytesRef view(
            const_cast<bcos::byte*>(_frameData.data()), _frameData.size());
        if (auto err = bcos::codec::rlp::decode(view, message.id))
        {
            throw std::runtime_error("MessageCodec: failed to decode message id");
        }
        auto payload = _frameData.getCroppedData(_frameData.size() - view.size());
        if (!m_compressionEnabled)
        {
            message.data.assign(payload.begin(), payload.end());
        }
        else
        {
            message.data = rawSnappyDecompress(payload, kMaxFrameSize);
        }
        // Diagnostics: dump complete BlockHeaders payloads to a file so the
        // wire format can be analysed offline.
        if (message.id == 20 && message.data.size() > 10000)
        {
            FILE* f = std::fopen("/home/more/tmp/blkheaders.bin", "wb");
            if (f)
            {
                std::fwrite(message.data.data(), 1, message.data.size(), f);
                std::fclose(f);
                std::cerr << "[codec] WROTE blkheaders.bin size=" << message.data.size()
                          << std::endl;
            }
        }
        std::cerr << "[codec] recv id=" << static_cast<int>(message.id)
                  << " compress=" << (m_compressionEnabled ? 1 : 0)
                  << " raw=" << bcos::toHexStringWithPrefix(payload).substr(0, 96)
                  << " data=" << bcos::toHexStringWithPrefix(message.data).substr(0, 96)
                  << std::endl;
    }
    return message;
}

}  // namespace bcos::devp2p::rlpx
