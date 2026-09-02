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
 * @file Framing.h
 * @brief RLPx frame encryption (AES-256-CTR + Keccak-256 MAC), ported from
 *        silkworm sentry/rlpx/framing/framing_cipher; wire-compatible with
 *        geth p2p/rlpx framing.
 * @date 2026/8/18
 */
#pragma once

#include <bcos-utilities/Common.h>

namespace bcos::devp2p::rlpx
{
// Encrypts/decrypts RLPx frames. Each direction owns a continuous AES-256-CTR
// stream (zero IV) and a Keccak-256 MAC hasher seeded from the handshake.
class FramingCipher
{
public:
    struct KeyMaterial
    {
        bcos::bytes ephemeralSharedSecret;      // 32B raw ECDH x-coordinate
        bool isInitiator;                       // true on the dialing side
        bcos::bytes initiatorNonce;             // 32B
        bcos::bytes recipientNonce;             // 32B
        bcos::bytes initiatorFirstMessageData;  // full auth wire bytes
        bcos::bytes recipientFirstMessageData;  // full ack wire bytes
    };

    explicit FramingCipher(KeyMaterial const& _keyMaterial);
    ~FramingCipher();
    FramingCipher(FramingCipher&&) noexcept;
    FramingCipher& operator=(FramingCipher&&) noexcept;

    // Derive aes-secret and mac-secret from the handshake key material.
    // Exposed for tests: EciesTest covers the ECIES scheme against independent
    // vectors (incl. geth's TestHandshakeForwardCompatibility inputs); the
    // deriveSecrets EIP-8 vector check ships with the handshake-layer tests.
    static void deriveSecrets(
        KeyMaterial const& _keyMaterial, bcos::bytes& _aesSecret, bcos::bytes& _macSecret);

    // Encrypt one frame (32B header + padded payload + MACs).
    bcos::bytes encryptFrame(bcos::bytes _frameData);
    // Decrypt a received 32B header; returns the unpadded frame payload size.
    size_t decryptHeader(bytesConstRef _data);
    // Wire length of a frame for a payload of `_headerFrameSize` bytes:
    // padded-to-16 payload + 16B frame MAC.
    static size_t frameSize(size_t _headerFrameSize);
    // Header wire length: 16B ciphertext + 16B MAC.
    static size_t headerSize();
    // Decrypt the frame body (padded payload + MAC) given the payload size.
    bcos::bytes decryptFrame(bytesConstRef _data, size_t _headerFrameSize);

private:
    class Impl;
    Impl* m_impl;
};
}  // namespace bcos::devp2p::rlpx
