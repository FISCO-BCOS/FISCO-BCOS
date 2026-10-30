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
 * @brief implementation for ed25519 signature algorithm
 * @file Ed25519Crypto.cpp
 * @date 2021.04.01
 * @author yujiechen
 */
#include <bcos-crypto/signature/Exceptions.h>
#include <bcos-crypto/signature/codec/SignatureDataWithPub.h>
#include <bcos-crypto/signature/ed25519/Ed25519Crypto.h>
#include <bcos-crypto/signature/ed25519/Ed25519KeyPair.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/LogStream.h>
#include <openssl/evp.h>
#include <memory>

using namespace bcos;
using namespace bcos::crypto;

namespace
{
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
}  // namespace

std::shared_ptr<bytes> bcos::crypto::ed25519Sign(
    const KeyPairInterface& _keyPair, const HashType& _messageHash, bool _signatureWithPub)
{
    EvpPkeyPtr key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                       reinterpret_cast<const unsigned char*>(_keyPair.secretKey()->constData()),
                       _keyPair.secretKey()->size()),
        &EVP_PKEY_free);
    EvpMdCtxPtr ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    FixedBytes<ED25519_SIGNATURE_LEN> signatureArray;
    size_t signatureLen = ED25519_SIGNATURE_LEN;
    // one-shot Ed25519 (NULL digest, PureEdDSA)
    if (!key || !ctx || EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1 ||
        EVP_DigestSign(ctx.get(), signatureArray.data(), &signatureLen, _messageHash.data(),
            HashType::SIZE) != 1)
    {
        BOOST_THROW_EXCEPTION(SignException() << errinfo_comment(
                                  "ed25519Sign exception, messageHash: " + _messageHash.hex()));
    }
    auto signatureData = std::make_shared<bytes>();
    *signatureData = signatureArray.asBytes();
    if (_signatureWithPub)
    {
        signatureData->insert(signatureData->end(), _keyPair.publicKey()->mutableData(),
            _keyPair.publicKey()->mutableData() + _keyPair.publicKey()->size());
    }
    return signatureData;
}

KeyPairInterface::UniquePtr bcos::crypto::ed25519GenerateKeyPair()
{
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> keyGenCtx(
        EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), &EVP_PKEY_CTX_free);
    EVP_PKEY* rawKey = nullptr;
    if (!keyGenCtx || EVP_PKEY_keygen_init(keyGenCtx.get()) != 1 ||
        EVP_PKEY_keygen(keyGenCtx.get(), &rawKey) != 1)
    {
        BOOST_THROW_EXCEPTION(
            GenerateKeyPairException() << errinfo_comment("ed25519GenerateKeyPair exception"));
    }
    EvpPkeyPtr key(rawKey, &EVP_PKEY_free);
    auto ed25519KeyPair = std::make_unique<Ed25519KeyPair>();
    size_t publicKeyLen = ed25519KeyPair->publicKey()->size();
    size_t privateKeyLen = ed25519KeyPair->secretKey()->size();
    if (EVP_PKEY_get_raw_public_key(key.get(),
            reinterpret_cast<unsigned char*>(ed25519KeyPair->publicKey()->mutableData()),
            &publicKeyLen) != 1 ||
        EVP_PKEY_get_raw_private_key(key.get(),
            reinterpret_cast<unsigned char*>(ed25519KeyPair->secretKey()->mutableData()),
            &privateKeyLen) != 1)
    {
        BOOST_THROW_EXCEPTION(
            GenerateKeyPairException() << errinfo_comment("ed25519GenerateKeyPair exception"));
    }
    return ed25519KeyPair;
}

bool bcos::crypto::ed25519Verify(
    PublicPtr _pubKey, const HashType& _messageHash, bytesConstRef _signatureData)
{
    EvpPkeyPtr key(
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
            reinterpret_cast<const unsigned char*>(_pubKey->constData()), _pubKey->size()),
        &EVP_PKEY_free);
    EvpMdCtxPtr ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!key || !ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1)
    {
        return false;
    }
    // reject short signatures while the length is still known: the fixed-length view below
    // would otherwise read past the caller's buffer
    if (_signatureData.size() < ED25519_SIGNATURE_LEN)
    {
        return false;
    }
    auto signatureWithoutPub = bytesConstRef(_signatureData.data(), ED25519_SIGNATURE_LEN);
    return EVP_DigestVerify(ctx.get(), signatureWithoutPub.data(), signatureWithoutPub.size(),
               _messageHash.data(), HashType::SIZE) == 1;
}

PublicPtr bcos::crypto::ed25519Recover(const HashType& _messageHash, bytesConstRef _signatureData)
{
    auto signature = std::make_shared<SignatureDataWithPub>(_signatureData);
    auto ed25519Pub = std::make_shared<KeyImpl>(ED25519_PUBLIC_LEN, ref(signature->pub()));
    if (!ed25519Verify(ed25519Pub, _messageHash, _signatureData))
    {
        BOOST_THROW_EXCEPTION(InvalidSignature() << errinfo_comment(
                                  "invalid signature: ed25519 recover failed, msgHash : " +
                                  _messageHash.hex() + ", signature:" + toHex(_signatureData)));
    }
    return ed25519Pub;
}

std::pair<bool, bytes> bcos::crypto::ed25519Recover(Hash::Ptr _hashImpl, bytesConstRef _input)
{
    struct
    {
        HashType hash;
        h256 pub;
        h256 r;
        h256 s;
    } in;
    if (_input.size() < sizeof(in))
    {
        return {false, {}};
    }
    memcpy(&in, _input.data(), sizeof(in));
    // verify the signature
    auto signatureData = std::make_shared<SignatureDataWithPub>(in.r, in.s, in.pub.ref());
    try
    {
        auto encodedData = signatureData->encode();
        auto ed25519Pub = std::make_shared<KeyImpl>(ED25519_PUBLIC_LEN, ref(signatureData->pub()));
        if (ed25519Verify(
                ed25519Pub, in.hash, bytesConstRef(encodedData->data(), encodedData->size())))
        {
            auto address = calculateAddress(_hashImpl, ed25519Pub);
            return {true, address.asBytes()};
        }
    }
    catch (const std::exception& e)
    {
        CRYPTO_LOG(WARNING) << LOG_DESC("ed25519Recover failed")
                            << LOG_KV("message", boost::diagnostic_information(e));
    }
    return {false, {}};
}


bool Ed25519Crypto::verify(std::shared_ptr<bytes const> _pubKeyBytes, const HashType& _hash,
    bytesConstRef _signatureData) const
{
    return ed25519Verify(
        std::make_shared<KeyImpl>(ED25519_PUBLIC_LEN, _pubKeyBytes), _hash, _signatureData);
}

KeyPairInterface::UniquePtr Ed25519Crypto::createKeyPair(SecretPtr _secretKey) const
{
    return std::make_unique<Ed25519KeyPair>(_secretKey);
}
