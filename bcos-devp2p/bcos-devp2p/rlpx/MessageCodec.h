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
 * @file MessageCodec.h
 * @brief RLPx message <-> frame payload codec (message id RLP + optional snappy),
 *        ported from silkworm sentry/rlpx/framing/message_frame_codec.
 * @date 2026/8/18
 */
#pragma once

#include <bcos-codec/rlp/Result.h>
#include <bcos-utilities/Common.h>

namespace bcos::devp2p::rlpx
{
// A decoded RLPx message.
struct Message
{
    uint8_t id{0};     // message code
    bcos::bytes data;  // RLP-encoded payload
};

// Encodes/decodes a Message into/from a frame payload:
//   frame-data = RLP(id) || (snappy(data) | data)
class MessageCodec
{
public:
    static constexpr size_t kMaxFrameSize = 16 << 20;  // 16 MiB

    bcos::bytes encode(Message const& _message) const;
    // Malformed frames are reported as an RlpError value, never thrown.
    bcos::codec::rlp::RlpResult<Message> decode(bytesConstRef _frameData) const;

    void enableCompression() { m_compressionEnabled = true; }
    // Largest DECOMPRESSED payload decode accepts (default kMaxFrameSize). Raw snappy
    // inflates up to ~21x (a 3-byte copy-2 element yields 64 bytes), so a frame-size
    // limit alone does not bound the buffer a peer can make us allocate.
    void setMaxDecompressedSize(size_t _size) { m_maxDecompressedSize = _size; }

private:
    bool m_compressionEnabled{false};
    size_t m_maxDecompressedSize{kMaxFrameSize};
};
}  // namespace bcos::devp2p::rlpx
