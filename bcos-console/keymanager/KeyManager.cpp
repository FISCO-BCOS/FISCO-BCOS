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
 * @brief: key manager implementation
 * @file: KeyManager.cpp
 */

#include "KeyManager.h"
#include <bcos-crypto/ChecksumAddress.h>
#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-utilities/Base64.h>
#include <boost/algorithm/hex.hpp>
#include <boost/filesystem.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/err.h>

namespace fs = boost::filesystem;
using namespace bcos::console;

KeyManager::KeyManager(bcos::crypto::KeyFactory::Ptr keyFactory,
    bcos::crypto::KeyPairFactory::Ptr keyPairFactory, bcos::crypto::Hash::Ptr hashImpl,
    ConsoleCryptoType cryptoType)
  : m_keyFactory(std::move(keyFactory)),
    m_keyPairFactory(std::move(keyPairFactory)),
    m_hashImpl(std::move(hashImpl)),
    m_cryptoType(cryptoType)
{}

bcos::crypto::KeyPairInterface::UniquePtr KeyManager::deriveKeyPair(
    bcos::bytes const& secretData)
{
    auto secret = m_keyFactory->createKey(
        bcos::bytesConstRef(reinterpret_cast<const bcos::byte*>(secretData.data()), secretData.size()));
    return m_keyPairFactory->createKeyPair(secret);
}

std::string KeyManager::pubToAddress(bcos::crypto::PublicPtr pub) const
{
    auto hash = m_hashImpl->hash(
        bcos::bytesConstRef(reinterpret_cast<const bcos::byte*>(pub->constData()), pub->size()));
    return hash.hex();
}

std::string KeyManager::currentAddress() const
{
    if (!m_currentKeyPair)
        return {};
    return pubToAddress(m_currentKeyPair->publicKey());
}

// ---- PEM loading ----

bool KeyManager::loadPemKey(std::string_view filePath)
{
    std::ifstream f{std::string(filePath)};
    if (!f)
    {
        std::cerr << "Cannot open key file: " << filePath << '\n';
        return false;
    }

    std::string line;
    std::stringstream b64Buf;
    bool inBody = false;

    while (std::getline(f, line))
    {
        // Trim trailing \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.starts_with("-----BEGIN"))
        {
            inBody = true;
            continue;
        }
        if (line.starts_with("-----END"))
        {
            break;
        }
        if (inBody)
        {
            b64Buf << line;
        }
    }

    auto b64Str = b64Buf.str();
    if (b64Str.empty())
    {
        std::cerr << "Empty or invalid PEM key: " << filePath << '\n';
        return false;
    }

    // Decode base64 to raw bytes
    try
    {
        auto rawBytes = bcos::base64DecodeBytes(b64Str);
        if (rawBytes.empty())
        {
            std::cerr << "Base64 decode failed for: " << filePath << '\n';
            return false;
        }

        auto keyPair = deriveKeyPair(rawBytes);
        if (!keyPair)
        {
            std::cerr << "Failed to derive key pair from: " << filePath << '\n';
            return false;
        }

        m_currentKeyPair = std::move(keyPair);
        std::cout << "Loaded account: " << currentAddress() << '\n';
        return true;
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error loading PEM key: " << e.what() << '\n';
        return false;
    }
}

// ---- P12 loading via OpenSSL PKCS12 ----

bool KeyManager::loadP12Key(std::string_view filePath, std::string_view password)
{
    std::ifstream f(std::string(filePath), std::ios::binary);
    if (!f)
    {
        std::cerr << "Cannot open P12 file: " << filePath << '\n';
        return false;
    }
    std::vector<unsigned char> p12Data(
        (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    BIO* bio = BIO_new_mem_buf(p12Data.data(), static_cast<int>(p12Data.size()));
    if (!bio)
    {
        std::cerr << "Failed to create BIO for P12 data\n";
        return false;
    }

    const char* pass = password.empty() ? nullptr : password.data();
    auto* p12 = d2i_PKCS12_bio(bio, nullptr);
    BIO_free(bio);
    if (!p12)
    {
        std::cerr << "Failed to parse P12 (wrong password or corrupt)\n";
        return false;
    }

    EVP_PKEY* pkey = nullptr;
    X509* cert = nullptr;
    STACK_OF(X509)* ca = nullptr;
    int ret = PKCS12_parse(p12, pass, &pkey, &cert, &ca);
    PKCS12_free(p12);
    if (!ret || !pkey)
    {
        std::cerr << "Failed to extract key from P12\n";
        if (cert) X509_free(cert);
        if (ca) sk_X509_pop_free(ca, X509_free);
        return false;
    }

    // Get raw EC private key
    auto* ecKey = EVP_PKEY_get1_EC_KEY(pkey);
    if (!ecKey)
    {
        std::cerr << "P12 file does not contain an EC key\n";
        EVP_PKEY_free(pkey);
        if (cert) X509_free(cert);
        if (ca) sk_X509_pop_free(ca, X509_free);
        return false;
    }

    auto const* ecBn = EC_KEY_get0_private_key(ecKey);
    if (!ecBn)
    {
        std::cerr << "No private key in P12 EC key\n";
        EC_KEY_free(ecKey);
        EVP_PKEY_free(pkey);
        if (cert) X509_free(cert);
        if (ca) sk_X509_pop_free(ca, X509_free);
        return false;
    }

    int bnBytes = BN_num_bytes(ecBn);
    bcos::bytes secretData(bnBytes);
    BN_bn2bin(ecBn, secretData.data());

    EC_KEY_free(ecKey);
    EVP_PKEY_free(pkey);
    if (cert) X509_free(cert);
    if (ca) sk_X509_pop_free(ca, X509_free);

    auto keyPair = deriveKeyPair(secretData);
    if (!keyPair)
    {
        std::cerr << "Failed to derive key pair from P12\n";
        return false;
    }
    m_currentKeyPair = std::move(keyPair);
    std::cout << "Loaded account (P12): " << currentAddress() << '\n';
    return true;
}

bool KeyManager::loadKey(
    std::string_view filePath, std::string_view format, std::string_view password)
{
    if (format == "p12")
    {
        return loadP12Key(filePath, password);
    }
    // Default to PEM
    return loadPemKey(filePath);
}

// ---- Key generation ----

bool KeyManager::newPemKey(std::string_view keyStoreDir)
{
    try
    {
        // Generate a new random key pair
        auto keyPair = m_keyPairFactory->generateKeyPair();
        auto addr = pubToAddress(keyPair->publicKey());

        // Build file path: keyStoreDir/addr.pem
        fs::path dir(keyStoreDir);
        if (!fs::exists(dir))
        {
            fs::create_directories(dir);
        }
        auto filePath = dir / (addr + ".pem");

        if (!storePem(*keyPair, filePath.string()))
        {
            return false;
        }

        m_currentKeyPair = std::move(keyPair);
        std::cout << "New account created: " << addr << '\n';
        std::cout << "Account file: " << filePath.string() << '\n';
        return true;
    }
    catch (std::exception const& e)
    {
        std::cerr << "Failed to generate new account: " << e.what() << '\n';
        return false;
    }
}

bool KeyManager::newP12Key(std::string_view keyStoreDir, std::string_view password)
{
    auto keyPair = m_keyPairFactory->generateKeyPair();
    auto addr = pubToAddress(keyPair->publicKey());
    auto secret = keyPair->secretKey();
    bcos::bytes secretData(secret->constData(), secret->constData() + secret->size());

    // Create EC_KEY from raw bytes
    BIGNUM* bn = BN_bin2bn(secretData.data(), static_cast<int>(secretData.size()), nullptr);
    if (!bn) { std::cerr << "Failed BIGNUM\n"; return false; }

    int nid = isSM2() ? NID_sm2 : NID_secp256k1;
    EC_KEY* ecKey = EC_KEY_new_by_curve_name(nid);
    if (!ecKey) { BN_free(bn); return false; }
    EC_KEY_set_private_key(ecKey, bn);

    EVP_PKEY* pkey = EVP_PKEY_new();
    EVP_PKEY_set1_EC_KEY(pkey, ecKey);

    // Self-signed X509 cert
    X509* cert = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 365 * 24 * 3600);
    X509_set_pubkey(cert, pkey);
    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        (unsigned char*)addr.c_str(), -1, -1, 0);
    X509_set_issuer_name(cert, name);
    X509_sign(cert, pkey, EVP_sha256());

    PKCS12* p12 = PKCS12_create(
        password.empty() ? nullptr : password.data(), "account", pkey, cert,
        nullptr, 0, 0, 0, 0, 0);

    if (!p12)
    {
        std::cerr << "Failed PKCS12_create\n";
        EVP_PKEY_free(pkey); X509_free(cert); EC_KEY_free(ecKey); BN_free(bn);
        return false;
    }

    fs::path dir(keyStoreDir);
    if (!fs::exists(dir)) fs::create_directories(dir);
    auto filePath = dir / (addr + ".p12");

    FILE* out = fopen(filePath.string().c_str(), "wb");
    if (!out)
    {
        std::cerr << "Cannot write: " << filePath.string() << '\n';
        PKCS12_free(p12); EVP_PKEY_free(pkey); X509_free(cert); EC_KEY_free(ecKey); BN_free(bn);
        return false;
    }
    i2d_PKCS12_fp(out, p12);
    fclose(out);

    PKCS12_free(p12); EVP_PKEY_free(pkey); X509_free(cert); EC_KEY_free(ecKey); BN_free(bn);

    m_currentKeyPair = std::move(keyPair);
    std::cout << "New P12 account: " << addr << '\n'
              << "File: " << filePath.string() << '\n';
    return true;
}

bool KeyManager::storePem(
    bcos::crypto::KeyPairInterface const& keyPair, std::string_view filePath)
{
    try
    {
        auto secret = keyPair.secretKey();
        auto secretBytes = bcos::bytes(reinterpret_cast<const bcos::byte*>(secret->constData()),
            reinterpret_cast<const bcos::byte*>(secret->constData()) + secret->size());
        auto b64Str = bcos::base64Encode(bcos::bytesConstRef(secretBytes.data(), secretBytes.size()));

        std::ofstream out{std::string(filePath)};
        if (!out)
        {
            std::cerr << "Cannot write key file: " << filePath << '\n';
            return false;
        }

        out << "-----BEGIN PRIVATE KEY-----\n";
        // Wrap base64 at 64 chars
        for (size_t i = 0; i < b64Str.size(); i += 64)
        {
            out << b64Str.substr(i, 64) << '\n';
        }
        out << "-----END PRIVATE KEY-----\n";
        return true;
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error writing PEM key: " << e.what() << '\n';
        return false;
    }
}

// ---- Account listing ----

std::vector<AccountEntry> KeyManager::listAccounts(std::string_view keyStoreDir) const
{
    std::vector<AccountEntry> entries;
    fs::path dir(keyStoreDir);
    if (!fs::exists(dir) || !fs::is_directory(dir))
    {
        return entries;
    }

    auto currentAddr = m_currentKeyPair ? currentAddress() : std::string();

    for (auto& entry : fs::directory_iterator(dir))
    {
        if (!fs::is_regular_file(entry.path()))
            continue;

        auto ext = entry.path().extension().string();
        if (ext != ".pem" && ext != ".p12" && ext != ".pub")
            continue;

        auto stem = entry.path().stem().string();
        AccountEntry acc;
        acc.keyFile = entry.path().string();
        acc.format = (ext == ".p12") ? "p12" : "pem";
        // Address is the filename stem (0x-prefixed)
        acc.address = stem;
        if (acc.address.starts_with("0x") || acc.address.starts_with("0X"))
        {
            acc.address = acc.address.substr(2);
        }
        acc.isCurrent = !currentAddr.empty() && acc.address == currentAddr;
        entries.push_back(std::move(acc));
    }

    return entries;
}
