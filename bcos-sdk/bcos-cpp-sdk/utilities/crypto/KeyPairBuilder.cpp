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
 * @file KeyPairBuilder.cpp
 * @author: octopus
 * @date 2022-01-13
 */
#include <bcos-cpp-sdk/utilities/Common.h>
#include <bcos-cpp-sdk/utilities/crypto/KeyPairBuilder.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/fastsm2/FastSM2KeyPairFactory.h>
#include <bcos-crypto/signature/hsmSM2/HsmSM2KeyPair.h>
#include <bcos-crypto/signature/hsmSM2/HsmSM2KeyPairFactory.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FileUtility.h>
#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/ossl_typ.h>
#include <openssl/pem.h>
#include <boost/throw_exception.hpp>
#include <fstream>
#include <memory>
#include <utility>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::utilities;

/**
 * @brief
 *
 * @param _cryptoType
 * @return bcos::crypto::KeyPair::UniquePtr
 */
bcos::crypto::KeyPairInterface::UniquePtr KeyPairBuilder::genKeyPair(
    CryptoType _cryptoType, const std::string _hsmLibPath)
{
    auto fixBytes = FixedBytes<32>().generateRandomFixedBytes();
    return genKeyPair(_cryptoType, bytesConstRef(fixBytes.data(), fixBytes.size()), _hsmLibPath);
}

bcos::crypto::KeyPair::UniquePtr KeyPairBuilder::genKeyPair(
    CryptoType _cryptoType, bytesConstRef _privateKey, const std::string _hsmLibPath)
{
    auto keyImpl = std::make_shared<bcos::crypto::KeyImpl>(_privateKey);
    if (_cryptoType == CryptoType::Secp256K1)
    {
        bcos::crypto::Secp256k1Crypto secp256k1Crypto;
        auto keyPair = secp256k1Crypto.createKeyPair(keyImpl);
        /*
        UTILITIES_KEYPAIR_LOG(TRACE)
            << LOG_BADGE("genKeyPair") << LOG_DESC("generate new ecdsa keypair")
            << LOG_KV("address", keyPair->address(std::make_shared<bcos::crypto::Keccak256>()));
        */

        return keyPair;
    }
    else if (_cryptoType == CryptoType::SM2)
    {
        bcos::crypto::FastSM2KeyPairFactory sM2KeyPairFactory;
        auto keyPair = sM2KeyPairFactory.createKeyPair(keyImpl);
        /*
        UTILITIES_KEYPAIR_LOG(TRACE)
            << LOG_BADGE("genKeyPair") << LOG_DESC("generate new sm keypair")
            << LOG_KV("address", keyPair->address(std::make_shared<bcos::crypto::SM3>()));
        */
        return keyPair;
    }
    else if (_cryptoType == CryptoType::HsmSM2)
    {
        auto hsmKeyPairFactory = std::make_shared<bcos::crypto::HsmSM2KeyPairFactory>(_hsmLibPath);
        UTILITIES_KEYPAIR_LOG(TRACE)
            << LOG_BADGE("genKeyPair") << LOG_DESC("generate new hsm keypair");
        return hsmKeyPairFactory->createKeyPair(keyImpl);
    }
    else
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("genKeyPair don't unsupport algorithm "));
    }
}

bcos::crypto::KeyPairInterface::UniquePtr KeyPairBuilder::useHsmKeyPair(
    unsigned int _keyIndex, std::string _password, const std::string _hsmLibPath)
{
    auto hsmKeyPairFactory = std::make_shared<bcos::crypto::HsmSM2KeyPairFactory>(_hsmLibPath);
    return hsmKeyPairFactory->createKeyPair(_keyIndex, _password);
}