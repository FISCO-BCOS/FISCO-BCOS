/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file Account.cpp
 */
#include "Account.h"
#include "bcos-ops/OpsError.h"
#include <bcos-cpp-sdk/utilities/crypto/KeyPairBuilder.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <fcntl.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>

namespace bcos::ops
{
bytes loadPemPrivateKey(std::string const& _path)
{
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_file(_path.c_str(), "r"), BIO_free);
    if (!bio)
    {
        throw OpsError(c_exitUsage, "cannot open account pem: " + _path);
    }
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(
        PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!pkey)
    {
        throw OpsError(c_exitUsage, "not an unencrypted private key pem: " + _path);
    }
    std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> ec(
        EVP_PKEY_get1_EC_KEY(pkey.get()), EC_KEY_free);
    BIGNUM const* priv = ec ? EC_KEY_get0_private_key(ec.get()) : nullptr;
    if (priv == nullptr)
    {
        throw OpsError(c_exitUsage, "pem does not hold an EC private key: " + _path);
    }
    bytes key(32, 0);
    if (BN_bn2binpad(priv, key.data(), static_cast<int>(key.size())) != 32)
    {
        throw OpsError(c_exitUsage, "unexpected private key size in " + _path);
    }
    return key;
}

bytes loadPrivateKey(std::string const& _path)
{
    std::ifstream in(_path);
    if (!in)
    {
        throw OpsError(c_exitUsage, "cannot open account file: " + _path);
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    auto text = buffer.str();
    auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        throw OpsError(c_exitUsage, "account file is empty: " + _path);
    }
    text = text.substr(first);
    if (text.starts_with("-----BEGIN"))
    {
        return loadPemPrivateKey(_path);
    }
    auto last = text.find_last_not_of(" \t\r\n");
    text = text.substr(0, last + 1);
    if (text.starts_with("0x") || text.starts_with("0X"))
    {
        text = text.substr(2);
    }
    bool hex = text.size() == 64 && std::all_of(text.begin(), text.end(),
                                        [](unsigned char c) { return std::isxdigit(c) != 0; });
    if (!hex)
    {
        throw OpsError(c_exitUsage,
            "account file is neither a pem nor a 64-hex-character private key: " + _path);
    }
    return fromHex(text);
}

bcos::crypto::KeyPairInterface::UniquePtr makeAccount(
    bool _sm, std::optional<std::string> const& _keyPath)
{
    bcos::cppsdk::utilities::KeyPairBuilder builder;
    auto type = _sm ? bcos::crypto::KeyPairType::SM2 : bcos::crypto::KeyPairType::Secp256K1;
    if (!_keyPath || _keyPath->empty())
    {
        return builder.genKeyPair(type);
    }
    auto key = loadPrivateKey(*_keyPath);
    return builder.genKeyPair(type, bytesConstRef(key.data(), key.size()));
}

std::string accountAddress(bool _sm, bcos::crypto::KeyPairInterface& _keyPair)
{
    bcos::crypto::Hash::Ptr hash;
    if (_sm)
    {
        hash = std::make_shared<bcos::crypto::SM3>();
    }
    else
    {
        hash = std::make_shared<bcos::crypto::Keccak256>();
    }
    return _keyPair.address(hash).hexPrefixed();
}

namespace
{
/// _mode 0600 for the private key (a pem readable by every local user is a leaked account),
/// 0644 for the public key
void writePem(std::string const& _path, mode_t _mode, std::function<int(BIO*)> const& _write)
{
    int fd = ::open(_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, _mode);
    if (fd < 0)
    {
        throw OpsError(c_exitUsage, "cannot create " + _path + ": " + std::strerror(errno));
    }
    FILE* file = ::fdopen(fd, "w");
    if (file == nullptr)
    {
        ::close(fd);
        throw OpsError(c_exitUsage, "cannot open " + _path + ": " + std::strerror(errno));
    }
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_fp(file, BIO_CLOSE), BIO_free);
    if (!bio)
    {
        ::fclose(file);
        throw OpsError(c_exitUsage, "cannot write " + _path);
    }
    // PEM_write_* returns 1 once the text sits in the stdio buffer; the write(2) happens in
    // BIO_flush (fclose inside BIO_free drops its return value), so a full disk is only visible
    // here. Never leave a truncated key file behind.
    if (_write(bio.get()) != 1 || BIO_flush(bio.get()) != 1)
    {
        int err = errno;
        bio.reset();
        ::unlink(_path.c_str());
        throw OpsError(c_exitUsage,
            "cannot write " + _path + (err != 0 ? std::string(": ") + std::strerror(err) : ""));
    }
}
}  // namespace

PemKeyPairFiles writePemKeyPair(
    std::string const& _dir, bool _sm, bcos::crypto::KeyPairInterface& _keyPair)
{
    auto const& secret = _keyPair.secretKey()->data();
    std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> ec(
        EC_KEY_new_by_curve_name(_sm ? NID_sm2 : NID_secp256k1), EC_KEY_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> priv(
        BN_bin2bn(secret.data(), static_cast<int>(secret.size()), nullptr), BN_free);
    if (!ec || !priv || EC_KEY_set_private_key(ec.get(), priv.get()) != 1)
    {
        throw OpsError(c_exitUsage, "cannot build the EC key for the pem");
    }
    auto const* group = EC_KEY_get0_group(ec.get());
    std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)> pub(EC_POINT_new(group), EC_POINT_free);
    if (!pub || EC_POINT_mul(group, pub.get(), priv.get(), nullptr, nullptr, nullptr) != 1 ||
        EC_KEY_set_public_key(ec.get(), pub.get()) != 1)
    {
        throw OpsError(c_exitUsage, "cannot derive the public key for the pem");
    }
    // named curve OID (not explicit parameters), the form the console pems carry
    EC_KEY_set_asn1_flag(ec.get(), OPENSSL_EC_NAMED_CURVE);

    std::error_code ec_;
    std::filesystem::create_directories(_dir, ec_);
    if (ec_)
    {
        throw OpsError(c_exitUsage, "cannot create " + _dir + ": " + ec_.message());
    }
    auto address = accountAddress(_sm, _keyPair);
    PemKeyPairFiles files;
    files.privateKeyFile = (std::filesystem::path(_dir) / (address + ".pem")).string();
    files.publicKeyFile = (std::filesystem::path(_dir) / (address + ".public.pem")).string();
    writePem(files.privateKeyFile, 0600, [&](BIO* bio) {
        return PEM_write_bio_ECPrivateKey(bio, ec.get(), nullptr, nullptr, 0, nullptr, nullptr);
    });
    writePem(files.publicKeyFile, 0644,
        [&](BIO* bio) { return PEM_write_bio_EC_PUBKEY(bio, ec.get()); });
    return files;
}
}  // namespace bcos::ops
