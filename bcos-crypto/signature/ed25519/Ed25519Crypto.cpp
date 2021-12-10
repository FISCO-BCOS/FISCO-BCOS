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
#include "Ed25519Crypto.h"
#include "../Exceptions.h"
#include "../codec/SignatureDataWithPub.h"
#include "Ed25519KeyPair.h"
#include <wedpr-crypto/WedprCrypto.h>

using namespace bcos;
using namespace bcos::crypto;
std::shared_ptr<bytes> bcos::crypto::ed25519Sign(
    KeyPairInterface::Ptr _keyPair, const HashType& _messageHash, bool _signatureWithPub)
{
    CInputBuffer privateKey{_keyPair->secretKey()->constData(), _keyPair->secretKey()->size()};
    CInputBuffer messagHash{(const char*)_messageHash.data(), HashType::size};
    FixedBytes<ED25519_SIGNATURE_LEN> signatureArray;
    COutputBuffer signatureResult{(char*)signatureArray.data(), ED25519_SIGNATURE_LEN};
    auto retCode = wedpr_ed25519_sign(&privateKey, &messagHash, &signatureResult);
    if (retCode != WEDPR_SUCCESS)
    {
        BOOST_THROW_EXCEPTION(SignException() << errinfo_comment(
                                  "secp256k1Sign exception, messageHash: " + _messageHash.hex()));
    }
    auto signatureData = std::make_shared<bytes>();
    *signatureData = signatureArray.asBytes();
    if (_signatureWithPub)
    {
        signatureData->insert(signatureData->end(), _keyPair->publicKey()->mutableData(),
            _keyPair->publicKey()->mutableData() + _keyPair->publicKey()->size());
    }
    return signatureData;
}

KeyPairInterface::Ptr bcos::crypto::ed25519GenerateKeyPair()
{
    auto ed25519KeyPair = std::make_shared<Ed25519KeyPair>();
    COutputBuffer publicKey{
        ed25519KeyPair->publicKey()->mutableData(), ed25519KeyPair->publicKey()->size()};
    COutputBuffer privateKey{
        ed25519KeyPair->secretKey()->mutableData(), ed25519KeyPair->secretKey()->size()};
    if (wedpr_ed25519_gen_key_pair(&publicKey, &privateKey) != WEDPR_SUCCESS)
    {
        BOOST_THROW_EXCEPTION(
            GenerateKeyPairException() << errinfo_comment("ed25519GenerateKeyPair exception"));
    }
    return ed25519KeyPair;
}

bool bcos::crypto::ed25519Verify(
    PublicPtr _pubKey, const HashType& _messageHash, bytesConstRef _signatureData)
{
    CInputBuffer publicKey{_pubKey->constData(), _pubKey->size()};
    CInputBuffer msgHash{(const char*)_messageHash.data(), HashType::size};

    auto signatureWithoutPub = bytesConstRef(_signatureData.data(), ED25519_SIGNATURE_LEN);
    CInputBuffer signatureData{(const char*)signatureWithoutPub.data(), signatureWithoutPub.size()};
    if (wedpr_ed25519_verify(&publicKey, &msgHash, &signatureData) != WEDPR_SUCCESS)
    {
        return false;
    }
    return true;
}

PublicPtr bcos::crypto::ed25519Recover(const HashType& _messageHash, bytesConstRef _signatureData)
{
    auto signature = std::make_shared<SignatureDataWithPub>(_signatureData);
    auto ed25519Pub = std::make_shared<KeyImpl>(ED25519_PUBLIC_LEN, signature->pub());
    if (!ed25519Verify(ed25519Pub, _messageHash, _signatureData))
    {
        BOOST_THROW_EXCEPTION(
            InvalidSignature() << errinfo_comment(
                "invalid signature: ed25519 recover failed, msgHash : " + _messageHash.hex() +
                ", signature:" + *toHexString(_signatureData)));
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
    memcpy(&in, _input.data(), std::min(_input.size(), sizeof(_input)));
    // verify the signature
    auto signatureData = std::make_shared<SignatureDataWithPub>(in.r, in.s, in.pub.ref());
    try
    {
        auto encodedData = signatureData->encode();
        auto ed25519Pub = std::make_shared<KeyImpl>(ED25519_PUBLIC_LEN, signatureData->pub());
        if (ed25519Verify(
                ed25519Pub, in.hash, bytesConstRef(encodedData->data(), encodedData->size())))
        {
            auto address = calculateAddress(_hashImpl, ed25519Pub);
            return {true, address.asBytes()};
        }
    }
    catch (const std::exception& e)
    {
        BCOS_LOG(WARNING) << LOG_DESC("ed25519Recover failed")
                          << LOG_KV("error", boost::diagnostic_information(e));
    }
    return {false, {}};
}


bool Ed25519Crypto::verify(
    std::shared_ptr<bytes const> _pubKeyBytes, const HashType& _hash, bytesConstRef _signatureData)
{
    return ed25519Verify(
        std::make_shared<KeyImpl>(ED25519_PUBLIC_LEN, _pubKeyBytes), _hash, _signatureData);
}

KeyPairInterface::Ptr Ed25519Crypto::createKeyPair(SecretPtr _secretKey)
{
    return std::make_shared<Ed25519KeyPair>(_secretKey);
}