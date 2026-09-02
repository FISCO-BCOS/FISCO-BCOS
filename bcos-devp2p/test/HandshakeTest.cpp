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
#include <bcos-crypto/random/CryptoRandom.h>
#include <bcos-devp2p/rlpx/Crypto.h>
#include <bcos-devp2p/rlpx/Framing.h>
#include <bcos-devp2p/rlpx/Handshake.h>
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

    BOOST_CHECK_EQUAL(
        toHex(aesSecret), "80e8632c05fed6fc2a13b0f8d31a3cf645366239170ea067065aba8e28bac487");
    BOOST_CHECK_EQUAL(
        toHex(macSecret), "2ea74ec5dae199227dff1af715362700e989d889d7a493cb0639691efb8e5f98");
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

    AuthAckMessage built(ephemeral, ref(initiator.publicKey()), ref(recipientNonce));
    auto wire = built.serialize();
    bytesConstRef wireRef(wire.data(), wire.size());
    auto body = wireRef.getCroppedData(2);

    AuthAckMessage parsed(body, ref(initiator.privateKey()));
    BOOST_CHECK(parsed.ephemeralPublicKey() == ephemeral.publicKey());
    BOOST_CHECK(parsed.nonce().toBytes() == built.nonce().toBytes());
}

// geth's EIP-8 forward-compatibility vectors: decrypt the Auth2/Ack2
// ciphertexts with geth's static keys and check the recovered initiator /
// ephemeral public keys and nonces against geth's expected values. This proves
// the handshake ECIES envelope + RLP parsing is wire-compatible with geth, not
// merely self-consistent.
BOOST_AUTO_TEST_CASE(eip8AuthAckCiphertextDecode)
{
    auto const keyA = fromHex("49a7b37aa6f6645917e7b807e9d1c00d4fa71f18343b0d4122a4d2df64dd6fee");
    auto const keyB = fromHex("b71c71a67e1177ad4e901695e1b4b9ee17ae16c6668d313eac2f96dbcda3f291");
    auto const ephA = fromHex("869d6ecf5211f1cc60418a13b9d870b22959d0c16f02bec714c960dd2298a32d");
    auto const ephB = fromHex("e238eb8e04fee6511ab04c6dd3c89ce097b11f25d584863ac2b6d5b35b1847e4");
    auto const nonceA = fromHex("7e968bba13b6c50e2c4cd7f241cc0d64d1ac25c7f5952df231ac6a2bda8ee5d6");
    auto const nonceB = fromHex("559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd");

    EccKeyPair keyAPair(keyA);
    EccKeyPair keyBPair(keyB);
    EccKeyPair ephAPair(ephA);
    EccKeyPair ephBPair(ephB);

    // Auth2 (geth eip8HandshakeAuthTests[0]): initiator A -> recipient B.
    auto const authWireHex =
        "01b304ab7578555167be8154d5cc456f567d5ba302662433674222360f08d5f1534499d3678b513b0fca474f"
        "3a514b18e75683032eb63fccb16c156dc6eb2c0b1593f0d84ac74f6e475f1b8d56116b849634a8c458705bf8"
        "3a626ea0384d4d7341aae591fae42ce6bd5c850bfe0b999a694a49bbbaf3ef6cda61110601d3b4c02ab6c304"
        "37257a6e0117792631a4b47c1d52fc0f8f89caadeb7d02770bf999cc147d2df3b62e1ffb2c9d8c125a398486"
        "5356266bca11ce7d3a688663a51d82defaa8aad69da39ab6d5470e81ec5f2a7a47fb865ff7cca21516f9299a"
        "07b1bc63ba56c7a1a892112841ca44b6e0034dee70c9adabc15d76a54f443593fafdc3b27af8059703f88928"
        "e199cb122362a4b35f62386da7caad09c001edaeb5f8a06d2b26fb6cb93c52a9fca51853b68193916982358f"
        "e1e5369e249875bb8d0d0ec36f917bc5e1eafd5896d46bd61ff23f1a863a8a8dcd54c7b109b771c8e61ec9c8"
        "908c733c0263440e2aa067241aaa433f0bb053c7b31a838504b148f570c0ad62837129e547678c5190341e4f"
        "1693956c3bf7678318e2d5b5340c9e488eefea198576344afbdf66db5f51204a6961a63ce072c8926c";
    auto authWire = fromHex(authWireHex);
    bytesConstRef authWireRef(authWire.data(), authWire.size());
    BOOST_REQUIRE_EQUAL(authWire.size(), 437);  // 2-byte size prefix + 435 B body
    AuthMessage auth(authWireRef.getCroppedData(2), ref(keyB));
    BOOST_CHECK(auth.initiatorPublicKey() == keyAPair.publicKey());
    BOOST_CHECK(auth.ephemeralPublicKey() == ephAPair.publicKey());
    BOOST_CHECK(auth.nonce().toBytes() == nonceA);

    // Ack2 (geth eip8HandshakeRespTests[0]): recipient B -> initiator A.
    auto const ackWireHex =
        "01ea0451958701280a56482929d3b0757da8f7fbe5286784beead59d95089c217c9b917788989470b0e330cc"
        "6e4fb383c0340ed85fab836ec9fb8a49672712aeabbdfd1e837c1ff4cace34311cd7f4de05d59279e3524ab2"
        "6ef753a0095637ac88f2b499b9914b5f64e143eae548a1066e14cd2f4bd7f814c4652f11b254f8a2d0191e2f"
        "5546fae6055694aed14d906df79ad3b407d94692694e259191cde171ad542fc588fa2b7333313d82a9f88733"
        "2f1dfc36cea03f831cb9a23fea05b33deb999e85489e645f6aab1872475d488d7bd6c7c120caf28dbfc5d683"
        "3888155ed69d34dbdc39c1f299be1057810f34fbe754d021bfca14dc989753d61c413d261934e1a9c67ee060"
        "a25eefb54e81a4d14baff922180c395d3f998d70f46f6b58306f969627ae364497e73fc27f6d17ae45a413d3"
        "22cb8814276be6ddd13b885b201b943213656cde498fa0e9ddc8e0b8f8a53824fbd82254f3e2c17e8eaea009"
        "c38b4aa0a3f306e8797db43c25d68e86f262e564086f59a2fc60511c42abfb3057c247a8a8fe4fb3ccbadde1"
        "7514b7ac8000cdb6a912778426260c47f38919a91f25f4b5ffb455d6aaaf150f7e5529c100ce62d6d92826a7"
        "1778d809bdf60232ae21ce8a437eca8223f45ac37f6487452ce626f549b3b5fdee26afd2072e4bc75833c246"
        "4c805246155289f4";
    auto ackWire = fromHex(ackWireHex);
    bytesConstRef ackWireRef(ackWire.data(), ackWire.size());
    BOOST_REQUIRE_EQUAL(ackWire.size(), 492);  // 2-byte size prefix + 490 B body
    AuthAckMessage ack(ackWireRef.getCroppedData(2), ref(keyA));
    BOOST_CHECK(ack.ephemeralPublicKey() == ephBPair.publicKey());
    BOOST_CHECK(ack.nonce().toBytes() == nonceB);
}

BOOST_AUTO_TEST_SUITE_END()
