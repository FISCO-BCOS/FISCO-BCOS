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
 * @file Crypto.h
 * @brief RLPx cryptographic primitives: ECIES, incremental Keccak-256 (MAC),
 *        recoverable signatures and ECDH. Ported from silkworm's sentry
 *        (erigontech/silkworm, Apache-2.0); wire-compatible with geth's
 *        crypto/ecies + p2p/rlpx.
 * @date 2026/8/18
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>

namespace bcos::devp2p::rlpx
{
// 64-byte uncompressed secp256k1 public key (no 0x04 prefix) — RLPx node-key format.
using PublicKey = bcos::bytes;
using PrivateKey = bcos::bytes;

// Incremental Keccak-256 (the RLPx MAC hash). hash() returns a snapshot of the
// current digest WITHOUT resetting the stream state.
class Sha3Hasher
{
public:
    Sha3Hasher();
    ~Sha3Hasher();
    Sha3Hasher(Sha3Hasher const&) = delete;
    Sha3Hasher& operator=(Sha3Hasher const&) = delete;

    void update(bytesConstRef _data);
    // Snapshot the current digest (state preserved for further update()).
    bcos::bytes hash();

private:
    struct Impl;
    Impl* m_impl;
};

bcos::bytes keccak256(bytesConstRef _data1);
bcos::bytes keccak256(bytesConstRef _data1, bytesConstRef _data2);

// _a[i] ^= _b[i] over the shorter input.
void xorBytes(bcos::bytes& _a, bytesConstRef _b);

// ECIES encryption/decryption (SEC1-style, compatible with geth crypto/ecies):
//   wire message = ephemeralPubkey(65B, 0x04-prefixed) || iv(16B) || ciphertext || mac(32B)
//   KDF:   sha256(0x00000001 || ecdhX); aesKey = K[0:16], macKey = sha256(K[16:32])
//   MAC:   HMAC-SHA256(macKey, iv || ciphertext || macExtraData)
//   cipher: AES-128-CTR
class EciesCipher
{
public:
    struct Message
    {
        PublicKey ephemeralPublicKey;
        bcos::bytes iv;
        bcos::bytes cipherText;
        bcos::bytes mac;
    };

    static Message encryptMessage(
        bytesConstRef _plainText, bytesConstRef _publicKey, bytesConstRef _macExtraData);
    static bcos::bytes decryptMessage(
        Message const& _message, bytesConstRef _privateKey, bytesConstRef _macExtraData);

    static bcos::bytes encrypt(
        bytesConstRef _plainText, bytesConstRef _publicKey, bytesConstRef _macExtraData);
    static bcos::bytes decrypt(
        bytesConstRef _messageData, bytesConstRef _privateKey, bytesConstRef _macExtraData);

    static bcos::bytes computeSharedSecret(bytesConstRef _publicKey, bytesConstRef _privateKey);
    static size_t roundUpToBlockSize(size_t _size);
    static size_t estimateEncryptedSize(size_t _size);

private:
    static bcos::bytes serializeMessage(Message const& _message);
    static Message deserializeMessage(bytesConstRef _messageData);
};

// Random secp256k1 keypair with a 64-byte uncompressed public key.
class EccKeyPair
{
public:
    EccKeyPair();
    explicit EccKeyPair(bcos::bytes _privateKey);

    PrivateKey const& privateKey() const { return m_privateKey; }
    PublicKey const& publicKey() const { return m_publicKey; }

private:
    bcos::bytes m_privateKey;
    bcos::bytes m_publicKey;
};

// 65-byte recoverable signature (r || s || recid) over the 32-byte `_hash`.
bcos::bytes signRecoverable(bytesConstRef _hash, bytesConstRef _privateKey);
// Recover the 64-byte public key from a 65-byte recoverable signature.
PublicKey recoverPublicKey(bytesConstRef _hash, bytesConstRef _signature);

}  // namespace bcos::devp2p::rlpx
