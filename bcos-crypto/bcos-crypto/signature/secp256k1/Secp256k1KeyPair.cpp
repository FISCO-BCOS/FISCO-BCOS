/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief implementation for secp256k1 KeyPair
 * @file Secp256k1KeyPair.cpp
 * @date 2021.03.05
 * @author yujiechen
 */
#include <bcos-crypto/signature/Exceptions.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <secp256k1.h>
#include <array>
#include <cstring>
#include <memory>

using namespace bcos;
using namespace bcos::crypto;

namespace
{
const std::unique_ptr<secp256k1_context, void (*)(secp256k1_context*)> g_SECP256K1_CTX{
    secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY),
    &secp256k1_context_destroy};
}  // namespace

bcos::crypto::Secp256k1KeyPair::Secp256k1KeyPair()
  : KeyPair(SECP256K1_PUBLIC_LEN, SECP256K1_PRIVATE_LEN, KeyPairType::Secp256K1)
{}

bcos::crypto::Secp256k1KeyPair::Secp256k1KeyPair(SecretPtr _secretKey) : Secp256k1KeyPair()
{
    m_secretKey = _secretKey;
    m_publicKey = priToPub(_secretKey);
    m_type = KeyPairType::Secp256K1;
}

bcos::crypto::PublicPtr bcos::crypto::Secp256k1KeyPair::priToPub(SecretPtr _secret)
{
    return secp256k1PriToPub(_secret);
}

bcos::crypto::PublicPtr bcos::crypto::secp256k1PriToPub(bcos::crypto::SecretPtr _secret)
{
    secp256k1_pubkey pubkey;
    if (secp256k1_ec_pubkey_create(
            g_SECP256K1_CTX.get(), &pubkey, (const unsigned char*)_secret->constData()) == 0)
    {
        BOOST_THROW_EXCEPTION(
            PriToPublicKeyException() << errinfo_comment("secp256k1PriToPub exception"));
    }
    std::array<unsigned char, SECP256K1_UNCOMPRESS_PUBLICKEY_LEN> serializedPubKey{};
    size_t serializedPubKeySize = serializedPubKey.size();
    secp256k1_ec_pubkey_serialize(g_SECP256K1_CTX.get(), serializedPubKey.data(),
        &serializedPubKeySize, &pubkey, SECP256K1_EC_UNCOMPRESSED);
    // drop the 0x04 uncompressed prefix, keep the 64-byte X||Y form
    PublicPtr pubKey = std::make_shared<KeyImpl>(SECP256K1_PUBLIC_LEN);
    memcpy(pubKey->mutableData(), serializedPubKey.data() + 1, SECP256K1_PUBLIC_LEN);
    return pubKey;
}
