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
 * @brief implementation for secp256k1 signature algorithm
 * @file Secp256k1Signature.cpp
 * @date 2021.03.05
 * @author yujiechen
 */

#include <bcos-crypto/signature/Exceptions.h>
#include <bcos-crypto/signature/codec/SignatureDataWithV.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <wedpr-crypto/WedprCrypto.h>
#include <memory>

using namespace bcos;
using namespace bcos::crypto;

std::shared_ptr<bytes> bcos::crypto::secp256k1Sign(
    const KeyPairInterface& _keyPair, const HashType& _hash)
{
    FixedBytes<SECP256K1_SIGNATURE_LEN> signatureDataArray;
    CInputBuffer privateKey{_keyPair.secretKey()->constData(), _keyPair.secretKey()->size()};
    CInputBuffer msgHash{(const char*)_hash.data(), HashType::size};
    COutputBuffer secp256k1SignatureResult{
        (char*)signatureDataArray.data(), SECP256K1_SIGNATURE_LEN};
    auto retCode = wedpr_secp256k1_sign(&privateKey, &msgHash, &secp256k1SignatureResult);
    if (retCode != WEDPR_SUCCESS)
    {
        BOOST_THROW_EXCEPTION(SignException() << errinfo_comment(
                                  "secp256k1Sign exception, raw data: " + _hash.hex()));
    }
    std::shared_ptr<bytes> signatureData = std::make_shared<bytes>();
    *signatureData = signatureDataArray.asBytes();
    return signatureData;
}

bool bcos::crypto::secp256k1Verify(
    PublicPtr _pubKey, const HashType& _hash, bytesConstRef _signatureData)
{
    CInputBuffer publicKey{_pubKey->constData(), _pubKey->size()};
    CInputBuffer msgHash{(const char*)_hash.data(), HashType::size};
    CInputBuffer signature{(const char*)_signatureData.data(), _signatureData.size()};
    auto verifyResult = wedpr_secp256k1_verify(&publicKey, &msgHash, &signature);
    if (verifyResult == WEDPR_SUCCESS)
    {
        return true;
    }
    return false;
}

KeyPairInterface::UniquePtr bcos::crypto::secp256k1GenerateKeyPair()
{
    auto keyPair = std::make_unique<Secp256k1KeyPair>();
    COutputBuffer publicKey{keyPair->publicKey()->mutableData(), keyPair->publicKey()->size()};
    COutputBuffer privateKey{keyPair->secretKey()->mutableData(), keyPair->secretKey()->size()};
    auto retCode = wedpr_secp256k1_gen_key_pair(&publicKey, &privateKey);
    if (retCode != WEDPR_SUCCESS)
    {
        BOOST_THROW_EXCEPTION(
            GenerateKeyPairException() << errinfo_comment("secp256k1GenerateKeyPair exception"));
    }
    return keyPair;
}

PublicPtr bcos::crypto::secp256k1Recover(const HashType& _hash, bytesConstRef _signatureData)
{
    CInputBuffer msgHash{(const char*)_hash.data(), HashType::size};
    CInputBuffer signature{(const char*)_signatureData.data(), _signatureData.size()};
    auto pubKey = std::make_shared<KeyImpl>(SECP256K1_PUBLIC_LEN);
    COutputBuffer publicKeyResult{pubKey->mutableData(), pubKey->size()};
    auto retCode = wedpr_secp256k1_recover_public_key(&msgHash, &signature, &publicKeyResult);
    if (retCode != WEDPR_SUCCESS)
    {
        BOOST_THROW_EXCEPTION(InvalidSignature() << errinfo_comment(
                                  "invalid signature: secp256k1Recover failed, msgHash : " +
                                  _hash.hex() + ", signData:" + *toHexString(_signatureData)));
    }
    return pubKey;
}

std::pair<bool, bytes> bcos::crypto::secp256k1Recover(Hash::Ptr _hashImpl, bytesConstRef _input)
{
    struct
    {
        HashType hash;
        h256 v;
        h256 r;
        h256 s;
    } in;
    memcpy(&in, _input.data(), std::min(_input.size(), sizeof(_input)));
    u256 v = (u256)in.v;
    if (v >= 27 && v <= 28)
    {
        auto signatureData = std::make_shared<SignatureDataWithV>(in.r, in.s, (byte)((int)v - 27));
        try
        {
            auto encodedBytes = signatureData->encode();
            auto publicKey = secp256k1Recover(
                in.hash, bytesConstRef(encodedBytes->data(), encodedBytes->size()));
            auto address = calculateAddress(_hashImpl, publicKey);
            return {true, address.asBytes()};
        }
        catch (const std::exception& e)
        {
            CRYPTO_LOG(WARNING) << LOG_DESC("secp256k1Recover failed")
                                << LOG_KV("error", boost::diagnostic_information(e));
        }
    }
    return {false, {}};
}

bool Secp256k1Crypto::verify(
    std::shared_ptr<bytes const> _pubKeyBytes, const HashType& _hash, bytesConstRef _signatureData)
{
    return secp256k1Verify(
        std::make_shared<KeyImpl>(SECP256K1_PUBLIC_LEN, _pubKeyBytes), _hash, _signatureData);
}

KeyPairInterface::UniquePtr Secp256k1Crypto::createKeyPair(SecretPtr _secretKey)
{
    return std::make_unique<Secp256k1KeyPair>(_secretKey);
}