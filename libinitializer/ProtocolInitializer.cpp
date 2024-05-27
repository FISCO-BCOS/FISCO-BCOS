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
 * @brief initializer for the protocol module
 * @file ProtocolInitializer.cpp
 * @author: yujiechen
 * @date 2021-06-10
 */
#include "libinitializer/ProtocolInitializer.h"
#include "libinitializer/Common.h"
#include <bcos-crypto/encrypt/AESCrypto.h>
#include <bcos-crypto/encrypt/HsmSM4Crypto.h>
#include <bcos-crypto/encrypt/SM4Crypto.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/fastsm2/FastSM2Crypto.h>
#include <bcos-crypto/signature/hsmSM2/HsmSM2Crypto.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-security/bcos-security/DataEncryption.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionSubmitResultFactoryImpl.h>

using namespace bcos;
using namespace bcostars::protocol;
using namespace bcos::initializer;
using namespace bcos::crypto;
using namespace bcos::tool;
using namespace bcos::security;

ProtocolInitializer::ProtocolInitializer()
  : m_keyFactory(std::make_shared<bcos::crypto::KeyFactoryImpl>())
{}
void ProtocolInitializer::init(NodeConfig::Ptr _nodeConfig)
{
    m_enableHsm = _nodeConfig->enableHsm();
    // TODO: ed25519
    if (_nodeConfig->smCryptoType())
    {
        if (m_enableHsm)
        {
            m_hsmLibPath = _nodeConfig->hsmLibPath();
            m_keyIndex = _nodeConfig->keyIndex();
            m_password = _nodeConfig->password();
            createHsmSMCryptoSuite();
            INITIALIZER_LOG(INFO) << LOG_DESC("begin init hsm sm crypto suite");
        }
        else
        {
            createSMCryptoSuite();
            INITIALIZER_LOG(INFO) << LOG_DESC("begin init sm crypto suite");
        }
    }
    else
    {
        createCryptoSuite();
    }
    INITIALIZER_LOG(INFO) << LOG_DESC("init crypto suite success");

    if (true == _nodeConfig->storageSecurityEnable())
    {
        // Notice: the reason we don't use HSM for storage security is that the encrypt function in
        // HSM only support data length from 0 to 65536 byte

        // // storage security with HSM
        // if (_nodeConfig->enableHsm())
        // {
        //     INITIALIZER_LOG(DEBUG)
        //         << LOG_DESC("storage_security.enable = true, storage security with HSM");
        //     m_dataEncryption = std::make_shared<HsmDataEncryption>(_nodeConfig);
        // }
        // else
        // {
        m_dataEncryption = std::make_shared<DataEncryption>(_nodeConfig);
        // }

        INITIALIZER_LOG(INFO) << LOG_DESC(
            "storage_security.enable = true, init data encryption success");
    }

    // create the block factory
    // TODO: pb/tars option
    auto blockHeaderFactory = std::make_shared<BlockHeaderFactoryImpl>(m_cryptoSuite);
    auto transactionFactory = std::make_shared<TransactionFactoryImpl>(m_cryptoSuite);
    auto receiptFactory = std::make_shared<TransactionReceiptFactoryImpl>(m_cryptoSuite);
    m_blockFactory = std::make_shared<BlockFactoryImpl>(
        m_cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);

    m_cryptoSuite->setKeyFactory(m_keyFactory);
    auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();
    m_txResultFactory = txResultFactory;

    INITIALIZER_LOG(INFO) << LOG_DESC("init blockFactory success");
}

void ProtocolInitializer::createCryptoSuite()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto encryptImpl = std::make_shared<AESCrypto>();
    m_cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, encryptImpl);
}

void ProtocolInitializer::createSMCryptoSuite()
{
    auto hashImpl = std::make_shared<SM3>();
    auto signatureImpl = std::make_shared<FastSM2Crypto>();
    auto encryptImpl = std::make_shared<SM4Crypto>();
    m_cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, encryptImpl);
}

void ProtocolInitializer::createHsmSMCryptoSuite()
{
    auto hashImpl = std::make_shared<SM3>();
    auto signatureImpl = std::make_shared<HsmSM2Crypto>(m_hsmLibPath);
    auto encryptImpl = std::make_shared<HsmSM4Crypto>(m_hsmLibPath);
    m_cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, encryptImpl);
}

void ProtocolInitializer::loadKeyPair(std::string const& _privateKeyPath)
{
    if (m_enableHsm)
    {
        // Create key pair according to the key index which inside HSM(Hardware Secure Machine)
        m_keyPair = dynamic_pointer_cast<bcos::crypto::HsmSM2Crypto>(m_cryptoSuite->signatureImpl())
                        ->createKeyPair(m_keyIndex, m_password);
        INITIALIZER_LOG(INFO) << METRIC << LOG_DESC("loadKeyPair from HSM")
                              << LOG_KV("lib_path", m_hsmLibPath) << LOG_KV("keyIndex", m_keyIndex)
                              << LOG_KV("HSM password", m_password);
    }
    else
    {
        auto privateKeyData =
            loadPrivateKey(_privateKeyPath, c_hexedPrivateKeySize, m_dataEncryption);
        if (!privateKeyData)
        {
            INITIALIZER_LOG(INFO) << LOG_DESC("loadKeyPair failed")
                                  << LOG_KV("privateKeyPath", _privateKeyPath);
            throw std::runtime_error("loadKeyPair failed, keyPair path: " + _privateKeyPath);
        }
        INITIALIZER_LOG(INFO) << LOG_DESC("loadKeyPair from privateKey")
                              << LOG_KV("privateKeySize", privateKeyData->size())
                              << LOG_KV("enableStorageSecurity", m_dataEncryption ? true : false);
        auto privateKey = m_keyFactory->createKey(*privateKeyData);
        m_keyPair = m_cryptoSuite->signatureImpl()->createKeyPair(privateKey);
        INITIALIZER_LOG(INFO) << METRIC << LOG_DESC("loadKeyPair from privateKeyPath")
                              << LOG_KV("privateKeyPath", _privateKeyPath);
    }

    INITIALIZER_LOG(INFO) << METRIC << LOG_DESC("loadKeyPair success")
                          << LOG_KV("publicKey", m_keyPair->publicKey()->hex());
}