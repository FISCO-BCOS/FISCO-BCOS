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
 * @file Handshake.h
 * @brief RLPx v4 encrypted handshake (EIP-8): Auth/Ack messages, ECIES
 *        encapsulation, shared-secret derivation. Ported from silkworm
 *        sentry/rlpx/auth; wire-compatible with geth p2p/rlpx.
 * @date 2026/8/18
 */
#pragma once

#include "Crypto.h"
#include "Socket.h"

namespace bcos::devp2p::rlpx
{
// RLPx v4 auth message: ECIES(recipientPub, rlp([sig, initiatorPub, nonce, 4])).
class AuthMessage
{
public:
    // Build as the initiator.
    AuthMessage(EccKeyPair const& _initiatorKeyPair, bytesConstRef _recipientPublicKey,
        EccKeyPair const& _ephemeralKeyPair);
    // Parse + verify as the recipient.
    AuthMessage(bytesConstRef _data, bytesConstRef _recipientPrivateKey);

    // Wire bytes: 2-byte size || ECIES body.
    bcos::bytes serialize() const;

    PublicKey const& initiatorPublicKey() const { return m_initiatorPublicKey; }
    PublicKey const& ephemeralPublicKey() const { return m_ephemeralPublicKey; }
    bytesConstRef nonce() const { return bytesConstRef(m_nonce.data(), m_nonce.size()); }

private:
    bcos::bytes bodyAsRlp() const;
    void initFromRlp(bytesConstRef _data);

    PublicKey m_initiatorPublicKey;
    bcos::bytes m_recipientPublicKey;
    PublicKey m_ephemeralPublicKey;
    bcos::bytes m_nonce;
    bcos::bytes m_signature;
    static constexpr uint8_t kVersion{4};
};

// RLPx v4 ack message: ECIES(initiatorPub, rlp([ephemeralPub, nonce, 4])).
class AuthAckMessage
{
public:
    AuthAckMessage(EccKeyPair const& _ephemeralKeyPair, bytesConstRef _initiatorPublicKey,
        bytesConstRef _recipientNonce);
    // Parse as the initiator (we already know the recipient static key).
    explicit AuthAckMessage(bytesConstRef _data, bytesConstRef _initiatorPrivateKey);

    bcos::bytes serialize() const;

    PublicKey const& ephemeralPublicKey() const { return m_ephemeralPublicKey; }
    bytesConstRef nonce() const { return bytesConstRef(m_nonce.data(), m_nonce.size()); }

private:
    bcos::bytes bodyAsRlp() const;
    void initFromRlp(bytesConstRef _data);

    PublicKey m_ephemeralPublicKey;
    bcos::bytes m_initiatorPublicKey;
    bcos::bytes m_nonce;
    static constexpr uint8_t kVersion{4};
};

// The keys derived from a completed handshake, plus the wire bytes of the
// auth/ack messages (needed to seed the framing MACs).
struct AuthKeys
{
    PublicKey peerEphemeralPublicKey;
    PrivateKey ephemeralPrivateKey;
    bcos::bytes initiatorNonce;
    bcos::bytes recipientNonce;
    bcos::bytes initiatorFirstMessageData;
    bcos::bytes recipientFirstMessageData;
};

// Runs the encrypted handshake over a socket.
//   initiator side: send auth, receive ack (needs the recipient static pubkey)
//   recipient side: receive auth, send ack
class Handshake
{
public:
    Handshake(EccKeyPair const& _keyPair, bool _isInitiator, bytesConstRef _recipientPublicKey = {})
      : m_keyPair(_keyPair),
        m_isInitiator(_isInitiator),
        m_recipientPublicKey(_recipientPublicKey.begin(), _recipientPublicKey.end())
    {}

    AuthKeys execute(Socket& _socket);

private:
    AuthKeys authInitiator(Socket& _socket);
    AuthKeys authRecipient(Socket& _socket);

    EccKeyPair m_keyPair;
    bool m_isInitiator;
    bcos::bytes m_recipientPublicKey;  // set for the initiator side
};
}  // namespace bcos::devp2p::rlpx
