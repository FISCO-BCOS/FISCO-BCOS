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
 * @file Account.cpp
 */
#include "Account.h"
#include "bcos-ops/OpsError.h"
#include <bcos-cpp-sdk/utilities/crypto/KeyPairBuilder.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <memory>

namespace bcos::ops
{
bytes loadPemPrivateKey(std::string const& _path)
{
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_file(_path.c_str(), "r"), BIO_free);
    if (!bio)
    {
        throw OpsError(c_exitUsage, "cannot open account pem: " + _path);
    }
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(
        PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!pkey)
    {
        throw OpsError(c_exitUsage, "not an unencrypted private key pem: " + _path);
    }
    std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> ec(
        EVP_PKEY_get1_EC_KEY(pkey.get()), EC_KEY_free);
    BIGNUM const* priv = ec ? EC_KEY_get0_private_key(ec.get()) : nullptr;
    if (priv == nullptr)
    {
        throw OpsError(c_exitUsage, "pem does not hold an EC private key: " + _path);
    }
    bytes key(32, 0);
    if (BN_bn2binpad(priv, key.data(), static_cast<int>(key.size())) != 32)
    {
        throw OpsError(c_exitUsage, "unexpected private key size in " + _path);
    }
    return key;
}

bcos::crypto::KeyPairInterface::UniquePtr makeAccount(
    bool _sm, std::optional<std::string> const& _pemPath)
{
    bcos::cppsdk::utilities::KeyPairBuilder builder;
    auto type = _sm ? bcos::crypto::KeyPairType::SM2 : bcos::crypto::KeyPairType::Secp256K1;
    if (!_pemPath || _pemPath->empty())
    {
        return builder.genKeyPair(type);
    }
    auto key = loadPemPrivateKey(*_pemPath);
    return builder.genKeyPair(type, bytesConstRef(key.data(), key.size()));
}
}  // namespace bcos::ops
