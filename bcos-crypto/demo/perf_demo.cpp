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
 * @brief perf for bcos-crypto
 * @file perf_demo.cpp
 * @date 2021.04.07
 * @author yujiechen
 */

#include <bcos-crypto/interfaces/crypto/hasher/OpenSSLHasher.h>
#include <bcos-crypto/encrypt/AESCrypto.h>
#include <bcos-crypto/encrypt/SM4Crypto.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/hash/Sha256.h>
#include <bcos-crypto/hash/Sha3.h>
#include <bcos-crypto/signature/ed25519/Ed25519Crypto.h>
#include <bcos-crypto/signature/fastsm2/FastSM2Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-utilities/Common.h>
#include <boost/core/ignore_unused.hpp>
#include <span>

using namespace bcos;
using namespace bcos::crypto;

const std::string HASH_CMD = "hash";
const std::string SIGN_CMD = "sign";
const std::string ENCRYPT_CMD = "enc";

void Usage(std::string const& _appName)
{
    std::cout << _appName << " [" << HASH_CMD << "/" << SIGN_CMD << "/" << ENCRYPT_CMD << "] count"
              << std::endl;
}

double getTPS(int64_t _endT, int64_t _startT, size_t _count)
{
    return (1000.0 * (double)_count) / (double)(_endT - _startT);
}

template <class Hash>
std::vector<bcos::h256> hashPerf(
    Hash& _hash, std::string_view _hashName, std::string_view _inputData, size_t _count)
{
    std::vector<bcos::h256> result(_count);

    std::cout << std::endl;
    std::cout << "----------- " << _hashName << " perf start -----------" << std::endl;
    auto startT = utcTime();
    for (size_t i = 0; i < _count; i++)
    {
        result[i] = _hash->hash(bytesConstRef((byte const*)_inputData.data(), _inputData.size()));
    }
    std::cout << "input data size: " << (double)_inputData.size() / 1000.0
              << "KB, loops: " << _count << ", timeCost: " << utcTime() - startT << std::endl;
    std::cout << "TPS of " << _hashName << ": "
              << getTPS(utcTime(), startT, _count) * (double)_inputData.size() / (1024 * 1024)
              << " MB/s" << std::endl;
    std::cout << "----------- " << _hashName << " perf end -----------" << std::endl;
    std::cout << std::endl;

    return result;
}

std::vector<bcos::h256> hashingPerf(
    bcos::crypto::Hasher auto& hasher, std::string_view _inputData, size_t _count)
{
    std::vector<bcos::h256> result(_count);

    std::string hashName = std::string{"New Hashing Test: "} + typeid(hasher).name();
    std::cout << std::endl;
    std::cout << "----------- " << hashName << " perf start -----------" << std::endl;
    auto startT = utcTime();

    for (size_t i = 0; i < _count; i++)
    {
        hasher.update(_inputData);
        std::span<std::byte> view{
            (std::byte*)result[i].data(), (std::span<std::byte>::size_type)result[i].size};

        hasher.final(view);
    }

    std::cout << "input data size: " << (double)_inputData.size() / 1000.0
              << "KB, loops: " << _count << ", timeCost: " << utcTime() - startT << std::endl;
    std::cout << "TPS of " << hashName << ": "
              << getTPS(utcTime(), startT, _count) * (double)_inputData.size() / (1024 * 1024)
              << " MB/s" << std::endl;
    std::cout << "----------- " << hashName << " perf end -----------" << std::endl;
    std::cout << std::endl;

    return result;
}

void stTest(std::string_view inputData, size_t _count)
{
    // keccak256 perf
    // auto hashImpl = std::make_shared<Keccak256>();
    // auto keccak256Old = hashPerf(hashImpl, "Keccak256", inputData, _count);

    // openssl::OPENSSL_Keccak256_Hasher hasherKeccak256;
    // auto keccak256New = hashingPerf(hasherKeccak256, inputData, _count);
    // if (keccak256Old[0] != keccak256New[0])
    // {
    //     std::cout << "Wrong keccak256 hash result! old: " << keccak256Old[0]
    //               << " new: " << keccak256New[0] << std::endl;
    // }

    // sha3 perf
    auto hashImpl2 = std::make_shared<class Sha3>();
    auto sha3Old = hashPerf(hashImpl2, "SHA3", inputData, _count);

    openssl::OpenSSL_SHA3_256_Hasher hasherSHA3;
    auto sha3New = hashingPerf(hasherSHA3, inputData, _count);

    for (size_t i = 0; i < _count; ++i)
    {
        if (sha3Old[i] != sha3New[i])
        {
            std::cout << "Wrong sha3 hash result! old: " << sha3Old[i] << " new: " << sha3New[i]
                      << std::endl;
            break;
        }
    }

    // sm3 perf
    auto hashImpl3 = std::make_shared<SM3>();
    auto sm3Old = hashPerf(hashImpl3, "SM3", inputData, _count);

    openssl::OPENSSL_SM3_Hasher hasherSM3;
    auto sm3New = hashingPerf(hasherSM3, inputData, _count);

    for (size_t i = 0; i < _count; ++i)
    {
        if (sm3Old[i] != sm3New[i])
        {
            std::cout << "Wrong sm3 hash result! old: " << sm3Old[i] << " new: " << sm3New[i]
                      << std::endl;
            break;
        }
    }

    auto hashImpl4 = std::make_shared<Sha256>();
    auto sha256Old = hashPerf(hashImpl4, "SHA256", inputData, _count);

    openssl::OpenSSL_SHA2_256_Hasher hasherSHA2;
    auto sha256New = hashingPerf(hasherSHA2, inputData, _count);

    for (size_t i = 0; i < _count; ++i)
    {
        if (sha256Old[i] != sha256New[i])
        {
            std::cout << "Wrong sha256 hash result! old: " << sha256Old[i]
                      << " new: " << sha256New[i] << std::endl;
            break;
        }
    }
}

void signaturePerf(SignatureCrypto::Ptr _signatureImpl, HashType const& _msgHash,
    std::string const& _signatureName, size_t _count)
{
    std::cout << std::endl;
    std::cout << "----------- " << _signatureName << " perf test start -----------" << std::endl;

    auto keyPair = _signatureImpl->generateKeyPair();
    // sign
    std::shared_ptr<bytes> signedData;
    auto startT = utcTime();
    for (size_t i = 0; i < _count; i++)
    {
        signedData = _signatureImpl->sign(*keyPair, _msgHash, false);
    }
    std::cout << "TPS of " << _signatureName << " sign:" << getTPS(utcTime(), startT, _count)
              << std::endl;

    // verify
    startT = utcTime();
    for (size_t i = 0; i < _count; i++)
    {
        assert(_signatureImpl->verify(keyPair->publicKey(), _msgHash, ref(*signedData)));
    }
    std::cout << "TPS of " << _signatureName << " verify:" << getTPS(utcTime(), startT, _count)
              << std::endl;

    // recover
    signedData = _signatureImpl->sign(*keyPair, _msgHash, true);
    startT = utcTime();
    for (size_t i = 0; i < _count; i++)
    {
        _signatureImpl->recover(_msgHash, ref(*signedData));
    }
    std::cout << "TPS of " << _signatureName << " recover:" << getTPS(utcTime(), startT, _count)
              << std::endl;
    std::cout << "----------- " << _signatureName << " perf test end -----------" << std::endl;
    std::cout << std::endl;
}

void derivePublicKeyPerf(SignatureCrypto::Ptr _signatureImpl, std::string const& _signatureName,
    const KeyPairInterface& _keyPair, size_t _count)
{
    std::cout << std::endl;
    std::cout << "----------- " << _signatureName << " derivePublicKeyPerf test start -----------"
              << std::endl;
    auto startT = utcTime();
    KeyPairInterface::Ptr keyPair;
    for (size_t i = 0; i < _count; i++)
    {
        keyPair = _signatureImpl->createKeyPair(_keyPair.secretKey());
        assert(keyPair->secretKey()->data() == _keyPair.secretKey()->data());
        assert(keyPair->publicKey()->data() == _keyPair.publicKey()->data());
    }
    std::cout << "TPS of " << _signatureName
              << " derivePublicKeyPerf:" << getTPS(utcTime(), startT, _count) << std::endl;
    std::cout << "----------- " << _signatureName << " derivePublicKeyPerf test end -----------"
              << std::endl;
}

void derivePublicKeyPerf(size_t _count)
{
    SignatureCrypto::Ptr signatureImpl = nullptr;
    signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto keyPair = signatureImpl->generateKeyPair();
    derivePublicKeyPerf(signatureImpl, "secp256k1", *keyPair, _count);

    signatureImpl = std::make_shared<SM2Crypto>();
    keyPair = signatureImpl->generateKeyPair();
    derivePublicKeyPerf(signatureImpl, "SM2", *keyPair, _count);

#if SM2_OPTIMIZE
    signatureImpl = std::make_shared<FastSM2Crypto>();
    keyPair = signatureImpl->generateKeyPair();
    derivePublicKeyPerf(signatureImpl, "FastSM2", *keyPair, _count);
#endif

    signatureImpl = std::make_shared<Ed25519Crypto>();
    keyPair = signatureImpl->generateKeyPair();
    derivePublicKeyPerf(signatureImpl, "Ed25519Crypto", *keyPair, _count);
}
void signaturePerf(size_t _count)
{
    std::string inputData = "signature perf test";
    auto msgHash = keccak256Hash(bytesConstRef((byte const*)inputData.c_str(), inputData.size()));
    // secp256k1 perf
    SignatureCrypto::Ptr signatureImpl = std::make_shared<Secp256k1Crypto>();
    signaturePerf(signatureImpl, msgHash, "secp256k1", _count);

    // sm2 perf
    signatureImpl = std::make_shared<SM2Crypto>();
    signaturePerf(signatureImpl, msgHash, "SM2", _count);

#if SM2_OPTIMIZE
    // fastsm2 perf
    signatureImpl = std::make_shared<FastSM2Crypto>();
    signaturePerf(signatureImpl, msgHash, "FastSM2", _count);
#endif

    // ed25519 perf
    signatureImpl = std::make_shared<Ed25519Crypto>();
    signaturePerf(signatureImpl, msgHash, "Ed25519", _count);
}

void encryptPerf(SymmetricEncryption::Ptr _encryptor, std::string const& _inputData,
    const std::string& _encryptorName, size_t _count)
{
    std::cout << std::endl;
    std::cout << "----------- " << _encryptorName << " perf test start -----------" << std::endl;
    std::string key = "abcdefgwerelkewrwerw";
    // encrypt
    bytesPointer encryptedData;
    auto startT = utcTime();
    for (size_t i = 0; i < _count; i++)
    {
        encryptedData = _encryptor->symmetricEncrypt((const unsigned char*)_inputData.c_str(),
            _inputData.size(), (const unsigned char*)key.c_str(), key.size());
    }
    std::cout << "PlainData size:" << (double)_inputData.size() / 1000.0 << " KB, loops: " << _count
              << ", timeCost: " << utcTime() - startT << " ms" << std::endl;
    std::cout << "TPS of " << _encryptorName << " encrypt:"
              << (getTPS(utcTime(), startT, _count) * (double)(_inputData.size())) / 1000.0
              << "KB/s" << std::endl;
    std::cout << std::endl;
    // decrypt
    startT = utcTime();
    bytesPointer decryptedData;
    for (size_t i = 0; i < _count; i++)
    {
        decryptedData = _encryptor->symmetricDecrypt((const unsigned char*)encryptedData->data(),
            encryptedData->size(), (const unsigned char*)key.c_str(), key.size());
    }
    std::cout << "CiperData size:" << (double)encryptedData->size() / 1000.0
              << " KB, loops: " << _count << ", timeCost:" << utcTime() - startT << " ms"
              << std::endl;
    std::cout << "TPS of " << _encryptorName << " decrypt:"
              << (getTPS(utcTime(), startT, _count) * (double)_inputData.size()) / 1000.0 << "KB/s"
              << std::endl;
    bytes plainBytes(_inputData.begin(), _inputData.end());
    assert(plainBytes == *decryptedData);

    std::cout << "----------- " << _encryptorName << " perf test end -----------" << std::endl;
    std::cout << std::endl;
}

void encryptPerf(size_t _count)
{
    std::string inputData = "w3rwerk2-304swlerkjewlrjoiur4kslfjsd,fmnsdlfjlwerlwerjw;erwe;rewrew";
    std::string deltaData = inputData;
    for (int i = 0; i < 100; i++)
    {
        inputData += deltaData;
    }
    // AES
    SymmetricEncryption::Ptr encryptor = std::make_shared<AESCrypto>();
    encryptPerf(encryptor, inputData, "AES", _count);
    // SM4
    encryptor = std::make_shared<SM4Crypto>();
    encryptPerf(encryptor, inputData, "SM4", _count);
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        Usage(argv[0]);
        return -1;
    }
    auto cmd = argv[1];
    size_t count = atoi(argv[2]);

    std::string data;
    data.reserve(16 * 1024);
    for (size_t i = 0; i < 16 * 1024; ++i)
    {
        char c = i;
        data.push_back(c);
    }


    if (HASH_CMD == cmd)
    {
        stTest(data, count);
    }
    else if (SIGN_CMD == cmd)
    {
        signaturePerf(count);
        derivePublicKeyPerf(count);
    }
    else if (ENCRYPT_CMD == cmd)
    {
        encryptPerf(count);
    }
    else
    {
        std::cout << "Invalid subcommand \"" << cmd << "\"" << std::endl;
        Usage(argv[0]);
    }
    return 0;
}
