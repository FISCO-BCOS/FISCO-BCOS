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
 *        hands over an already-encoded header and payload views. Protocol-level header fields
 *        (seq, response flag, routing destination, ...) are the protocol layer's business:
 *        it re-reads them from the frame bytes at its own boundary.
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
struct FrameMeta
{
    enum class Status : uint8_t
    {
        NeedMoreData,   ///< declaredLength holds the frame's total length (buffer-grow hint)
        Frame,          ///< consumed/frame are valid
        ProtocolError,  ///< the stream is desynchronized; the session drops the connection
        /// Delivered by the read loop when the configured read timeout (setReadTimeoutMs, 0 =
        /// disabled by default) expires before any bytes arrive. Unlike the other statuses this
        /// one never comes out of the Decoder: `frame` is empty and `consumed`/`declaredLength`
        /// are 0. The session stays alive — the consumer decides what a stalled read means.
        Timeout,
    };

    Status status = Status::NeedMoreData;
    uint32_t declaredLength = 0;  ///< Status::NeedMoreData: the frame length the header declares
    uint32_t consumed = 0;        ///< Status::Frame: bytes consumed from the read buffer
    /// Offset of the frame's first byte within `frame`. 0 on the copy path; the read buffer's
    /// readPos on the take-buffer path, where the dead prefix of already-consumed bytes rides
    /// along inside the moved vector and is freed with it.
    uint32_t frameOffset = 0;
    /// Decoder-to-read-loop hint, never meaningful to the message handler: the frame is at
    /// least FRAME_TAKE_BUFFER_THRESHOLD bytes, so the decoder left `frame` empty and the read
    /// loop moves the whole receive buffer in (filling frameOffset) instead of copying. The
    /// read loop clears the flag before dispatch.
    bool takeBuffer = false;
    /// The wire frame storage, owned. Owned because dispatch is asynchronous (posted to another
    /// executor) while the read buffer is reused and resized. Copy path: exactly the frame
    /// (header + payload). Take-buffer path: the session's whole former receive buffer with
    /// the frame at [frameOffset, frameOffset + consumed). Always read through frameData().
    bytes frame;

    /// The decoded wire frame as a view into `frame` — valid for both the copy and the
    /// take-buffer path; the single access contract for consumers.
    bytesConstRef frameData() const
    {
        if (frame.empty())
        {
            return {};
        }
        return {frame.data() + frameOffset, consumed};
    }
};

/// A stream decoder: splits a TCP byte stream into frames. Stateless formats use an empty
/// decoder (BasicSession holds it with [[no_unique_address]]); stateful formats (continuation
/// frames, per-connection compression contexts) keep their state in the instance.
///
/// tryDecode MUST be bounds-safe on arbitrary attacker-controlled input: it runs before any
/// other validation on the receive path. It must not throw — return ProtocolError instead.
template <typename D>
concept FrameDecoder = std::default_initializable<D> && requires(D& decoder, const bytesConstRef& buffer) {
    { decoder.tryDecode(buffer) } -> std::same_as<FrameMeta>;
};
}  // namespace bcos::network
