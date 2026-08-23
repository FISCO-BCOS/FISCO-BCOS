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
 * @file AesCtrCipher.cpp
 * @brief AES-CTR implementation (OpenSSL EVP).
 * @date 2026/8/18
 */
#include "AesCtrCipher.h"

#include <openssl/evp.h>
#include <stdexcept>

namespace bcos::crypto
{
AesCtrCipher::AesCtrCipher(bytesConstRef _key, bytesConstRef _iv, Direction _direction)
  : m_direction(_direction), m_ctx(EVP_CIPHER_CTX_new())
{
    if (m_ctx == nullptr)
    {
        throw std::runtime_error("AesCtrCipher: failed to allocate EVP context");
    }
    if (_key.size() != 16 && _key.size() != 32)
    {
        EVP_CIPHER_CTX_free(m_ctx);
        throw std::invalid_argument("AesCtrCipher: key must be 16 or 32 bytes");
    }
    if (_iv.size() != AES_BLOCK_SIZE)
    {
        EVP_CIPHER_CTX_free(m_ctx);
        throw std::invalid_argument("AesCtrCipher: iv must be 16 bytes");
    }
    const EVP_CIPHER* cipher = (_key.size() == 16) ? EVP_aes_128_ctr() : EVP_aes_256_ctr();
    int ok = (_direction == Direction::Encrypt) ?
                 EVP_EncryptInit_ex(m_ctx, cipher, nullptr, _key.data(), _iv.data()) :
                 EVP_DecryptInit_ex(m_ctx, cipher, nullptr, _key.data(), _iv.data());
    if (ok != 1)
    {
        EVP_CIPHER_CTX_free(m_ctx);
        throw std::runtime_error("AesCtrCipher: init failed");
    }
}

AesCtrCipher::~AesCtrCipher()
{
    if (m_ctx != nullptr)
    {
        EVP_CIPHER_CTX_free(m_ctx);
    }
}

bytes AesCtrCipher::update(bytesConstRef _data)
{
    bytes out(_data.size());
    if (_data.empty())
    {
        return out;
    }
    int outLen = 0;
    int ok = (m_direction == Direction::Encrypt) ?
                 EVP_EncryptUpdate(m_ctx, out.data(), &outLen, _data.data(),
                     static_cast<int>(_data.size())) :
                 EVP_DecryptUpdate(m_ctx, out.data(), &outLen, _data.data(),
                     static_cast<int>(_data.size()));
    if (ok != 1)
    {
        throw std::runtime_error("AesCtrCipher: update failed");
    }
    out.resize(static_cast<size_t>(outLen));
    return out;
}

bytes aesCtrCrypt(bytesConstRef _data, bytesConstRef _key, bytesConstRef _iv)
{
    AesCtrCipher cipher(_key, _iv, AesCtrCipher::Direction::Encrypt);
    return cipher.update(_data);
}
}  // namespace bcos::crypto
