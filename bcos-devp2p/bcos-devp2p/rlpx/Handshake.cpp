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
 * @file Handshake.cpp
 * @brief RLPx v4 handshake implementation (port of silkworm auth messages).
 * @date 2026/8/18
 */
#include "Handshake.h"

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/random/CryptoRandom.h>
#include <stdexcept>

namespace bcos::devp2p::rlpx
{
namespace
{
// 2-byte big-endian size prefix (EIP-8 auth-size).
bcos::bytes serializeSizeImpl(size_t _size)
{
    bcos::bytes size(2, 0);
    size[0] = static_cast<bcos::byte>((_size >> 8) & 0xff);
    size[1] = static_cast<bcos::byte>(_size & 0xff);
    return size;
}

size_t deserializeSize(bytesConstRef _data)
{
    if (_data.size() < 2)
    {
        throw std::runtime_error("handshake: size prefix too short");
    }
    return (static_cast<size_t>(_data[0]) << 8) | static_cast<size_t>(_data[1]);
}
}  // namespace

// ---------------------------------------------------------------------------
// AuthMessage
// ---------------------------------------------------------------------------
AuthMessage::AuthMessage(EccKeyPair const& _initiatorKeyPair, bytesConstRef _recipientPublicKey,
    EccKeyPair const& _ephemeralKeyPair)
  : m_initiatorPublicKey(_initiatorKeyPair.publicKey()),
    m_recipientPublicKey(_recipientPublicKey.begin(), _recipientPublicKey.end()),
    m_ephemeralPublicKey(_ephemeralKeyPair.publicKey())
{
    auto const& initiatorPrivateKey = _initiatorKeyPair.privateKey();
    auto const& ephemeralPrivateKey = _ephemeralKeyPair.privateKey();
    bcos::bytes sharedSecret = EciesCipher::computeSharedSecret(_recipientPublicKey,
        bytesConstRef(initiatorPrivateKey.data(), initiatorPrivateKey.size()));
    m_nonce = bcos::crypto::cryptoRandomBytes(sharedSecret.size());
    xorBytes(sharedSecret, bytesConstRef(m_nonce.data(), m_nonce.size()));
    m_signature = signRecoverable(bytesConstRef(sharedSecret.data(), sharedSecret.size()),
        bytesConstRef(ephemeralPrivateKey.data(), ephemeralPrivateKey.size()));
}

AuthMessage::AuthMessage(bytesConstRef _data, bytesConstRef _recipientPrivateKey)
{
    auto plainText = decryptBody(_data, _recipientPrivateKey);
    initFromRlp(bytesConstRef(plainText.data(), plainText.size()));

    bcos::bytes sharedSecret = EciesCipher::computeSharedSecret(
        bytesConstRef(m_initiatorPublicKey.data(), m_initiatorPublicKey.size()),
        _recipientPrivateKey);
    if (sharedSecret.size() != m_nonce.size())
    {
        throw std::runtime_error("AuthMessage: invalid nonce size");
    }
    xorBytes(sharedSecret, bytesConstRef(m_nonce.data(), m_nonce.size()));
    m_ephemeralPublicKey = recoverPublicKey(
        bytesConstRef(sharedSecret.data(), sharedSecret.size()),
        bytesConstRef(m_signature.data(), m_signature.size()));
}

bcos::bytes AuthMessage::bodyAsRlp() const
{
    bcos::bytes data;
    bcos::codec::rlp::encode(data,
        bytesConstRef(m_signature.data(), m_signature.size()),
        bytesConstRef(m_initiatorPublicKey.data(), m_initiatorPublicKey.size()),
        bytesConstRef(m_nonce.data(), m_nonce.size()), static_cast<uint64_t>(kVersion));
    return data;
}

void AuthMessage::initFromRlp(bytesConstRef _data)
{
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    auto [listError, listHeader] = bcos::codec::rlp::decodeHeader(view);
    if (listError || !listHeader.isList)
    {
        throw std::runtime_error("AuthMessage: auth body is not an RLP list");
    }
    bcos::bytesRef items(view.data(), listHeader.payloadLength);
    auto decodeBytes = [&items](bcos::bytes& out) {
        if (auto err = bcos::codec::rlp::decode(items, out))
        {
            throw std::runtime_error("AuthMessage: item decode failed");
        }
    };
    decodeBytes(m_signature);
    decodeBytes(m_initiatorPublicKey);
    decodeBytes(m_nonce);
    uint64_t version = 0;
    if (auto err = bcos::codec::rlp::decode(items, version))
    {
        throw std::runtime_error("AuthMessage: version decode failed");
    }
    // Ignore trailing items (EIP-8 forward compatibility).
    if (m_signature.size() != 65 || m_initiatorPublicKey.size() != 64 || m_nonce.size() != 32)
    {
        throw std::runtime_error("AuthMessage: invalid item sizes");
    }
}

bcos::bytes AuthMessage::serializeSize(size_t _bodySize)
{
    return serializeSizeImpl(_bodySize);
}

bcos::bytes AuthMessage::decryptBody(bytesConstRef _data, bytesConstRef _recipientPrivateKey)
{
    auto size = serializeSizeImpl(_data.size());
    return EciesCipher::decrypt(
        _data, _recipientPrivateKey, bytesConstRef(size.data(), size.size()));
}

bcos::bytes AuthMessage::serialize() const
{
    bcos::bytes bodyRlp = bodyAsRlp();
    bodyRlp.resize(EciesCipher::roundUpToBlockSize(bodyRlp.size()));
    size_t bodySize = EciesCipher::estimateEncryptedSize(bodyRlp.size());

    auto size = serializeSizeImpl(bodySize);
    auto body = EciesCipher::encrypt(bytesConstRef(bodyRlp.data(), bodyRlp.size()),
        bytesConstRef(m_recipientPublicKey.data(), m_recipientPublicKey.size()),
        bytesConstRef(size.data(), size.size()));
    body.insert(body.begin(), size.begin(), size.end());
    return body;
}

// ---------------------------------------------------------------------------
// AuthAckMessage
// ---------------------------------------------------------------------------
AuthAckMessage::AuthAckMessage(EccKeyPair const& _ephemeralKeyPair,
    bytesConstRef _initiatorPublicKey, bytesConstRef _recipientNonce)
  : m_ephemeralPublicKey(_ephemeralKeyPair.publicKey()),
    m_initiatorPublicKey(_initiatorPublicKey.begin(), _initiatorPublicKey.end()),
    m_nonce(_recipientNonce.begin(), _recipientNonce.end())
{}

AuthAckMessage::AuthAckMessage(bytesConstRef _data, bytesConstRef _initiatorPrivateKey)
{
    auto plainText = decryptBody(_data, _initiatorPrivateKey);
    initFromRlp(bytesConstRef(plainText.data(), plainText.size()));
}

bcos::bytes AuthAckMessage::bodyAsRlp() const
{
    bcos::bytes data;
    bcos::codec::rlp::encode(data,
        bytesConstRef(m_ephemeralPublicKey.data(), m_ephemeralPublicKey.size()),
        bytesConstRef(m_nonce.data(), m_nonce.size()), static_cast<uint64_t>(kVersion));
    return data;
}

void AuthAckMessage::initFromRlp(bytesConstRef _data)
{
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    auto [listError, listHeader] = bcos::codec::rlp::decodeHeader(view);
    if (listError || !listHeader.isList)
    {
        throw std::runtime_error("AuthAckMessage: ack body is not an RLP list");
    }
    bcos::bytesRef items(view.data(), listHeader.payloadLength);
    auto decodeBytes = [&items](bcos::bytes& out) {
        if (auto err = bcos::codec::rlp::decode(items, out))
        {
            throw std::runtime_error("AuthAckMessage: item decode failed");
        }
    };
    decodeBytes(m_ephemeralPublicKey);
    decodeBytes(m_nonce);
    uint64_t version = 0;
    if (auto err = bcos::codec::rlp::decode(items, version))
    {
        throw std::runtime_error("AuthAckMessage: version decode failed");
    }
    if (m_ephemeralPublicKey.size() != 64 || m_nonce.size() != 32)
    {
        throw std::runtime_error("AuthAckMessage: invalid item sizes");
    }
}

bcos::bytes AuthAckMessage::serializeSize(size_t _bodySize)
{
    return serializeSizeImpl(_bodySize);
}

bcos::bytes AuthAckMessage::decryptBody(bytesConstRef _data, bytesConstRef _initiatorPrivateKey)
{
    auto size = serializeSizeImpl(_data.size());
    return EciesCipher::decrypt(
        _data, _initiatorPrivateKey, bytesConstRef(size.data(), size.size()));
}

bcos::bytes AuthAckMessage::serialize() const
{
    bcos::bytes bodyRlp = bodyAsRlp();
    bodyRlp.resize(EciesCipher::roundUpToBlockSize(bodyRlp.size()));
    size_t bodySize = EciesCipher::estimateEncryptedSize(bodyRlp.size());

    auto size = serializeSizeImpl(bodySize);
    auto body = EciesCipher::encrypt(bytesConstRef(bodyRlp.data(), bodyRlp.size()),
        bytesConstRef(m_initiatorPublicKey.data(), m_initiatorPublicKey.size()),
        bytesConstRef(size.data(), size.size()));
    body.insert(body.begin(), size.begin(), size.end());
    return body;
}

// ---------------------------------------------------------------------------
// Handshake orchestration
// ---------------------------------------------------------------------------
AuthKeys Handshake::execute(Socket& _socket)
{
    if (m_isInitiator)
    {
        return authInitiator(_socket);
    }
    return authRecipient(_socket);
}

AuthKeys Handshake::authInitiator(Socket& _socket)
{
    if (m_recipientPublicKey.size() != 64)
    {
        throw std::invalid_argument("Handshake: initiator requires the recipient public key");
    }
    EccKeyPair ephemeralKeyPair;

    AuthMessage authMessage(
        m_keyPair, bytesConstRef(m_recipientPublicKey.data(), m_recipientPublicKey.size()),
        ephemeralKeyPair);
    auto authData = authMessage.serialize();
    _socket.sendAll(bytesConstRef(authData.data(), authData.size()));

    // Read the ack: 2-byte size || encrypted body.
    auto sizeData = _socket.recvFixed(2);
    size_t size = deserializeSize(bytesConstRef(sizeData.data(), sizeData.size()));
    if (size > 2048)
    {
        throw std::runtime_error("Handshake: ack message too big");
    }
    auto ackData = _socket.recvFixed(size);

    AuthAckMessage ackMessage(
        bytesConstRef(ackData.data(), ackData.size()), ref(m_keyPair.privateKey()));

    AuthKeys keys;
    keys.peerEphemeralPublicKey = ackMessage.ephemeralPublicKey();
    keys.ephemeralPrivateKey = ephemeralKeyPair.privateKey();
    keys.initiatorNonce = authMessage.nonce().toBytes();
    keys.recipientNonce = ackMessage.nonce().toBytes();
    keys.initiatorFirstMessageData = authData;
    // The MAC seed uses the FULL wire message, including the 2-byte size prefix.
    keys.recipientFirstMessageData = sizeData;
    keys.recipientFirstMessageData.insert(
        keys.recipientFirstMessageData.end(), ackData.begin(), ackData.end());
    return keys;
}

AuthKeys Handshake::authRecipient(Socket& _socket)
{
    // Read the auth: 2-byte size || encrypted body.
    auto sizeData = _socket.recvFixed(2);
    size_t size = deserializeSize(bytesConstRef(sizeData.data(), sizeData.size()));
    if (size > 2048)
    {
        throw std::runtime_error("Handshake: auth message too big");
    }
    auto authData = _socket.recvFixed(size);

    AuthMessage authMessage(
        bytesConstRef(authData.data(), authData.size()), ref(m_keyPair.privateKey()));

    EccKeyPair ephemeralKeyPair;
    // The ack carries a fresh recipient nonce (independent of the initiator's).
    auto recipientNonce = bcos::crypto::cryptoRandomBytes(32);
    AuthAckMessage ackMessage(ephemeralKeyPair, ref(authMessage.initiatorPublicKey()),
        bytesConstRef(recipientNonce.data(), recipientNonce.size()));
    auto ackData = ackMessage.serialize();
    _socket.sendAll(bytesConstRef(ackData.data(), ackData.size()));

    AuthKeys keys;
    keys.peerEphemeralPublicKey = authMessage.ephemeralPublicKey();
    keys.ephemeralPrivateKey = ephemeralKeyPair.privateKey();
    keys.initiatorNonce = authMessage.nonce().toBytes();
    keys.recipientNonce = recipientNonce;
    // The MAC seed uses the FULL wire message, including the 2-byte size prefix.
    keys.initiatorFirstMessageData = sizeData;
    keys.initiatorFirstMessageData.insert(
        keys.initiatorFirstMessageData.end(), authData.begin(), authData.end());
    keys.recipientFirstMessageData = ackData;
    return keys;
}

}  // namespace bcos::devp2p::rlpx
