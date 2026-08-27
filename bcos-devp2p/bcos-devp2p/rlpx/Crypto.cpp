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
 * @file Crypto.cpp
 * @brief RLPx crypto primitives implementation (port of silkworm sentry
 *        rlpx/crypto + auth/ecies_cipher, wire-compatible with geth).
 * @date 2026/8/18
 */
#include "Crypto.h"

#include <bcos-crypto/encrypt/AesCtrCipher.h>
#include <bcos-crypto/encrypt/HmacSha256.h>
#include <bcos-crypto/hash/Sha256.h>
#include <bcos-crypto/random/CryptoRandom.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Ecdh.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <cstring>
#include <stdexcept>

namespace bcos::devp2p::rlpx
{
using bcos::crypto::kAesBlockSize;

// ---------------------------------------------------------------------------
// Sha3Hasher — incremental Keccak-256 via OpenSSL EVP (pad-byte hack).
// ---------------------------------------------------------------------------
namespace
{
struct Keccak256CtxLayout
{
    uint64_t A[5][5];
    size_t block_size;
    size_t md_size;
    size_t num;
    unsigned char buf[1600 / 8 - 32];
    unsigned char pad;
};

struct EvpMdCtxKeccak256
{
    const EVP_MD* digest;
    void* engine;
    unsigned long flags;
    Keccak256CtxLayout* md_data;
};

void initKeccak256(EVP_MD_CTX* _ctx)
{
    if (EVP_DigestInit_ex(_ctx, EVP_sha3_256(), nullptr) != 1)
    {
        throw std::runtime_error("Sha3Hasher: EVP_DigestInit_ex failed");
    }
    // Swap SHA3-256's 0x06 padding for Keccak's 0x01 (same trick as bcos-crypto).
    auto keccak256 = reinterpret_cast<EvpMdCtxKeccak256*>(_ctx);
    if (keccak256->md_data == nullptr || keccak256->md_data->pad != 0x06)
    {
        throw std::runtime_error("Sha3Hasher: unexpected OpenSSL keccak layout");
    }
    keccak256->md_data->pad = 0x01;
}
}  // namespace

struct Sha3Hasher::Impl
{
    EVP_MD_CTX* ctx{EVP_MD_CTX_new()};
    Impl() { initKeccak256(ctx); }
    ~Impl() { EVP_MD_CTX_free(ctx); }
};

Sha3Hasher::Sha3Hasher() : m_impl(new Impl()) {}
Sha3Hasher::~Sha3Hasher()
{
    delete m_impl;
}

void Sha3Hasher::update(bytesConstRef _data)
{
    if (EVP_DigestUpdate(m_impl->ctx, _data.data(), _data.size()) != 1)
    {
        throw std::runtime_error("Sha3Hasher: EVP_DigestUpdate failed");
    }
}

bcos::bytes Sha3Hasher::hash()
{
    // Snapshot the digest state via context copy; the original keeps streaming.
    EVP_MD_CTX* copyCtx = EVP_MD_CTX_new();
    if (EVP_MD_CTX_copy_ex(copyCtx, m_impl->ctx) != 1)
    {
        EVP_MD_CTX_free(copyCtx);
        throw std::runtime_error("Sha3Hasher: EVP_MD_CTX_copy_ex failed");
    }
    bcos::bytes out(32, 0);
    unsigned int outLen = 0;
    if (EVP_DigestFinal_ex(copyCtx, out.data(), &outLen) != 1)
    {
        EVP_MD_CTX_free(copyCtx);
        throw std::runtime_error("Sha3Hasher: EVP_DigestFinal_ex failed");
    }
    EVP_MD_CTX_free(copyCtx);
    out.resize(outLen);
    return out;
}

bcos::bytes keccak256(bytesConstRef _data1)
{
    Sha3Hasher hasher;
    hasher.update(_data1);
    return hasher.hash();
}

bcos::bytes keccak256(bytesConstRef _data1, bytesConstRef _data2)
{
    Sha3Hasher hasher;
    hasher.update(_data1);
    hasher.update(_data2);
    return hasher.hash();
}

void xorBytes(bcos::bytes& _a, bytesConstRef _b)
{
    for (size_t i = 0; i < _a.size() && i < _b.size(); ++i)
    {
        _a[i] ^= _b[i];
    }
}

// ---------------------------------------------------------------------------
// ECIES
// ---------------------------------------------------------------------------
namespace
{
constexpr size_t kEciesKeySize = 16;     // AES-128
constexpr size_t kEciesMacSize = 32;     // HMAC-SHA256
constexpr size_t kEciesPubKeySize = 65;  // 0x04-prefixed uncompressed point

// NIST SP 800-56 Concatenation KDF (one SHA-256 iteration yields 32 bytes).
bcos::bytes eciesKdf(bytesConstRef _secret)
{
    bcos::bytes data(sizeof(uint32_t), 0);
    // counter = 1 (big-endian)
    data[3] = 1;
    data.insert(data.end(), _secret.begin(), _secret.end());
    return bcos::crypto::sha256Hash(bytesConstRef(data.data(), data.size())).asBytes();
}
}  // namespace

bcos::bytes EciesCipher::computeSharedSecret(bytesConstRef _publicKey, bytesConstRef _privateKey)
{
    return bcos::crypto::secp256k1EcdhCopyX(_publicKey, _privateKey);
}

EciesCipher::Message EciesCipher::encryptMessage(
    bytesConstRef _plainText, bytesConstRef _publicKey, bytesConstRef _macExtraData)
{
    EccKeyPair ephemeralKeyPair;

    auto const& ephemeralPrivateKey = ephemeralKeyPair.privateKey();
    bcos::bytes ecdheSecret = computeSharedSecret(
        _publicKey, bytesConstRef(ephemeralPrivateKey.data(), ephemeralPrivateKey.size()));
    bcos::bytes sharedSecret = eciesKdf(bytesConstRef(ecdheSecret.data(), ecdheSecret.size()));
    bytesConstRef aesKey(sharedSecret.data(), kEciesKeySize);
    bytesConstRef macKey(sharedSecret.data() + kEciesKeySize, kEciesKeySize);

    bcos::bytes iv = bcos::crypto::cryptoRandomBytes(kAesBlockSize);

    bcos::bytes cipherText =
        bcos::crypto::aesCtrCrypt(_plainText, aesKey, bytesConstRef(iv.data(), iv.size()));

    // MAC key = sha256(K[16:32]) — matches geth's deriveKeys (Km = sha256(Km_raw)).
    auto macKeyHash = bcos::crypto::sha256Hash(macKey).asBytes();
    bcos::bytes mac = bcos::crypto::hmacSha256(bytesConstRef(macKeyHash.data(), macKeyHash.size()),
        bytesConstRef(iv.data(), iv.size()), bytesConstRef(cipherText.data(), cipherText.size()),
        _macExtraData);

    return {ephemeralKeyPair.publicKey(), std::move(iv), std::move(cipherText), std::move(mac)};
}

bcos::bytes EciesCipher::decryptMessage(
    Message const& _message, bytesConstRef _privateKey, bytesConstRef _macExtraData)
{
    auto const& ephemeralPublicKey = _message.ephemeralPublicKey;
    bcos::bytes ecdheSecret = computeSharedSecret(
        bytesConstRef(ephemeralPublicKey.data(), ephemeralPublicKey.size()), _privateKey);
    bcos::bytes sharedSecret = eciesKdf(bytesConstRef(ecdheSecret.data(), ecdheSecret.size()));
    bytesConstRef aesKey(sharedSecret.data(), kEciesKeySize);
    bytesConstRef macKey(sharedSecret.data() + kEciesKeySize, kEciesKeySize);

    auto macKeyHash = bcos::crypto::sha256Hash(macKey).asBytes();
    bcos::bytes mac = bcos::crypto::hmacSha256(bytesConstRef(macKeyHash.data(), macKeyHash.size()),
        bytesConstRef(_message.iv.data(), _message.iv.size()),
        bytesConstRef(_message.cipherText.data(), _message.cipherText.size()), _macExtraData);
    // Constant-time MAC comparison (geth uses hmac.Equal here): a byte-by-byte
    // short-circuit on a network-facing ECIES MAC is a classic timing oracle.
    if (mac.size() != _message.mac.size() ||
        CRYPTO_memcmp(mac.data(), _message.mac.data(), mac.size()) != 0)
    {
        throw std::runtime_error("EciesCipher: invalid MAC");
    }

    return bcos::crypto::aesCtrCrypt(
        bytesConstRef(_message.cipherText.data(), _message.cipherText.size()), aesKey,
        bytesConstRef(_message.iv.data(), _message.iv.size()));
}

bcos::bytes EciesCipher::serializeMessage(Message const& _message)
{
    bcos::bytes data;
    data.reserve(
        kEciesPubKeySize + _message.iv.size() + _message.cipherText.size() + _message.mac.size());
    // 0x04-prefixed uncompressed ephemeral public key.
    data.push_back(0x04);
    data.insert(data.end(), _message.ephemeralPublicKey.begin(), _message.ephemeralPublicKey.end());
    data.insert(data.end(), _message.iv.begin(), _message.iv.end());
    data.insert(data.end(), _message.cipherText.begin(), _message.cipherText.end());
    data.insert(data.end(), _message.mac.begin(), _message.mac.end());
    return data;
}

EciesCipher::Message EciesCipher::deserializeMessage(bytesConstRef _messageData)
{
    const size_t ivSize = kAesBlockSize;
    const size_t macSize = kEciesMacSize;
    const size_t minSize = kEciesPubKeySize + ivSize + macSize;
    if (_messageData.size() < minSize)
    {
        throw std::runtime_error("EciesCipher: message data too short");
    }
    if (_messageData[0] != 0x04)
    {
        throw std::runtime_error("EciesCipher: unsupported public key prefix");
    }
    const size_t cipherTextSize = _messageData.size() - minSize;

    Message message;
    message.ephemeralPublicKey.assign(_messageData.data() + 1, _messageData.data() + 1 + 64);
    message.iv.assign(
        _messageData.data() + kEciesPubKeySize, _messageData.data() + kEciesPubKeySize + ivSize);
    message.cipherText.assign(_messageData.data() + kEciesPubKeySize + ivSize,
        _messageData.data() + kEciesPubKeySize + ivSize + cipherTextSize);
    message.mac.assign(_messageData.data() + _messageData.size() - macSize,
        _messageData.data() + _messageData.size());
    return message;
}

bcos::bytes EciesCipher::encrypt(
    bytesConstRef _plainText, bytesConstRef _publicKey, bytesConstRef _macExtraData)
{
    return serializeMessage(encryptMessage(_plainText, _publicKey, _macExtraData));
}

bcos::bytes EciesCipher::decrypt(
    bytesConstRef _messageData, bytesConstRef _privateKey, bytesConstRef _macExtraData)
{
    return decryptMessage(deserializeMessage(_messageData), _privateKey, _macExtraData);
}

size_t EciesCipher::roundUpToBlockSize(size_t _size)
{
    return bcos::crypto::aesRoundUpToBlockSize(_size);
}

size_t EciesCipher::estimateEncryptedSize(size_t _size)
{
    return _size + kEciesPubKeySize + kAesBlockSize + kEciesMacSize;
}

// ---------------------------------------------------------------------------
// EccKeyPair
// ---------------------------------------------------------------------------
namespace
{
bcos::bytes publicKeyFromPrivate(bytesConstRef _privateKey)
{
    if (_privateKey.size() != 32)
    {
        throw std::invalid_argument("EccKeyPair: private key must be 32 bytes");
    }
    auto secret = std::make_shared<bcos::crypto::KeyImpl>(_privateKey.toBytes());
    bcos::crypto::Secp256k1KeyPair keyPair(secret);
    auto const& data = keyPair.publicKey()->data();
    return bcos::bytes(data.begin(), data.end());
}
}  // namespace

EccKeyPair::EccKeyPair()
{
    for (;;)
    {
        m_privateKey = bcos::crypto::cryptoRandomBytes(32);
        try
        {
            m_publicKey = publicKeyFromPrivate(ref(m_privateKey));
            break;
        }
        catch (std::exception const&)
        {
            // Extremely unlikely: an out-of-range scalar. Retry with a fresh key.
        }
    }
}

EccKeyPair::EccKeyPair(bcos::bytes _privateKey) : m_privateKey(std::move(_privateKey))
{
    m_publicKey = publicKeyFromPrivate(ref(m_privateKey));
}

// ---------------------------------------------------------------------------
// Recoverable signatures
// ---------------------------------------------------------------------------
bcos::bytes signRecoverable(bytesConstRef _hash, bytesConstRef _privateKey)
{
    if (_hash.size() != 32 || _privateKey.size() != 32)
    {
        throw std::invalid_argument("signRecoverable: invalid input sizes");
    }
    auto secret = std::make_shared<bcos::crypto::KeyImpl>(_privateKey.toBytes());
    bcos::crypto::Secp256k1KeyPair keyPair(secret);
    bcos::crypto::HashType hash(bytesConstRef(_hash.data(), 32));
    auto signature = bcos::crypto::secp256k1Sign(keyPair, hash);
    if (signature == nullptr || signature->size() != 65)
    {
        throw std::runtime_error("signRecoverable: signing failed");
    }
    return *signature;
}

PublicKey recoverPublicKey(bytesConstRef _hash, bytesConstRef _signature)
{
    if (_hash.size() != 32 || _signature.size() != 65)
    {
        throw std::invalid_argument("recoverPublicKey: invalid input sizes");
    }
    bcos::crypto::HashType hash(bytesConstRef(_hash.data(), 32));
    auto publicKey = bcos::crypto::secp256k1Recover(hash, _signature);
    if (publicKey == nullptr || publicKey->size() != 64)
    {
        throw std::runtime_error("recoverPublicKey: recovery failed");
    }
    auto const& data = publicKey->data();
    return bcos::bytes(data.begin(), data.end());
}

}  // namespace bcos::devp2p::rlpx
