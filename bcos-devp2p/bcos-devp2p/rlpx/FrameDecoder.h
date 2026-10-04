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
 * @file FrameDecoder.h
 * @brief bcos-network FrameDecoder for the RLPx wire format: splits the
 *        inbound encrypted byte stream into frames, decrypting them with the
 *        per-session FramingCipher injected after the RLPx handshake.
 * @date 2026/10/4
 */
#pragma once

#include "Framing.h"
#include "MessageCodec.h"
#include <bcos-network/FrameMeta.h>
#include <optional>

namespace bcos::devp2p::rlpx
{
// Stateful stream decoder satisfying bcos-network's FrameDecoder concept
// (default_initializable + noexcept tryDecode over the accumulated read
// buffer). The ingress cipher is injected with initCipher() once the RLPx
// handshake completes — before session start, so the read loop never decodes
// with an unset cipher.
//
// FrameMeta contract deviation forced by encryption: `consumed` counts WIRE
// bytes (ciphertext) so the read loop can advance its buffer, while `frame`
// holds the DECRYPTED payload, which is shorter. Consumers must therefore
// read the payload from meta.frame directly — FrameMeta::frameData() assumes
// frame.size() == consumed and does not apply here.
class RlpxFrameDecoder
{
public:
    void initCipher(FramingCipher::KeyMaterial const& _keyMaterial)
    {
        m_cipher.emplace(_keyMaterial);
        m_headerParsed = false;
        m_framePayloadSize = 0;
    }

    bcos::network::FrameMeta tryDecode(const bytesConstRef& _buffer) noexcept
    {
        bcos::network::FrameMeta meta;
        if (!m_cipher)
        {
            // Defensive: the session is always started after initCipher().
            meta.status = bcos::network::FrameMeta::Status::ProtocolError;
            return meta;
        }
        try
        {
            // Header stage: decrypt the 32B header exactly once. decryptHeader
            // advances the ingress CTR/MAC stream state, so re-parsing the same
            // header bytes would desynchronize the stream — m_headerParsed
            // makes the re-entrant calls idempotent.
            if (!m_headerParsed)
            {
                if (_buffer.size() < FramingCipher::headerSize())
                {
                    meta.status = bcos::network::FrameMeta::Status::NeedMoreData;
                    meta.declaredLength = FramingCipher::headerSize();
                    return meta;
                }
                m_framePayloadSize = static_cast<uint32_t>(m_cipher->decryptHeader(
                    bytesConstRef(_buffer.data(), FramingCipher::headerSize())));
                if (m_framePayloadSize > MessageCodec::kMaxFrameSize)
                {
                    meta.status = bcos::network::FrameMeta::Status::ProtocolError;
                    return meta;
                }
                m_headerParsed = true;
            }

            size_t const total =
                FramingCipher::headerSize() + FramingCipher::frameSize(m_framePayloadSize);
            if (_buffer.size() < total)
            {
                meta.status = bcos::network::FrameMeta::Status::NeedMoreData;
                meta.declaredLength = static_cast<uint32_t>(total);
                return meta;
            }
            meta.frame = m_cipher->decryptFrame(
                bytesConstRef(
                    _buffer.data() + FramingCipher::headerSize(), total - FramingCipher::headerSize()),
                m_framePayloadSize);
            m_headerParsed = false;
            meta.status = bcos::network::FrameMeta::Status::Frame;
            meta.consumed = static_cast<uint32_t>(total);
            return meta;
        }
        catch (...)
        {
            // MAC/decrypt failures desynchronize the stream: report a protocol
            // error and let the session drop the connection (concept contract:
            // never throw).
            meta.status = bcos::network::FrameMeta::Status::ProtocolError;
            return meta;
        }
    }

private:
    std::optional<FramingCipher> m_cipher;  // FramingCipher has no default ctor
    bool m_headerParsed{false};
    uint32_t m_framePayloadSize{0};
};
}  // namespace bcos::devp2p::rlpx
