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
 * @brief implementation for sm2 signature
 * @file SM2Crypto.cpp
 * @date 2021.03.10
 * @author yujiechen
 */
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/Exceptions.h>
#include <bcos-crypto/signature/codec/SignatureDataWithPub.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-crypto/signature/sm2/SM2KeyPair.h>

using namespace bcos;
using namespace bcos::crypto;
bool SM2Crypto::verify(
    std::shared_ptr<bytes const> _pubKeyBytes, const HashType& _hash, bytesConstRef _signatureData)
{
    return verify(
        std::make_shared<KeyImpl>(SM2_PUBLIC_KEY_LEN, _pubKeyBytes), _hash, _signatureData);
}

KeyPairInterface::UniquePtr SM2Crypto::createKeyPair(SecretPtr _secretKey)
{
    return m_keyPairFactory->createKeyPair(_secretKey);
}

std::shared_ptr<bytes> SM2Crypto::sign(
    const KeyPairInterface& _keyPair, const HashType& _hash, bool _signatureWithPub)
{
    FixedBytes<SM2_SIGNATURE_LEN> signatureDataArray;
    CInputBuffer rawPrivateKey{_keyPair.secretKey()->constData(), _keyPair.secretKey()->size()};
    CInputBuffer rawPublicKey{_keyPair.publicKey()->constData(), _keyPair.publicKey()->size()};
    CInputBuffer rawMsgHash{(const char*)_hash.data(), HashType::size};
    COutputBuffer sm2SignatureResult{(char*)signatureDataArray.data(), SM2_SIGNATURE_LEN};
    auto retCode = m_signer(&rawPrivateKey, &rawPublicKey, &rawMsgHash, &sm2SignatureResult);
    if (retCode != WEDPR_SUCCESS)
    {
        BOOST_THROW_EXCEPTION(
            SignException() << errinfo_comment("sm2Sign failed, raw data: " + _hash.hex()));
    }
    std::shared_ptr<bytes> signatureData = std::make_shared<bytes>();
    *signatureData = signatureDataArray.asBytes();
    // append the public key
    if (_signatureWithPub)
    {
        signatureData->insert(signatureData->end(), _keyPair.publicKey()->mutableData(),
            _keyPair.publicKey()->mutableData() + _keyPair.publicKey()->size());
    }
    return signatureData;
}

bool SM2Crypto::verify(PublicPtr _pubKey, const HashType& _hash, bytesConstRef _signatureData)
{
    CInputBuffer publicKey{_pubKey->constData(), _pubKey->size()};
    CInputBuffer messageHash{(const char*)_hash.data(), HashType::size};

    auto signatureWithoutPub = bytesConstRef(_signatureData.data(), SM2_SIGNATURE_LEN);
    CInputBuffer signature{(const char*)signatureWithoutPub.data(), signatureWithoutPub.size()};
    auto verifyResult = m_verifier(&publicKey, &messageHash, &signature);
    if (verifyResult == WEDPR_SUCCESS)
    {
        return true;
    }
    return false;
}

PublicPtr SM2Crypto::recover(const HashType& _hash, bytesConstRef _signData)
{
    auto signatureStruct = std::make_shared<SignatureDataWithPub>(_signData);
    auto sm2Pub = std::make_shared<KeyImpl>(SM2_PUBLIC_KEY_LEN, signatureStruct->pub());
    if (verify(sm2Pub, _hash, _signData))
    {
        return sm2Pub;
    }
    BOOST_THROW_EXCEPTION(InvalidSignature() << errinfo_comment(
                              "invalid signature: sm2 recover public key failed, msgHash : " +
                              _hash.hex() + ", signature:" + *toHexString(_signData)));
}

std::pair<bool, bytes> SM2Crypto::recoverAddress(Hash::Ptr _hashImpl, bytesConstRef _input)
{
    struct
    {
        HashType hash;
        h512 pub;
        h256 r;
        h256 s;
    } in;
    memcpy(&in, _input.data(), std::min(_input.size(), sizeof(_input)));
    // verify the signature
    auto signatureData = std::make_shared<SignatureDataWithPub>(in.r, in.s, in.pub.ref());
    try
    {
        auto encodedData = signatureData->encode();
        auto sm2Pub = std::make_shared<KeyImpl>(SM2_PUBLIC_KEY_LEN, signatureData->pub());
        if (verify(sm2Pub, in.hash, bytesConstRef(encodedData->data(), encodedData->size())))
        {
            auto address = calculateAddress(_hashImpl, sm2Pub);
            return {true, address.asBytes()};
        }
    }
    catch (const std::exception& e)
    {
        CRYPTO_LOG(WARNING) << LOG_DESC("sm2Recover failed")
                            << LOG_KV("error", boost::diagnostic_information(e));
    }
    return {false, {}};
}

KeyPairInterface::UniquePtr SM2Crypto::generateKeyPair()
{
    return m_keyPairFactory->generateKeyPair();
}
