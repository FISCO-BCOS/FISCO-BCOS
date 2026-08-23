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
 * @file RlpxPrimitivesTest.cpp
 * @brief Tests for the RLPx crypto primitives: AES-128/256-CTR, HMAC-SHA256,
 *        secp256k1 ECDH, secure random bytes.
 * @date 2026/8/18
 */
#include <bcos-crypto/encrypt/AesCtrCipher.h>
#include <bcos-crypto/encrypt/HmacSha256.h>
#include <bcos-crypto/random/CryptoRandom.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Ecdh.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos
{
BOOST_AUTO_TEST_SUITE(RlpxPrimitivesTest)

// AES-128-CTR against a known vector (NIST SP 800-38A F.5.1).
BOOST_AUTO_TEST_CASE(aes128Ctr)
{
    auto key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
    auto iv = fromHex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    auto plain = fromHex("6bc1bee22e409f96e93d7e117393172a");

    auto cipher = aesCtrCrypt(ref(plain), ref(key), ref(iv));
    BOOST_CHECK_EQUAL(toHex(cipher), "874d6191b620e3261bef6864990db6ce");

    auto roundTrip = aesCtrCrypt(ref(cipher), ref(key), ref(iv));
    BOOST_CHECK(roundTrip == plain);
}

// Stateful AES-256-CTR: two updates must continue the same keystream.
BOOST_AUTO_TEST_CASE(aes256CtrStateful)
{
    auto key = fromHex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    auto iv = bytes(16, 0);
    auto part1 = fromHex("6bc1bee22e409f96e93d7e117393172a");
    auto part2 = fromHex("ae2d8a571e03ac9c9eb76fac45af8e51");

    AesCtrCipher cipher(ref(key), ref(iv), AesCtrCipher::Direction::Encrypt);
    auto c1 = cipher.update(ref(part1));
    auto c2 = cipher.update(ref(part2));

    // Single-shot over the concatenation must match.
    auto joined = part1;
    joined.insert(joined.end(), part2.begin(), part2.end());
    auto oneShot = aesCtrCrypt(ref(joined), ref(key), ref(iv));
    BOOST_CHECK(c1 == bytesConstRef(oneShot.data(), part1.size()).toBytes());
    BOOST_CHECK(
        c2 == bytesConstRef(oneShot.data() + part1.size(), part2.size()).toBytes());
}

// HMAC-SHA256 against RFC 4231 test case 2.
BOOST_AUTO_TEST_CASE(hmacSha256Vector)
{
    auto key = fromHex("4a656665");  // "Jefe"
    auto data = fromHex("7768617420646f2079612077616e7420666f72206e6f7468696e673f");  // "what do ya want for nothing?"

    auto mac = hmacSha256(ref(key), ref(data));
    BOOST_CHECK_EQUAL(toHex(mac),
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

// secp256k1 ECDH with the raw-x hash against a fixed key pair.
BOOST_AUTO_TEST_CASE(ecdhCopyX)
{
    auto priv1 = fromHex("7ebbc6a8358bc76dd73ebc557056702c8cfc34e5cfcd90eb83af0347575fd2ad");
    auto priv2 = fromHex("6a3d6396903245bba5837752b9e0348874e72db0c4e11e9c485a81b4ea4353b9");

    auto pub1 = bcos::crypto::secp256k1PriToPub(
        std::make_shared<bcos::crypto::KeyImpl>(priv1));
    auto pub2 = bcos::crypto::secp256k1PriToPub(
        std::make_shared<bcos::crypto::KeyImpl>(priv2));

    bytesConstRef pub1Ref(pub1->data().data(), pub1->data().size());
    bytesConstRef pub2Ref(pub2->data().data(), pub2->data().size());
    auto s1 = secp256k1EcdhCopyX(pub2Ref, ref(priv1));
    auto s2 = secp256k1EcdhCopyX(pub1Ref, ref(priv2));
    BOOST_CHECK(s1 == s2);
    BOOST_CHECK_EQUAL(s1.size(), 32u);
}

// Secure random bytes differ and are non-trivial.
BOOST_AUTO_TEST_CASE(randomBytes)
{
    auto a = cryptoRandomBytes(32);
    auto b = cryptoRandomBytes(32);
    BOOST_CHECK(a != b);
    BOOST_CHECK(std::any_of(a.begin(), a.end(), [](byte x) { return x != 0; }));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos
