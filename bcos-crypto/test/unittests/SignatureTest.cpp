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
 * @brief test cases for secp256k1/sm2/ed25519
 * @file SignatureTest.h
 * @date 2021.03.06
 */
#include "../../hash/Keccak256.h"
#include "../../hash/SM3.h"
#include "../../hash/Sha3.h"
#include "../../signature/Exceptions.h"
#include "../../signature/codec/SignatureDataWithPub.h"
#include "../../signature/codec/SignatureDataWithV.h"
#include "../../signature/ed25519/Ed25519Crypto.h"
#include "../../signature/ed25519/Ed25519KeyPair.h"
#include "../../signature/key/KeyFactoryImpl.h"
#include "../../signature/secp256k1/Secp256k1Crypto.h"
#include "../../signature/secp256k1/Secp256k1KeyPair.h"
#include "../../signature/sm2/SM2Crypto.h"
#include "../../signature/sm2/SM2KeyPair.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <string>
using namespace bcos;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(SignatureTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testSecp256k1KeyPair)
{
    // BOOST_CHECK(Secret::size == 32);
    // BOOST_CHECK(Public::size == 64);
    // check secret->public
    auto fixedSec1 = h256(
        "bcec428d5205abe0f0cc8a7340839"
        "08d9eb8563e31f943d760786edf42ad67dd");
    auto sec1 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec1.asBytes());
    auto pub1 = secp256k1PriToPub(sec1);
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto keyPair1 = signatureImpl->createKeyPair(sec1);
    BOOST_CHECK(pub1->data() == keyPair1->publicKey()->data());
    BOOST_CHECK(sec1->data() == keyPair1->secretKey()->data());

    auto fixedSec2 = h256("bcec428d5205abe0");
    auto sec2 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec2.asBytes());
    auto pub2 = secp256k1PriToPub(sec2);
    auto keyPair2 = signatureImpl->createKeyPair(sec2);
    BOOST_CHECK(pub2->data() == keyPair2->publicKey()->data());
    BOOST_CHECK(sec2->data() == keyPair2->secretKey()->data());
    BOOST_CHECK(pub1->data() != pub2->data());

    // check address
    auto hashImpl = std::make_shared<Keccak256>();
    auto secp256k1KeyPair1 = std::make_shared<Secp256k1KeyPair>(sec1);
    auto secp256k1KeyPair2 = std::make_shared<Secp256k1KeyPair>(sec2);

    Address address1 = secp256k1KeyPair1->address(hashImpl);
    Address address2 = secp256k1KeyPair2->address(hashImpl);
    BOOST_CHECK(address1 != address2);

    // test calculateAddress
    BOOST_CHECK(secp256k1KeyPair1->address(hashImpl) == calculateAddress(hashImpl, pub1));
    BOOST_CHECK(secp256k1KeyPair2->address(hashImpl) == calculateAddress(hashImpl, pub2));
    // test privateKeyToPublicKey
    auto derivedPub1 = secp256k1KeyPair1->priToPub(secp256k1KeyPair1->secretKey());
    auto derivedPub2 = secp256k1KeyPair2->priToPub(secp256k1KeyPair2->secretKey());
    BOOST_CHECK(derivedPub1->data() == pub1->data());
    BOOST_CHECK(derivedPub2->data() == pub2->data());

    // create KeyPair
    auto keyPair = secp256k1GenerateKeyPair();
    BOOST_CHECK(keyPair->secretKey());
    BOOST_CHECK(keyPair->publicKey());
    auto testPub = secp256k1PriToPub(keyPair->secretKey());
    BOOST_CHECK(keyPair->publicKey()->data() == testPub->data());
    SecretPtr emptySecret = std::make_shared<KeyImpl>(SECP256K1_PRIVATE_LEN);
    BOOST_CHECK_THROW(Secp256k1KeyPair emptyKeyPair(emptySecret), PriToPublicKeyException);
}

BOOST_AUTO_TEST_CASE(testSecp256k1SignAndVerify)
{
    auto keyPair = secp256k1GenerateKeyPair();
    auto hashData = keccak256Hash((std::string)("abcd"));
    std::cout << "### hashData:" << *toHexString(hashData) << std::endl;
    std::cout << "#### publicKey:" << keyPair->publicKey()->hex() << std::endl;
    std::cout << "#### publicKey shortHex:" << keyPair->publicKey()->shortHex() << std::endl;
    /// normal check
    // sign
    auto signData = secp256k1Sign(keyPair, hashData);
    std::cout << "### signData:" << *toHexString(*signData) << std::endl;
    // verify
    bool result = secp256k1Verify(
        keyPair->publicKey(), hashData, bytesConstRef(signData->data(), signData->size()));
    BOOST_CHECK(result == true);
    std::cout << "### verify result:" << result << std::endl;

    // recover
    auto pub = secp256k1Recover(hashData, bytesConstRef(signData->data(), signData->size()));
    std::cout << "### secp256k1Recover begin, publicKey:"
              << *toHexString(keyPair->publicKey()->data()) << std::endl;
    std::cout << "#### recoverd publicKey:" << *toHexString(pub->data()) << std::endl;
    BOOST_CHECK(pub->data() == keyPair->publicKey()->data());
    /// exception check:
    // check1: invalid payload(hash)
    h256 invalidHash = keccak256Hash((std::string)("abce"));
    result = secp256k1Verify(
        keyPair->publicKey(), invalidHash, bytesConstRef(signData->data(), signData->size()));
    BOOST_CHECK(result == false);

    PublicPtr invalidPub = std::make_shared<KeyImpl>(SECP256K1_PUBLIC_LEN);
    invalidPub = secp256k1Recover(invalidHash, bytesConstRef(signData->data(), signData->size()));
    BOOST_CHECK(invalidPub->data() != keyPair->publicKey()->data());

    // check2: invalid sig
    auto anotherSig(secp256k1Sign(keyPair, invalidHash));
    result = secp256k1Verify(
        keyPair->publicKey(), hashData, bytesConstRef(anotherSig->data(), anotherSig->size()));
    BOOST_CHECK(result == false);

    invalidPub = secp256k1Recover(hashData, bytesConstRef(anotherSig->data(), anotherSig->size()));
    BOOST_CHECK(invalidPub->data() != keyPair->publicKey()->data());

    // check3: invalid keyPair
    auto keyPair2 = secp256k1GenerateKeyPair();
    result = secp256k1Verify(
        keyPair2->publicKey(), hashData, bytesConstRef(signData->data(), signData->size()));
    BOOST_CHECK(result == false);

    h256 r(keccak256Hash(std::string("+++")));
    h256 s(keccak256Hash(std::string("24324")));
    byte v = 4;
    auto signatureData = std::make_shared<SignatureDataWithV>(r, s, v);
    auto secp256k1Crypto = std::make_shared<Secp256k1Crypto>();
    auto encodedData = signatureData->encode();
    BOOST_CHECK_THROW(secp256k1Crypto->recover(hashData, ref(*encodedData)), InvalidSignature);


    // test signatureData encode and decode
    encodedData = signatureData->encode();
    auto signatureData2 = std::make_shared<SignatureDataWithV>(
        bytesConstRef(encodedData->data(), encodedData->size()));
    BOOST_CHECK(signatureData2->r() == signatureData->r());
    BOOST_CHECK(signatureData2->s() == signatureData->s());
    BOOST_CHECK(signatureData2->v() == signatureData->v());

    auto signatureData3 =
        std::make_shared<SignatureDataWithV>(bytesConstRef(signData->data(), signData->size()));
    encodedData = signatureData3->encode();
    BOOST_CHECK(*signData == *encodedData);
    auto publicKey = secp256k1Crypto->recover(hashData, ref(*encodedData));
    BOOST_CHECK(publicKey->data() == keyPair->publicKey()->data());
}

BOOST_AUTO_TEST_CASE(testSM2KeyPair)
{
    // check secret->public
    h256 fixedSec1(
        "bcec428d5205abe0f0cc8a7340839"
        "08d9eb8563e31f943d760786edf42ad67dd");
    auto sec1 = std::make_shared<KeyImpl>(fixedSec1.asBytes());
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    auto secCreated = keyFactory->createKey(fixedSec1.asBytes());
    BOOST_CHECK(sec1->data() == secCreated->data());

    auto pub1 = sm2PriToPub(sec1);
    auto keyPair1 = std::make_shared<SM2KeyPair>(sec1);

    auto signatureImpl = std::make_shared<SM2Crypto>();
    auto keyPairTest = signatureImpl->createKeyPair(sec1);
    BOOST_CHECK(keyPairTest->publicKey()->data() == keyPair1->publicKey()->data());
    BOOST_CHECK(keyPairTest->secretKey()->data() == keyPair1->secretKey()->data());

    h256 fixedSec2("bcec428d5205abe0");
    auto sec2 = std::make_shared<KeyImpl>(fixedSec2.asBytes());
    auto pub2 = sm2PriToPub(sec2);
    auto keyPair2 = std::make_shared<SM2KeyPair>(sec2);

    BOOST_CHECK(pub1->data() != pub2->data());


    // check public to address
    auto hashImpl = std::make_shared<SM3>();
    Address address1 = calculateAddress(hashImpl, pub1);
    Address address2 = calculateAddress(hashImpl, pub2);
    BOOST_CHECK(address1 != address2);

    // check secret to address
    Address addressSec1 = keyPair1->address(hashImpl);
    Address addressSec2 = keyPair2->address(hashImpl);
    BOOST_CHECK(addressSec1 != addressSec2);
    BOOST_CHECK(address1 == addressSec1);
    BOOST_CHECK(address2 == addressSec2);

    // create keyPair
    auto keyPair = sm2GenerateKeyPair();
    BOOST_CHECK(keyPair->publicKey());
    BOOST_CHECK(keyPair->secretKey());
    pub1 = sm2PriToPub(keyPair->secretKey());
    BOOST_CHECK(keyPair->publicKey()->data() == pub1->data());

    // empty case
    auto emptySecret = std::make_shared<KeyImpl>(SM2_PRIVATE_KEY_LEN);
    BOOST_CHECK_THROW(SM2KeyPair sm2KeyPair(emptySecret), PriToPublicKeyException);
}

BOOST_AUTO_TEST_CASE(testSM2SignAndVerify)
{
    auto signatureCrypto = std::make_shared<SM2Crypto>();
    auto hashCrypto = std::make_shared<SM3>();
    auto keyPair = signatureCrypto->generateKeyPair();
    auto hashData = hashCrypto->hash(std::string("abcd"));
    // sign
    auto sig = signatureCrypto->sign(keyPair, hashData, true);
    // verify
    bool result = signatureCrypto->verify(
        keyPair->publicKey(), hashData, bytesConstRef(sig->data(), sig->size()));
    std::cout << "#### phase 1, signatureData:" << *toHexString(*sig) << std::endl;
    BOOST_CHECK(result == true);
    // recover
    auto pub = signatureCrypto->recover(hashData, bytesConstRef(sig->data(), sig->size()));
    std::cout << "#### phase 2" << std::endl;
    BOOST_CHECK(pub->data() == keyPair->publicKey()->data());


    // exception case
    // invalid payload(hash)
    auto invalidHash = hashCrypto->hash(std::string("abce"));
    result = signatureCrypto->verify(
        keyPair->publicKey(), invalidHash, bytesConstRef(sig->data(), sig->size()));
    BOOST_CHECK(result == false);
    // recover
    BOOST_CHECK_THROW(
        signatureCrypto->recover(invalidHash, bytesConstRef(sig->data(), sig->size())),
        InvalidSignature);

    // invalid signature
    auto anotherSig = signatureCrypto->sign(keyPair, invalidHash, true);
    result = signatureCrypto->verify(
        keyPair->publicKey(), hashData, bytesConstRef(anotherSig->data(), anotherSig->size()));
    BOOST_CHECK(result == false);
    BOOST_CHECK_THROW(
        signatureCrypto->recover(hashData, bytesConstRef(anotherSig->data(), anotherSig->size())),
        InvalidSignature);

    // invalid sig
    auto keyPair2 = signatureCrypto->generateKeyPair();
    result = signatureCrypto->verify(
        keyPair2->publicKey(), hashData, bytesConstRef(sig->data(), sig->size()));
    BOOST_CHECK(result == false);
    auto signatureStruct =
        std::make_shared<SignatureDataWithPub>(bytesConstRef(sig->data(), sig->size()));
    auto r = signatureStruct->r();
    auto s = signatureStruct->s();

    auto signatureStruct2 = std::make_shared<SignatureDataWithPub>(r, s, signatureStruct->pub());
    auto encodedData = signatureStruct2->encode();
    auto recoverKey =
        signatureCrypto->recover(hashData, bytesConstRef(encodedData->data(), encodedData->size()));
    BOOST_CHECK(recoverKey->data() == keyPair->publicKey()->data());
}


BOOST_AUTO_TEST_CASE(testED25519SignAndVerify)
{
    auto signatureCrypto = std::make_shared<Ed25519Crypto>();
    auto hashCrypto = std::make_shared<class Sha3>();
    auto keyPair = signatureCrypto->generateKeyPair();
    auto hashData = hashCrypto->hash(std::string("abcd"));
    // sign
    auto sig = signatureCrypto->sign(keyPair, hashData, true);
    // verify
    bool result = signatureCrypto->verify(
        keyPair->publicKey(), hashData, bytesConstRef(sig->data(), sig->size()));
    std::cout << "#### phase 1, signatureData:" << *toHexString(*sig) << std::endl;
    std::cout << "#### keyPair->publicKey():" << *toHexString(keyPair->publicKey()->data())
              << std::endl;
    BOOST_CHECK(result == true);
    // recover
    auto pub = signatureCrypto->recover(hashData, bytesConstRef(sig->data(), sig->size()));
    std::cout << "#### phase 2" << std::endl;
    BOOST_CHECK(pub->data() == keyPair->publicKey()->data());

    // exception case
    // invalid payload(hash)
    auto invalidHash = hashCrypto->hash(std::string("abce"));
    result = signatureCrypto->verify(
        keyPair->publicKey(), invalidHash, bytesConstRef(sig->data(), sig->size()));
    BOOST_CHECK(result == false);

    // recover
    BOOST_CHECK_THROW(
        signatureCrypto->recover(invalidHash, bytesConstRef(sig->data(), sig->size())),
        InvalidSignature);

    // invalid signature
    auto anotherSig = signatureCrypto->sign(keyPair, invalidHash, true);
    result = signatureCrypto->verify(
        keyPair->publicKey(), hashData, bytesConstRef(anotherSig->data(), anotherSig->size()));
    BOOST_CHECK(result == false);
    BOOST_CHECK_THROW(
        signatureCrypto->recover(hashData, bytesConstRef(anotherSig->data(), anotherSig->size())),
        InvalidSignature);

    // invalid sig
    auto keyPair2 = signatureCrypto->generateKeyPair();
    result = signatureCrypto->verify(
        keyPair2->publicKey(), hashData, bytesConstRef(sig->data(), sig->size()));
    BOOST_CHECK(result == false);

    auto signatureStruct =
        std::make_shared<SignatureDataWithPub>(bytesConstRef(sig->data(), sig->size()));
    auto r = signatureStruct->r();
    auto s = signatureStruct->s();

    auto signatureStruct2 = std::make_shared<SignatureDataWithPub>(r, s, signatureStruct->pub());
    auto encodedData = signatureStruct2->encode();
    auto recoverKey =
        signatureCrypto->recover(hashData, bytesConstRef(encodedData->data(), encodedData->size()));
    BOOST_CHECK(recoverKey->data() == keyPair->publicKey()->data());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos