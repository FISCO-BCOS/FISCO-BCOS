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
 * @file EciesTest.cpp
 * @brief ECIES + recoverable-signature tests against independent vectors.
 * @date 2026/8/18
 */
#include <bcos-devp2p/rlpx/Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::devp2p::rlpx;

BOOST_AUTO_TEST_SUITE(EciesTest)

// Decrypt an independently generated ECIES ciphertext (Python pycryptodome +
// ecdsa), proving the whole scheme (ECDH copy-x, KDF, AES-128-CTR, MAC) is
// wire-compatible with geth's crypto/ecies.
BOOST_AUTO_TEST_CASE(decryptIndependentVector)
{
    auto wire = fromHex(
        "040a3111436338a9aff7ac07d2193e9e5327e552c75960c2668663d1859decc8"
        "73d50a12c767134cedfd9c11c46dcc59cb1ad456ad8c105113770940077c7dce"
        "52f2d6b08ba781a88349bfdd63de1a5dae46bd60b66d4c032570e9402e8bca94"
        "d7ee11f4a0d88b629890cd5287454819d422451d51f3d2f0cddaba");
    auto privateKey = fromHex("36a7edad64d51a568b00e51d3fa8cd340aa704153010edf7f55ab3066ca4ef21");
    auto macExtra = fromHex("01cf");

    auto plainText = EciesCipher::decrypt(ref(wire), ref(privateKey), ref(macExtra));
    BOOST_CHECK_EQUAL(toHex(plainText), "68656c6c6f20726c7078");  // "hello rlpx"
}

// geth's TestHandshakeForwardCompatibility inputs: the static shared secret and
// the signed message must match independent Python computation (ecdsa lib), and
// the recoverable signature must recover the ephemeral public key.
BOOST_AUTO_TEST_CASE(authSignatureMatchesGeth)
{
    auto keyA = fromHex("49a7b37aa6f6645917e7b807e9d1c00d4fa71f18343b0d4122a4d2df64dd6fee");
    auto keyB = fromHex("b71c71a67e1177ad4e901695e1b4b9ee17ae16c6668d313eac2f96dbcda3f291");
    auto ephA = fromHex("869d6ecf5211f1cc60418a13b9d870b22959d0c16f02bec714c960dd2298a32d");
    auto nonceA = fromHex("7e968bba13b6c50e2c4cd7f241cc0d64d1ac25c7f5952df231ac6a2bda8ee5d6");

    EccKeyPair keyAPair(keyA);
    EccKeyPair keyBPair(keyB);
    EccKeyPair ephAPair(ephA);

    // staticShared = ecdh(keyA_priv, keyB_pub) — validated against Python.
    auto staticShared = EciesCipher::computeSharedSecret(ref(keyBPair.publicKey()), ref(keyA));
    BOOST_CHECK_EQUAL(
        toHex(staticShared), "2d21423c1dc3355da36e7f2c2b530eeffcf0680f93201a958b2ec3a7d04958e6");

    bcos::bytes signedMsg = staticShared;
    xorBytes(signedMsg, ref(nonceA));
    BOOST_CHECK_EQUAL(
        toHex(signedMsg), "53b7c9860e75f0538f22a8de6a9f038b2d5c4dc866b53767ba82a98c0ac7bd30");

    // Sign with the ephemeral key and recover — must give back the ephemeral pubkey.
    auto signature = signRecoverable(ref(signedMsg), ref(ephA));
    auto recovered = recoverPublicKey(ref(signedMsg), ref(signature));
    BOOST_CHECK(recovered == ephAPair.publicKey());
}

// Recover the signer's public key from the signature.
BOOST_AUTO_TEST_CASE(recoverPublicKeyRoundTrip)
{
    EccKeyPair keyPair;
    auto hash = fromHex("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
    auto signature = signRecoverable(ref(hash), ref(keyPair.privateKey()));
    auto recovered = recoverPublicKey(ref(hash), ref(signature));
    BOOST_CHECK(recovered == keyPair.publicKey());
}

// ECIES round trip + tamper detection.
BOOST_AUTO_TEST_CASE(encryptDecryptRoundTrip)
{
    EccKeyPair receiver;
    auto plainText = fromHex("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
    auto macExtra = fromHex("01cf");

    auto cipherText =
        EciesCipher::encrypt(ref(plainText), ref(receiver.publicKey()), ref(macExtra));
    BOOST_CHECK(cipherText.size() == 65 + 16 + plainText.size() + 32);

    auto decrypted =
        EciesCipher::decrypt(ref(cipherText), ref(receiver.privateKey()), ref(macExtra));
    BOOST_CHECK(decrypted == plainText);

    // Wrong mac-extra-data must fail the MAC check.
    auto wrongExtra = fromHex("0000");
    BOOST_CHECK_THROW(
        EciesCipher::decrypt(ref(cipherText), ref(receiver.privateKey()), ref(wrongExtra)),
        std::runtime_error);

    // Tamper one byte -> MAC mismatch.
    auto tampered = cipherText;
    tampered[70] ^= 0x01;
    BOOST_CHECK_THROW(
        EciesCipher::decrypt(ref(tampered), ref(receiver.privateKey()), ref(macExtra)),
        std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
