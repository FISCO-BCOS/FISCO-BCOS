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
 * @file Keccak256.cpp
 * @brief Shared access to the OpenSSL Keccak-256 provider digest.
 */
#include "Keccak256.h"
#include <memory>
#include <stdexcept>

namespace bcos::crypto::hasher
{
const EVP_MD* keccak256MD()
{
    static const std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> md(
        EVP_MD_fetch(nullptr, "KECCAK-256", nullptr), &EVP_MD_free);
    if (!md) [[unlikely]]
    {
        throw std::runtime_error("EVP_MD_fetch KECCAK-256 error!");
    }
    return md.get();
}
}  // namespace bcos::crypto::hasher
