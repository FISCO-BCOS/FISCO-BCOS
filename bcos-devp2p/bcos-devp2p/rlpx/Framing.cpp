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
 * @file Framing.cpp
 * @brief RLPx framing implementation (port of silkworm framing_cipher).
 * @date 2026/8/18
 */
#include "Framing.h"

#include "Crypto.h"
#include <bcos-crypto/encrypt/AesCtrCipher.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <stdexcept>

namespace bcos::devp2p::rlpx
{
using bcos::crypto::kAesBlockSize;

namespace
{
// The fixed 3-byte header-data (capability/context/frame ids are all zero);
// the receiver only reads the 3-byte frame size, and the MAC covers the
// ciphertext, so this constant is wire-compatible with geth's zeroHeader.
constexpr std::array<bcos::byte, 3> kZeroHeader = {0xC2, 0x80, 0x80};

// Single-block AES-256-ECB encryption of a 16-byte input (the RLPx MAC seed).
bcos::bytes aes256EcbEncryptBlock(bytesConstRef _block, bytesConstRef _key)
{
    if (_block.size() != 16 || _key.size() != 32)
    {
        throw std::invalid_argument("aes256EcbEncryptBlock: invalid sizes");
    }
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    bcos::bytes out(16, 0);
    int outLen = 0;
    int ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr, _key.data(), nullptr) == 1 &&
             EVP_EncryptUpdate(ctx, out.data(), &outLen, _block.data(), 16) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok || outLen != 16)
    {
        throw std::runtime_error("aes256EcbEncryptBlock: AES-ECB failed");
    }
    return out;
}

bcos::bytes serializeFrameSize(size_t _size)
{
    bcos::bytes data(3, 0);
    data[0] = static_cast<bcos::byte>((_size >> 16) & 0xff);
    data[1] = static_cast<bcos::byte>((_size >> 8) & 0xff);
    data[2] = static_cast<bcos::byte>(_size & 0xff);
    return data;
}

size_t deserializeFrameSize(bytesConstRef _data)
{
    if (_data.size() < 3)
    {
        throw std::runtime_error("FramingCipher: frame size data too short");
    }
    return (static_cast<size_t>(_data[0]) << 16) | (static_cast<size_t>(_data[1]) << 8) |
           static_cast<size_t>(_data[2]);
}
}  // namespace

class FramingCipher::Impl
{
public:
    Impl(KeyMaterial const& _keyMaterial, bcos::bytes _aesSecret, bcos::bytes _macSecret)
      : m_macSecret(std::move(_macSecret)),
        m_egressDataCipher(bytesConstRef(_aesSecret.data(), _aesSecret.size()), zeroIv(),
            bcos::crypto::AesCtrCipher::Direction::Encrypt),
        m_ingressDataCipher(bytesConstRef(_aesSecret.data(), _aesSecret.size()), zeroIv(),
            bcos::crypto::AesCtrCipher::Direction::Decrypt)
    {
        initMacHashers(_keyMaterial);
    }

    bcos::bytes encryptFrame(bcos::bytes _frameData)
    {
        // The RLPx wire format encodes the frame size in 3 bytes (24 bits). A
        // frame of 2^24 bytes or more would silently truncate mod 2^24 and
        // corrupt the stream — fail closed like geth (errPlainMessageTooLarge).
        if (_frameData.size() > 0xFFFFFF)
        {
            throw std::runtime_error("FramingCipher: frame too large (max 16MB)");
        }
        bcos::bytes headerData(kZeroHeader.begin(), kZeroHeader.end());

        bcos::bytes header;
        header.reserve(16);
        auto sizeBytes = serializeFrameSize(_frameData.size());
        header.insert(header.end(), sizeBytes.begin(), sizeBytes.end());
        header.insert(header.end(), headerData.begin(), headerData.end());
        header.resize(16, 0);

        bcos::bytes headerCipherText = m_egressDataCipher.update(ref(header));
        bcos::bytes headerMacValue = headerMac(m_egressMacHasher, ref(headerCipherText));

        _frameData.resize(bcos::crypto::aesRoundUpToBlockSize(_frameData.size()), 0);
        bcos::bytes frameCipherText = m_egressDataCipher.update(ref(_frameData));
        bcos::bytes frameMacValue = frameMac(m_egressMacHasher, ref(frameCipherText));

        bcos::bytes data;
        data.reserve(headerCipherText.size() + headerMacValue.size() + frameCipherText.size() +
                     frameMacValue.size());
        data.insert(data.end(), headerCipherText.begin(), headerCipherText.end());
        data.insert(data.end(), headerMacValue.begin(), headerMacValue.end());
        data.insert(data.end(), frameCipherText.begin(), frameCipherText.end());
        data.insert(data.end(), frameMacValue.begin(), frameMacValue.end());
        return data;
    }

    size_t decryptHeader(bytesConstRef _headerCipherText, bytesConstRef _headerMac)
    {
        auto expectedMac = headerMac(m_ingressMacHasher, _headerCipherText);
        // Constant-time MAC comparison (mirrors geth's hmac.Equal).
        if (expectedMac.size() != _headerMac.size() ||
            CRYPTO_memcmp(expectedMac.data(), _headerMac.data(), expectedMac.size()) != 0)
        {
            throw std::runtime_error("FramingCipher: invalid header MAC");
        }
        bcos::bytes header = m_ingressDataCipher.update(_headerCipherText);
        return deserializeFrameSize(ref(header));
    }

    bcos::bytes decryptFrame(
        bytesConstRef _frameCipherText, bytesConstRef _frameMac, size_t _frameSize)
    {
        auto expectedMac = frameMac(m_ingressMacHasher, _frameCipherText);
        // Constant-time MAC comparison (mirrors geth's hmac.Equal).
        if (expectedMac.size() != _frameMac.size() ||
            CRYPTO_memcmp(expectedMac.data(), _frameMac.data(), expectedMac.size()) != 0)
        {
            throw std::runtime_error("FramingCipher: invalid frame MAC");
        }
        bcos::bytes frameData = m_ingressDataCipher.update(_frameCipherText);
        frameData.resize(_frameSize);
        return frameData;
    }

private:
    static bytesConstRef zeroIv()
    {
        static bcos::bytes iv(16, 0);
        return bytesConstRef(iv.data(), iv.size());
    }

    void initMacHashers(KeyMaterial const& _keyMaterial)
    {
        auto initiatorNonce = _keyMaterial.initiatorNonce;
        xorBytes(initiatorNonce, ref(m_macSecret));
        auto recipientNonce = _keyMaterial.recipientNonce;
        xorBytes(recipientNonce, ref(m_macSecret));

        auto& initiatorHasher = _keyMaterial.isInitiator ? m_egressMacHasher : m_ingressMacHasher;
        auto& recipientHasher = _keyMaterial.isInitiator ? m_ingressMacHasher : m_egressMacHasher;

        initiatorHasher.update(ref(recipientNonce));
        initiatorHasher.update(ref(_keyMaterial.initiatorFirstMessageData));

        recipientHasher.update(ref(initiatorNonce));
        recipientHasher.update(ref(_keyMaterial.recipientFirstMessageData));
    }

    bcos::bytes headerMac(Sha3Hasher& _hasher, bytesConstRef _headerCipherText)
    {
        if (_headerCipherText.size() < 16)
        {
            throw std::runtime_error("FramingCipher: header ciphertext too short");
        }
        auto digest = _hasher.hash();
        auto seed = aes256EcbEncryptBlock(bytesConstRef(digest.data(), 16), ref(m_macSecret));
        xorBytes(seed, _headerCipherText);
        _hasher.update(ref(seed));
        auto finalDigest = _hasher.hash();
        finalDigest.resize(16);
        return finalDigest;
    }

    bcos::bytes frameMac(Sha3Hasher& _hasher, bytesConstRef _frameCipherText)
    {
        _hasher.update(_frameCipherText);
        auto digest = _hasher.hash();
        auto seed = aes256EcbEncryptBlock(bytesConstRef(digest.data(), 16), ref(m_macSecret));
        xorBytes(seed, bytesConstRef(digest.data(), digest.size()));
        _hasher.update(ref(seed));
        auto finalDigest = _hasher.hash();
        finalDigest.resize(16);
        return finalDigest;
    }

    bcos::bytes m_macSecret;
    bcos::crypto::AesCtrCipher m_egressDataCipher;
    bcos::crypto::AesCtrCipher m_ingressDataCipher;
    Sha3Hasher m_egressMacHasher;
    Sha3Hasher m_ingressMacHasher;
};

namespace
{
void makeSecrets(FramingCipher::KeyMaterial const& _keyMaterial, bcos::bytes& _aesSecret,
    bcos::bytes& _macSecret)
{
    // aes-secret = keccak256(ecdhe || keccak256(recipientNonce || initiatorNonce))
    // mac-secret = keccak256(ecdhe || aes-secret)
    auto nonceHash = keccak256(ref(_keyMaterial.recipientNonce), ref(_keyMaterial.initiatorNonce));
    auto sharedSecret = keccak256(
        ref(_keyMaterial.ephemeralSharedSecret), bytesConstRef(nonceHash.data(), nonceHash.size()));
    _aesSecret = keccak256(ref(_keyMaterial.ephemeralSharedSecret),
        bytesConstRef(sharedSecret.data(), sharedSecret.size()));
    _macSecret = keccak256(ref(_keyMaterial.ephemeralSharedSecret),
        bytesConstRef(_aesSecret.data(), _aesSecret.size()));
}
}  // namespace

FramingCipher::FramingCipher(KeyMaterial const& _keyMaterial) : m_impl(nullptr)
{
    bcos::bytes aesSecret;
    bcos::bytes macSecret;
    deriveSecrets(_keyMaterial, aesSecret, macSecret);
    m_impl = new Impl(_keyMaterial, std::move(aesSecret), std::move(macSecret));
}

void FramingCipher::deriveSecrets(
    KeyMaterial const& _keyMaterial, bcos::bytes& _aesSecret, bcos::bytes& _macSecret)
{
    makeSecrets(_keyMaterial, _aesSecret, _macSecret);
}

FramingCipher::~FramingCipher()
{
    delete m_impl;
}

FramingCipher::FramingCipher(FramingCipher&& _other) noexcept : m_impl(_other.m_impl)
{
    _other.m_impl = nullptr;
}

FramingCipher& FramingCipher::operator=(FramingCipher&& _other) noexcept
{
    if (this != &_other)
    {
        delete m_impl;
        m_impl = _other.m_impl;
        _other.m_impl = nullptr;
    }
    return *this;
}

bcos::bytes FramingCipher::encryptFrame(bcos::bytes _frameData)
{
    return m_impl->encryptFrame(std::move(_frameData));
}

size_t FramingCipher::headerSize()
{
    // 16B ciphertext + 16B MAC.
    return kAesBlockSize * 2;
}

size_t FramingCipher::decryptHeader(bytesConstRef _data)
{
    if (_data.size() < headerSize())
    {
        throw std::runtime_error("FramingCipher: header data too short");
    }
    return m_impl->decryptHeader(bytesConstRef(_data.data(), kAesBlockSize),
        bytesConstRef(_data.data() + kAesBlockSize, kAesBlockSize));
}

size_t FramingCipher::frameSize(size_t _headerFrameSize)
{
    // padded ciphertext + 16B MAC.
    return bcos::crypto::aesRoundUpToBlockSize(_headerFrameSize) + kAesBlockSize;
}

bcos::bytes FramingCipher::decryptFrame(bytesConstRef _data, size_t _headerFrameSize)
{
    if (_data.size() < frameSize(_headerFrameSize))
    {
        throw std::runtime_error("FramingCipher: frame data too short");
    }
    // Slice by the computed frame size, not the caller buffer length: a socket
    // read buffer may hold coalesced bytes of the next frame, and MAC-ing them
    // would desynchronize the running ingress MAC hasher.
    auto const paddedSize = bcos::crypto::aesRoundUpToBlockSize(_headerFrameSize);
    return m_impl->decryptFrame(bytesConstRef(_data.data(), paddedSize),
        bytesConstRef(_data.data() + paddedSize, kAesBlockSize), _headerFrameSize);
}

}  // namespace bcos::devp2p::rlpx
