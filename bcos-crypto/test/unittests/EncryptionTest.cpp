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
 * @brief test cases for AES/sm4
 * @file SignatureTest.h
 * @date 2021.03.06
 */
#include <bcos-crypto/encrypt/AESCrypto.h>
#include <bcos-crypto/encrypt/Exceptions.h>
#include <bcos-crypto/encrypt/SM4Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(EncryptionTest, TestPromptFixture)

void testEncryption(SymmetricEncryption::Ptr _encrypt)
{
    std::string plainData = "testAESx%$234sdfjl234129p";
    std::string key = "abcd";
    // encrypt
    auto ciperData = _encrypt->symmetricEncrypt((const unsigned char*)plainData.c_str(),
        plainData.size(), (const unsigned char*)key.c_str(), key.size());
    // decrypt
    auto decryptedData = _encrypt->symmetricDecrypt((const unsigned char*)ciperData->data(),
        ciperData->size(), (const unsigned char*)key.c_str(), key.size());
    bytes plainDataBytes(plainData.begin(), plainData.end());
    BOOST_CHECK(*decryptedData == plainDataBytes);

    // invalid key
    std::string invalidKey = "ABCD";
    BOOST_CHECK_THROW(
        _encrypt->symmetricDecrypt((const unsigned char*)ciperData->data(), ciperData->size(),
            (const unsigned char*)invalidKey.c_str(), invalidKey.size()),
        DecryptException);

    // invalid ciper
    (*ciperData)[0] += 10;
    decryptedData = _encrypt->symmetricDecrypt((const unsigned char*)ciperData->data(),
        ciperData->size(), (const unsigned char*)key.c_str(), key.size());
    plainDataBytes = bytes(plainData.begin(), plainData.end());
    BOOST_CHECK(*decryptedData != plainDataBytes);
    // test encrypt/decrypt with given ivData
    std::string ivData = "adfwerivswerwerwerpi9werlwerwasdfa234523423dsfa";
    key = "werwlerewkrjewwwwwwwr4234981034%wer23423&3453453453465646778)7897678";
    plainData += "testAESx%$234sdfjl234129p" + ivData + key;
    ciperData = _encrypt->symmetricEncrypt((const unsigned char*)plainData.c_str(),
        plainData.size(), (const unsigned char*)key.c_str(), key.size(),
        (const unsigned char*)ivData.c_str(), ivData.size());

    decryptedData = _encrypt->symmetricDecrypt((const unsigned char*)ciperData->data(),
        ciperData->size(), (const unsigned char*)key.c_str(), key.size(),
        (const unsigned char*)ivData.c_str(), ivData.size());
    plainDataBytes = bytes(plainData.begin(), plainData.end());
    BOOST_CHECK(*decryptedData == plainDataBytes);

    // invalid ivData
    std::string invalidIVData = "bdfwerivswerwerwerpi9werlwerwasdfa234523423dsf";
    decryptedData = _encrypt->symmetricDecrypt((const unsigned char*)ciperData->data(),
        ciperData->size(), (const unsigned char*)key.c_str(), key.size(),
        (const unsigned char*)invalidIVData.c_str(), invalidIVData.size());
    plainDataBytes = bytes(plainData.begin(), plainData.end());
    BOOST_CHECK(*decryptedData != plainDataBytes);
}
BOOST_AUTO_TEST_CASE(testAES)
{
    auto aes = std::make_shared<AESCrypto>();
    testEncryption(aes);
}

BOOST_AUTO_TEST_CASE(testSM4)
{
    auto sm4 = std::make_shared<SM4Crypto>();
    testEncryption(sm4);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
