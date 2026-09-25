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
 * @brief FrameMeta + the FrameDecoder concept: the seam that keeps libnetwork a generic
 *        framed-transport engine. The session layer never sees a concrete message type —
 *        inbound, a Decoder splits the byte stream into owned frames plus opaque metadata
 *        (sequence number, response flag, destination id) that the session delivers,
 *        uninterpreted, to the message handler; outbound, the caller hands over an
 *        already-encoded header and payload views.
 */
#pragma once

#include "bcos-utilities/Common.h"
#include <concepts>
#include <cstdint>
#include <string>

namespace bcos::gateway
{
/// One decode step's output: both the stream-splitting result and every piece of metadata the
/// session needs to dispatch the frame. The wire format is entirely the Decoder's business.
struct FrameMeta
{
    enum class Status : uint8_t
    {
        NeedMoreData,   ///< declaredLength holds the frame's total length (buffer-grow hint)
        Frame,          ///< consumed/frame/seq/isResp/dstID are valid
        ProtocolError,  ///< the stream is desynchronized; the session drops the connection
    };

    Status status = Status::NeedMoreData;
    uint32_t declaredLength = 0;  ///< Status::NeedMoreData: the frame length the header declares
    uint32_t consumed = 0;        ///< Status::Frame: bytes consumed from the read buffer
    uint32_t seq = 0;             ///< response-correlation key
    bool isResp = false;          ///< response flag, opaque to libnetwork: the message handler
                                  ///< decides whether it settles a pending request
                                  ///< (BasicSession::claimResponse)
    /// Destination node id, opaque to libnetwork (empty for protocols without multi-hop
    /// routing): the message handler uses it to tell local delivery from forwarding — a routed
    /// response must never claim a LOCAL pending callback on a seq collision.
    std::string dstID;
    /// The complete wire frame (header + payload), owned. Owned because dispatch is asynchronous
    /// (posted to another executor) while the read buffer is reused and resized.
    bytes frame;
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
}  // namespace bcos::gateway
