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
 *
 * @file FrameMeta.h
 * @brief FrameMeta + the FrameDecoder concept: the seam that keeps bcos-network a generic
 *        framed-transport engine. The session layer never sees a concrete message type —
 *        inbound, a Decoder splits the byte stream into owned frames; outbound, the caller
 *        hands over an already-encoded header and payload views.
 */
#pragma once

#include "bcos-utilities/Common.h"
#include <concepts>
#include <cstdint>

namespace bcos::network
{
/// Frames at least this large are handed over through the take-buffer path (see
/// FrameMeta::takeBuffer): the read loop swaps the whole receive buffer into FrameMeta::frame
/// instead of copying the frame out. Below this, a memcpy is cheaper than a buffer swap plus
/// a fresh allocation.
constexpr uint32_t FRAME_TAKE_BUFFER_THRESHOLD = 256 * 1024;

/// One decode step's output: the stream-splitting result. The wire format is entirely the
/// Decoder's business.
///
/// TWO BYTE COUNTS, deliberately distinct — conflating them was a latent out-of-bounds read:
///  - `wireConsumed` is what the READ LOOP advances its buffer by: the bytes physically taken off
///    the socket. For a plaintext format it equals the frame length; for a format with a
///    fixed-size header/framing layer (RLPx: 32B header + padded body + 16B MAC) it is LARGER than
///    the frame's plaintext.
///  - `frame` holds the PLAINTEXT the consumer sees, and is NEVER indexed by `wireConsumed`.
///    Traversal is through frameData() (the copy path) or frameOffset() + frameSize() (the
///    take-buffer path), so a decoder may legitimately report consumed > frame.size() — the RLPx
///    decoder does exactly that (see RlpxFrameDecoder: encryption overhead). The previous shape
///    had frameData() compute {frame.data() + frameOffset, consumed}, which read up to
///    (consumed - frame.size()) bytes past the end for such a decoder.
struct FrameMeta
{
    enum class Status : uint8_t
    {
        NeedMoreData,   ///< frameSize holds the frame's total length (buffer-grow hint)
        Frame,          ///< wireConsumed/frame/frameSize are valid
        ProtocolError,  ///< the stream is desynchronized; the session drops the connection
        /// Delivered by the read loop when the configured read timeout (setReadTimeoutMs, 0 =
        /// disabled by default) expires before any bytes arrive. Unlike the other statuses this
        /// one never comes out of the Decoder: `frame` is empty and the counts are 0. The session
        /// stays alive — the consumer decides what a stalled read means.
        Timeout,
    };

    Status status = Status::NeedMoreData;
    /// Status::NeedMoreData: the frame length the header declares (pure buffer-grow hint).
    uint32_t frameSize = 0;
    /// Status::Frame: bytes to advance the receive buffer by (wire bytes, see the type comment).
    uint32_t wireConsumed = 0;
    /// Offset of the frame's first plaintext byte within `frame`: 0 on the copy path, the read
    /// buffer's readPos on the take-buffer path (where the dead prefix of already-consumed bytes
    /// rides along inside the moved vector and is freed with it).
    uint32_t frameOffset = 0;
    /// Decoder-to-read-loop hint, never meaningful to the message handler: the frame is at
    /// least FRAME_TAKE_BUFFER_THRESHOLD bytes, so the decoder left `frame` empty and the read
    /// loop moves the whole receive buffer in (filling frameOffset and frameSize) instead of
    /// copying. The read loop clears the flag before dispatch.
    bool takeBuffer = false;
    /// The frame's PLAINTEXT storage, owned. Owned because dispatch is asynchronous (posted to
    /// another executor) while the read buffer is reused and resized.
    ///  - copy path: exactly the frame's plaintext, and frameOffset is 0.
    ///  - take-buffer path: the session's whole former receive buffer, with the plaintext at
    ///    [frameOffset, frameOffset + frameSize).
    /// Always traverse it through frameData().
    bytes frame;

    /// The decoded frame's plaintext as a view into `frame` — valid for BOTH paths and the single
    /// access contract for consumers. Never wider than `frame`, regardless of wireConsumed.
    bytesConstRef frameData() const
    {
        return {frame.data() + frameOffset, frameSize};
    }
};

/// A stream decoder: splits a TCP byte stream into frames. Stateless formats use an empty
/// decoder (BasicSession holds it with [[no_unique_address]]); stateful formats (continuation
/// frames, per-connection compression or encryption contexts) keep their state in the instance.
///
/// tryDecode MUST be bounds-safe on arbitrary attacker-controlled input: it runs before any
/// other validation on the receive path. It must not throw — return ProtocolError instead.
///
/// On Status::Frame a decoder MUST set: wireConsumed, frameSize, and the plaintext either in
/// `frame` with frameOffset == 0 (copy path) or with takeBuffer = true. It MUST NOT use
/// wireConsumed to index `frame` — see the two-byte-counts note above.
template <typename D>
concept FrameDecoder = std::default_initializable<D> && requires(D& decoder, const bytesConstRef& buffer) {
    { decoder.tryDecode(buffer) } -> std::same_as<FrameMeta>;
};
}  // namespace bcos::network
