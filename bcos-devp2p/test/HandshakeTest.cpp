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
 * @file HandshakeTest.cpp
 * @brief FramingCipher secret derivation + auth/ack message round trips.
 * @date 2026/8/18
 */
#include <bcos-devp2p/rlpx/Crypto.h>
#include <bcos-devp2p/rlpx/Framing.h>
#include <bcos-devp2p/rlpx/Handshake.h>
#include <bcos-crypto/random/CryptoRandom.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::devp2p::rlpx;

BOOST_AUTO_TEST_SUITE(HandshakeTest)

// geth TestHandshakeForwardCompatibility: aes-secret and mac-secret derived
// from the (ephB, ephA) ECDH + nonces must match geth's authoritative values.
BOOST_AUTO_TEST_CASE(secretsMatchGethVector)
{
    auto ephA = fromHex("869d6ecf5211f1cc60418a13b9d870b22959d0c16f02bec714c960dd2298a32d");
    auto ephB = fromHex("e238eb8e04fee6511ab04c6dd3c89ce097b11f25d584863ac2b6d5b35b1847e4");
    auto nonceA = fromHex("7e968bba13b6c50e2c4cd7f241cc0d64d1ac25c7f5952df231ac6a2bda8ee5d6");
    auto nonceB = fromHex("559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd");

    EccKeyPair ephAPair(ephA);
    EccKeyPair ephBPair(ephB);

    FramingCipher::KeyMaterial keyMaterial;
    keyMaterial.ephemeralSharedSecret =
        EciesCipher::computeSharedSecret(ref(ephAPair.publicKey()), ref(ephB));
    keyMaterial.isInitiator = false;
    keyMaterial.initiatorNonce = nonceA;
    keyMaterial.recipientNonce = nonceB;
    keyMaterial.initiatorFirstMessageData = {0x01, 0x02};
    keyMaterial.recipientFirstMessageData = {0x03, 0x04};

    bytes aesSecret;
    bytes macSecret;
    FramingCipher::deriveSecrets(keyMaterial, aesSecret, macSecret);

    BOOST_CHECK_EQUAL(toHex(aesSecret),
        "80e8632c05fed6fc2a13b0f8d31a3cf645366239170ea067065aba8e28bac487");
    BOOST_CHECK_EQUAL(toHex(macSecret),
        "2ea74ec5dae199227dff1af715362700e989d889d7a493cb0639691efb8e5f98");
}

// Auth message build + parse (initiator signs, recipient recovers).
BOOST_AUTO_TEST_CASE(authMessageRoundTrip)
{
    EccKeyPair initiator;
    EccKeyPair recipient;
    EccKeyPair ephemeral;

    AuthMessage built(initiator, ref(recipient.publicKey()), ephemeral);
    auto wire = built.serialize();
    // Strip the 2-byte size prefix; the parse constructor takes the ECIES body.
    bytesConstRef wireRef(wire.data(), wire.size());
    auto body = wireRef.getCroppedData(2);

    AuthMessage parsed(body, ref(recipient.privateKey()));
    BOOST_CHECK(parsed.initiatorPublicKey() == initiator.publicKey());
    BOOST_CHECK(parsed.ephemeralPublicKey() == ephemeral.publicKey());
    BOOST_CHECK(parsed.nonce().toBytes() == built.nonce().toBytes());
}

// Ack message round trip.
BOOST_AUTO_TEST_CASE(ackMessageRoundTrip)
{
    EccKeyPair initiator;
    EccKeyPair ephemeral;
    auto recipientNonce = bcos::crypto::cryptoRandomBytes(32);

    AuthAckMessage built(
        ephemeral, ref(initiator.publicKey()), ref(recipientNonce));
    auto wire = built.serialize();
    bytesConstRef wireRef(wire.data(), wire.size());
    auto body = wireRef.getCroppedData(2);

    AuthAckMessage parsed(body, ref(initiator.privateKey()));
    BOOST_CHECK(parsed.ephemeralPublicKey() == ephemeral.publicKey());
    BOOST_CHECK(parsed.nonce().toBytes() == built.nonce().toBytes());
}

BOOST_AUTO_TEST_SUITE_END()
