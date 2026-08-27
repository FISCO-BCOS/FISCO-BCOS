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
 * @file HmacSha256.cpp
 * @brief HMAC-SHA256 implementation (OpenSSL one-shot HMAC).
 * @date 2026/8/18
 */
#include "HmacSha256.h"

#include <openssl/hmac.h>
#include <limits>
#include <stdexcept>
#include <vector>

namespace bcos::crypto
{
namespace
{
bytes hmacSha256Segments(bytesConstRef _key, std::vector<bytesConstRef> const& _parts)
{
    size_t total = 0;
    for (auto const& part : _parts)
    {
        total += part.size();
    }
    bytes data;
    data.reserve(total);
    for (auto const& part : _parts)
    {
        data.insert(data.end(), part.begin(), part.end());
    }

    bytes out(EVP_MAX_MD_SIZE);
    unsigned int outLen = 0;
    // HMAC takes an int key length; reject sizes that would wrap and silently
    // produce a wrong-length MAC.
    if (_key.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        throw std::invalid_argument("hmacSha256: key too large");
    }
    if (HMAC(EVP_sha256(), _key.data(), static_cast<int>(_key.size()), data.data(), data.size(),
            out.data(), &outLen) == nullptr)
    {
        throw std::runtime_error("hmacSha256: HMAC failed");
    }
    out.resize(outLen);
    return out;
}
}  // namespace

bytes hmacSha256(bytesConstRef _key, bytesConstRef _data1)
{
    return hmacSha256Segments(_key, {_data1});
}

bytes hmacSha256(bytesConstRef _key, bytesConstRef _data1, bytesConstRef _data2)
{
    return hmacSha256Segments(_key, {_data1, _data2});
}

bytes hmacSha256(bytesConstRef _key, bytesConstRef _data1, bytesConstRef _data2, bytesConstRef _data3)
{
    return hmacSha256Segments(_key, {_data1, _data2, _data3});
}
}  // namespace bcos::crypto
