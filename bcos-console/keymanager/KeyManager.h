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
 * @brief: console key manager — load/generate/store PEM/P12 keys
 * @file: KeyManager.h
 */

#pragma once

#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-crypto/interfaces/crypto/KeyPairFactory.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::crypto
{
class Secp256k1Crypto;
}

namespace bcos::console
{

// Crypto type for key management.
enum class ConsoleCryptoType
{
    ECDSA,  // secp256k1 + Keccak256
    SM2     // SM2 + SM3
};

// Adapter wrapping Secp256k1Crypto as a KeyPairFactory
class Secp256k1KeyPairFactoryAdapter : public ::bcos::crypto::KeyPairFactory
{
public:
    Secp256k1KeyPairFactoryAdapter();
    ::bcos::crypto::KeyPairInterface::UniquePtr createKeyPair(
        ::bcos::crypto::SecretPtr _secretKey) override;
    ::bcos::crypto::KeyPairInterface::UniquePtr generateKeyPair() override;

private:
    std::shared_ptr<::bcos::crypto::Secp256k1Crypto> m_crypto;
};

// Lightweight account entry for display / switching.
struct AccountEntry
{
    std::string address;     // hex address (without 0x prefix)
    std::string keyFile;     // path to PEM/P12 file
    std::string format;      // "pem" or "p12"
    bool isCurrent = false;  // currently loaded key
};

// Manages the console's signing key.
// Supports PEM and PKCS#12 formats, ECDSA (secp256k1) and SM2.
class KeyManager
{
public:
    using Ptr = std::shared_ptr<KeyManager>;

    KeyManager(::bcos::crypto::KeyFactory::Ptr keyFactory,
        ::bcos::crypto::KeyPairFactory::Ptr keyPairFactory, ::bcos::crypto::Hash::Ptr hashImpl,
        ConsoleCryptoType cryptoType = ConsoleCryptoType::ECDSA);

    // ---- Crypto type ----
    ConsoleCryptoType cryptoType() const { return m_cryptoType; }
    bool isSM2() const { return m_cryptoType == ConsoleCryptoType::SM2; }

    // ---- Key loading ----
    // Load a key from a PEM file. Returns true on success.
    bool loadPemKey(std::string_view filePath);

    // Load a key from a P12 file. Returns true on success.
    bool loadP12Key(std::string_view filePath, std::string_view password);

    // Load key from file (auto-detect format by extension).
    bool loadKey(std::string_view filePath, std::string_view format,
        std::string_view password = {});

    // Generate a new key pair and store as PEM.
    bool newPemKey(std::string_view keyStoreDir);

    // Generate a new key pair and store as P12.
    bool newP12Key(std::string_view keyStoreDir, std::string_view password);

    // ---- Key access ----
    // Current key pair (for signing transactions).
    ::bcos::crypto::KeyPairInterface const* currentKeyPair() const { return m_currentKeyPair.get(); }

    // Current address (hex, without 0x).
    std::string currentAddress() const;

    // ---- Account listing ----
    // Scan a directory for PEM/P12 files and return account entries.
    std::vector<AccountEntry> listAccounts(std::string_view keyStoreDir) const;

private:
    // Derive the public key from the secret key using the KeyPairFactory.
    ::bcos::crypto::KeyPairInterface::UniquePtr deriveKeyPair(::bcos::bytes const& secretData);

    // Compute address from public key.
    std::string pubToAddress(::bcos::crypto::PublicPtr pub) const;

    // Store a key pair as PEM to file.
    bool storePem(::bcos::crypto::KeyPairInterface const& keyPair, std::string_view filePath);

    ::bcos::crypto::KeyFactory::Ptr m_keyFactory;
    ::bcos::crypto::KeyPairFactory::Ptr m_keyPairFactory;
    ::bcos::crypto::Hash::Ptr m_hashImpl;
    ConsoleCryptoType m_cryptoType = ConsoleCryptoType::ECDSA;

    ::bcos::crypto::KeyPairInterface::UniquePtr m_currentKeyPair;
};

}  // namespace bcos::console
