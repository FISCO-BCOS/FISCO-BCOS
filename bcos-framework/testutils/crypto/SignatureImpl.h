/*
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
 * @file SignatureImpl.h
 * @author: yujiechen
 * @date: 2021-04-06
 */
#pragma once
#include "../../interfaces/crypto/KeyInterface.h"
#include "../../interfaces/crypto/KeyPairInterface.h"
#include "../../interfaces/crypto/Signature.h"
#include "../../libutilities/DataConvertUtility.h"
#include "../../libutilities/Exceptions.h"
#include <wedpr-crypto/WedprCrypto.h>
namespace bcos
{
namespace test
{
DERIVE_BCOS_EXCEPTION(SignException);
DERIVE_BCOS_EXCEPTION(InvalidSignature);
DERIVE_BCOS_EXCEPTION(InvalidSignatureData);
DERIVE_BCOS_EXCEPTION(InvalidKey);
DERIVE_BCOS_EXCEPTION(GenerateKeyPairException);
class CommonKeyImpl : public crypto::KeyInterface
{
public:
    using Ptr = std::shared_ptr<CommonKeyImpl>;
    explicit CommonKeyImpl(size_t _keySize) : m_keyData(std::make_shared<bytes>(_keySize)) {}
    explicit CommonKeyImpl(size_t _keySize, bytes const& _data) : CommonKeyImpl(_keySize)
    {
        decode(ref(_data));
    }
    explicit CommonKeyImpl(bytesConstRef _data) : m_keyData(std::make_shared<bytes>())
    {
        decode(_data);
    }
    explicit CommonKeyImpl(bytes const& _data) : CommonKeyImpl(ref(_data)) {}
    explicit CommonKeyImpl(size_t _keySize, std::shared_ptr<bytes> _data)
    {
        if (_data->size() < _keySize)
        {
            BOOST_THROW_EXCEPTION(InvalidKey() << errinfo_comment(
                                      "invalidKey, the key size: " + std::to_string(_data->size()) +
                                      ", expected size:" + std::to_string(_keySize)));
        }
        m_keyData = _data;
    }
    explicit CommonKeyImpl(size_t _keySize, std::shared_ptr<const bytes> _data)
    {
        if (_data->size() < _keySize)
        {
            BOOST_THROW_EXCEPTION(InvalidKey() << errinfo_comment(
                                      "invalidKey, the key size: " + std::to_string(_data->size()) +
                                      ", expected size:" + std::to_string(_keySize)));
        }
        m_keyData = std::make_shared<bytes>(*_data);
    }
    ~CommonKeyImpl() override {}

    const bytes& data() const override { return *m_keyData; }
    size_t size() const override { return m_keyData->size(); }
    char* mutableData() override { return (char*)m_keyData->data(); }
    const char* constData() const override { return (const char*)m_keyData->data(); }
    std::shared_ptr<bytes> encode() const override { return m_keyData; }
    void decode(bytesConstRef _data) override { m_keyData->assign(_data.begin(), _data.end()); }
    void decode(bytes&& _data) override { *m_keyData = std::move(_data); }

    std::string shortHex() override
    {
        auto startIt = m_keyData->begin();
        auto endIt = m_keyData->end();
        if (m_keyData->size() > 4)
        {
            endIt = startIt + 4 * sizeof(byte);
        }
        return *toHexString(startIt, endIt) + "...";
    }

    std::string hex() override { return *toHexString(*m_keyData); }

private:
    std::shared_ptr<bytes> m_keyData;
};

class CommonKeyPair : public crypto::KeyPairInterface
{
public:
    using Ptr = std::shared_ptr<CommonKeyPair>;
    CommonKeyPair(int _pubKeyLen, int _secretLen)
      : m_publicKey(std::make_shared<CommonKeyImpl>(_pubKeyLen)),
        m_secretKey(std::make_shared<CommonKeyImpl>(_secretLen))
    {}

    ~CommonKeyPair() override {}
    crypto::SecretPtr secretKey() const override { return m_secretKey; }
    crypto::PublicPtr publicKey() const override { return m_publicKey; }

    Address address(crypto::Hash::Ptr _hashImpl) override
    {
        return right160(_hashImpl->hash(m_publicKey));
    }

protected:
    crypto::PublicPtr m_publicKey;
    crypto::SecretPtr m_secretKey;
};

class CommonKeyPairImpl : public CommonKeyPair
{
public:
    using Ptr = std::shared_ptr<CommonKeyPairImpl>;
    CommonKeyPairImpl() : CommonKeyPair(64, 32) {}
};

class Secp256k1SignatureImpl : public crypto::SignatureCrypto
{
public:
    using Ptr = std::shared_ptr<Secp256k1SignatureImpl>;
    Secp256k1SignatureImpl() = default;
    virtual ~Secp256k1SignatureImpl() {}

    // sign
    std::shared_ptr<bytes> sign(
        crypto::KeyPairInterface::Ptr _keyPair, const crypto::HashType& _hash, bool) override
    {
        FixedBytes<65> signatureDataArray;
        CInputBuffer privateKey{_keyPair->secretKey()->constData(), _keyPair->secretKey()->size()};
        CInputBuffer msgHash{(const char*)_hash.data(), crypto::HashType::size};
        COutputBuffer secp256k1SignatureResult{(char*)signatureDataArray.data(), 65};
        auto retCode = wedpr_secp256k1_sign(&privateKey, &msgHash, &secp256k1SignatureResult);
        if (retCode != 0)
        {
            BOOST_THROW_EXCEPTION(SignException() << errinfo_comment(
                                      "secp256k1Sign exception, raw data: " + _hash.hex()));
        }
        std::shared_ptr<bytes> signatureData = std::make_shared<bytes>();
        *signatureData = signatureDataArray.asBytes();
        return signatureData;
    }

    // verify
    bool verify(crypto::PublicPtr _pubKey, const crypto::HashType& _hash,
        bytesConstRef _signatureData) override
    {
        CInputBuffer publicKey{_pubKey->constData(), _pubKey->size()};
        CInputBuffer msgHash{(const char*)_hash.data(), crypto::HashType::size};
        CInputBuffer signature{(const char*)_signatureData.data(), _signatureData.size()};
        auto verifyResult = wedpr_secp256k1_verify(&publicKey, &msgHash, &signature);
        if (verifyResult == 0)
        {
            return true;
        }
        return false;
    }
    bool verify(std::shared_ptr<const bytes> _pubKeyBytes, const crypto::HashType& _hash,
        bytesConstRef _signatureData) override
    {
        return verify(std::make_shared<CommonKeyImpl>(64, _pubKeyBytes), _hash, _signatureData);
    }

    // recover the public key from the given signature
    crypto::PublicPtr recover(const crypto::HashType& _hash, bytesConstRef _signatureData) override
    {
        CInputBuffer msgHash{(const char*)_hash.data(), crypto::HashType::size};
        CInputBuffer signature{(const char*)_signatureData.data(), _signatureData.size()};
        auto pubKey = std::make_shared<CommonKeyImpl>(64);
        COutputBuffer publicKeyResult{pubKey->mutableData(), pubKey->size()};
        auto retCode = wedpr_secp256k1_recover_public_key(&msgHash, &signature, &publicKeyResult);
        if (retCode != 0)
        {
            BOOST_THROW_EXCEPTION(InvalidSignature() << errinfo_comment(
                                      "invalid signature: secp256k1Recover failed, msgHash : " +
                                      _hash.hex() + ", signData:" + *toHexString(_signatureData)));
        }
        return pubKey;
    }

    // generate keyPair
    crypto::KeyPairInterface::Ptr generateKeyPair() override
    {
        auto keyPair = std::make_shared<CommonKeyPairImpl>();
        COutputBuffer publicKey{keyPair->publicKey()->mutableData(), keyPair->publicKey()->size()};
        COutputBuffer privateKey{keyPair->secretKey()->mutableData(), keyPair->secretKey()->size()};
        auto retCode = wedpr_secp256k1_gen_key_pair(&publicKey, &privateKey);
        if (retCode != 0)
        {
            BOOST_THROW_EXCEPTION(GenerateKeyPairException()
                                  << errinfo_comment("secp256k1GenerateKeyPair exception"));
        }
        return keyPair;
    }
    // recoverAddress(for precompiled)
    std::pair<bool, bytes> recoverAddress(crypto::Hash::Ptr, bytesConstRef) override
    {
        return std::make_pair(false, bytes());
    }
    bcos::crypto::KeyPairInterface::Ptr createKeyPair(bcos::crypto::SecretPtr) override
    {
        return nullptr;
    }
};

class SM2SignatureImpl : public crypto::SignatureCrypto
{
public:
    using Ptr = std::shared_ptr<SM2SignatureImpl>;
    SM2SignatureImpl() = default;
    virtual ~SM2SignatureImpl() {}

    // sign
    std::shared_ptr<bytes> sign(crypto::KeyPairInterface::Ptr _keyPair,
        const crypto::HashType& _hash, bool _signatureWithPub = false) override
    {
        FixedBytes<64> signatureDataArray;
        CInputBuffer rawPrivateKey{
            _keyPair->secretKey()->constData(), _keyPair->secretKey()->size()};
        CInputBuffer rawPublicKey{
            _keyPair->publicKey()->constData(), _keyPair->publicKey()->size()};
        CInputBuffer rawMsgHash{(const char*)_hash.data(), crypto::HashType::size};
        COutputBuffer sm2SignatureResult{(char*)signatureDataArray.data(), 64};
        auto retCode =
            wedpr_sm2_sign_fast(&rawPrivateKey, &rawPublicKey, &rawMsgHash, &sm2SignatureResult);
        if (retCode != 0)
        {
            BOOST_THROW_EXCEPTION(
                SignException() << errinfo_comment("sm2Sign failed, raw data: " + _hash.hex()));
        }
        std::shared_ptr<bytes> signatureData = std::make_shared<bytes>();
        *signatureData = signatureDataArray.asBytes();
        // append the public key
        if (_signatureWithPub)
        {
            signatureData->insert(signatureData->end(), _keyPair->publicKey()->mutableData(),
                _keyPair->publicKey()->mutableData() + _keyPair->publicKey()->size());
        }
        return signatureData;
    }

    // verify
    bool verify(crypto::PublicPtr _pubKey, const crypto::HashType& _hash,
        bytesConstRef _signatureData) override
    {
        CInputBuffer publicKey{_pubKey->constData(), _pubKey->size()};
        CInputBuffer messageHash{(const char*)_hash.data(), crypto::HashType::size};

        auto signatureWithoutPub = bytesConstRef(_signatureData.data(), 64);
        CInputBuffer signature{(const char*)signatureWithoutPub.data(), signatureWithoutPub.size()};
        auto verifyResult = wedpr_sm2_verify(&publicKey, &messageHash, &signature);
        if (verifyResult == 0)
        {
            return true;
        }
        return false;
    }
    bool verify(std::shared_ptr<const bytes> _pubKeyBytes, const crypto::HashType& _hash,
        bytesConstRef _signatureData) override
    {
        return verify(std::make_shared<CommonKeyImpl>(64, _pubKeyBytes), _hash, _signatureData);
    }

    // recover the public key from the given signature
    crypto::PublicPtr recover(const crypto::HashType& _hash, bytesConstRef _signData) override
    {
        bytes publicKeyBytes;
        publicKeyBytes.insert(publicKeyBytes.end(), _signData.begin() + 64, _signData.end());
        auto sm2Pub = std::make_shared<CommonKeyImpl>(64, publicKeyBytes);
        if (verify(sm2Pub, _hash, _signData))
        {
            return sm2Pub;
        }
        BOOST_THROW_EXCEPTION(InvalidSignature() << errinfo_comment(
                                  "invalid signature: sm2 recover public key failed, msgHash : " +
                                  _hash.hex() + ", signature:" + *toHexString(_signData)));
    }

    // generate keyPair
    crypto::KeyPairInterface::Ptr generateKeyPair() override
    {
        auto keyPair = std::make_shared<CommonKeyPairImpl>();
        COutputBuffer publicKey{keyPair->publicKey()->mutableData(), keyPair->publicKey()->size()};
        COutputBuffer privateKey{keyPair->secretKey()->mutableData(), keyPair->secretKey()->size()};
        auto retCode = wedpr_sm2_gen_key_pair(&publicKey, &privateKey);
        if (retCode != 0)
        {
            BOOST_THROW_EXCEPTION(
                GenerateKeyPairException() << errinfo_comment("sm2GenerateKeyPair exception"));
        }
        return keyPair;
    }

    // recoverAddress(for precompiled)
    std::pair<bool, bytes> recoverAddress(crypto::Hash::Ptr, bytesConstRef) override
    {
        return std::make_pair(false, bytes());
    }
    bcos::crypto::KeyPairInterface::Ptr createKeyPair(bcos::crypto::SecretPtr) override
    {
        return nullptr;
    }
};

}  // namespace test
}  // namespace bcos
