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
 * @file AesCtrCipher.h
 * @brief Stateful AES-CTR stream cipher (OpenSSL EVP) — used by RLPx framing
 *        (AES-256-CTR with the 32-byte aes-secret) and ECIES (AES-128-CTR).
 * @date 2026/8/18
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <cstddef>

// Opaque OpenSSL context (avoids leaking openssl/evp.h into consumers).
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;

namespace bcos::crypto
{
constexpr size_t AES_BLOCK_SIZE = 16;

// Round `size` up to the next multiple of the AES block size (16).
inline size_t aesRoundUpToBlockSize(size_t _size)
{
    return (_size + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE * AES_BLOCK_SIZE;
}

// Stateful AES-CTR stream cipher: the keystream continues across update()
// calls, so encrypting multiple frames with one instance behaves like a
// single CTR stream (exactly what RLPx framing needs).
class AesCtrCipher
{
public:
    enum class Direction
    {
        Encrypt,
        Decrypt,
    };

    // Key must be 16 (AES-128) or 32 (AES-256) bytes; iv exactly 16 bytes.
    AesCtrCipher(bytesConstRef _key, bytesConstRef _iv, Direction _direction);
    ~AesCtrCipher();

    AesCtrCipher(AesCtrCipher const&) = delete;
    AesCtrCipher& operator=(AesCtrCipher const&) = delete;

    bytes update(bytesConstRef _data);

private:
    Direction m_direction;
    EVP_CIPHER_CTX* m_ctx;
};

// One-shot AES-CTR encrypt/decrypt (identical for CTR mode).
bytes aesCtrCrypt(bytesConstRef _data, bytesConstRef _key, bytesConstRef _iv);
}  // namespace bcos::crypto
