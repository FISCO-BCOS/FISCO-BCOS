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
#include "../encrypt/AESCrypto.h"
#include "../encrypt/SM4Crypto.h"
#include "../hash/Keccak256.h"
#include "../hash/SM3.h"
#include "../hash/Sha3.h"
#include "../signature/ed25519/Ed25519Crypto.h"
#include "../signature/secp256k1/Secp256k1Crypto.h"
#include "../signature/sm2/SM2Crypto.h"
#include <bcos-framework/libutilities/Common.h>

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

void hashPerf(
    Hash::Ptr _hash, std::string const& _hashName, std::string const& _inputData, size_t _count)
{
    std::cout << std::endl;
    std::cout << "----------- " << _hashName << " perf start -----------" << std::endl;
    auto startT = utcTime();
    for (size_t i = 0; i < _count; i++)
    {
        _hash->hash(bytesConstRef((byte const*)_inputData.c_str(), _inputData.size()));
    }
    std::cout << "input data size: " << (double)_inputData.size() / 1000.0
              << "KB, loops: " << _count << ", timeCost: " << utcTime() - startT << std::endl;
    std::cout << "TPS of " << _hashName << ": "
              << getTPS(utcTime(), startT, _count) * (double)_inputData.size() / 1000.0 << " KB/s"
              << std::endl;
    std::cout << "----------- " << _hashName << " perf end -----------" << std::endl;
    std::cout << std::endl;
}

void hashPerf(size_t _count)
{
    std::string inputData = "abcdwer234q4@#2424wdf";
    std::string deltaData = inputData;
    for (int i = 0; i < 50; i++)
    {
        inputData += deltaData;
    }
    // sha3 perf
    Hash::Ptr hashImpl = std::make_shared<class Sha3>();
    hashPerf(hashImpl, "SHA3", inputData, _count);
    // keccak256 perf
    hashImpl = std::make_shared<Keccak256>();
    hashPerf(hashImpl, "Keccak256", inputData, _count);
    // sm3 perf
    hashImpl = std::make_shared<SM3>();
    hashPerf(hashImpl, "SM3", inputData, _count);
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
        signedData = _signatureImpl->sign(keyPair, _msgHash, false);
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
    signedData = _signatureImpl->sign(keyPair, _msgHash, true);
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
    if (HASH_CMD == cmd)
    {
        hashPerf(count);
    }
    else if (SIGN_CMD == cmd)
    {
        signaturePerf(count);
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
