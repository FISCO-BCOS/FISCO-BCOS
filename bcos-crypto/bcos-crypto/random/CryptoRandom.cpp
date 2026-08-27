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
 * @file CryptoRandom.cpp
 * @brief Secure random implementation (OpenSSL RAND_bytes).
 * @date 2026/8/18
 */
#include "CryptoRandom.h"

#include <openssl/rand.h>
#include <limits>
#include <stdexcept>

namespace bcos::crypto
{
bytes cryptoRandomBytes(size_t _size)
{
    // RAND_bytes takes an int length; reject sizes that would wrap instead of
    // silently returning uninitialized heap bytes.
    if (_size > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        throw std::invalid_argument("cryptoRandomBytes: size too large");
    }
    bytes out(_size);
    if (_size > 0 && RAND_bytes(out.data(), static_cast<int>(_size)) != 1)
    {
        throw std::runtime_error("cryptoRandomBytes: RAND_bytes failed");
    }
    return out;
}
}  // namespace bcos::crypto
