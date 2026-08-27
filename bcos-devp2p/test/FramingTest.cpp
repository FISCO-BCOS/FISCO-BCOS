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
 * @file FramingTest.cpp
 * @brief FramingCipher initiator/recipient self-consistency round trips.
 * @date 2026/8/27
 */
#include <bcos-devp2p/rlpx/Framing.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::devp2p::rlpx;

namespace
{
// Deterministic handshake key material shared by both ends of a session.
FramingCipher::KeyMaterial makeKeyMaterial()
{
    FramingCipher::KeyMaterial keyMaterial;
    keyMaterial.ephemeralSharedSecret =
        fromHex("e54997b78e3ec70d6a984f6b2a8d6a04e52ec9b2e2df0e37a17e5e2f52f8a1f2");
    keyMaterial.initiatorNonce =
        fromHex("7e968bba13b6c50e2c4cd7f241cc0d64d1ac25c7f5952df231ac6a2bda8ee5d6");
    keyMaterial.recipientNonce =
        fromHex("b71c71a67e1177ad4e901695e1b4b9ee17ae16c6668d313eac2f96dbcda3f291");
    keyMaterial.initiatorFirstMessageData = fromHex("01cfdeadbeefcafe01020304");
    keyMaterial.recipientFirstMessageData = fromHex("02d0facefeedbeef05060708");
    return keyMaterial;
}

// One side of a session: encrypts into a wire stream, decrypts from one.
struct CipherEnd
{
    explicit CipherEnd(bool _isInitiator)
    {
        auto keyMaterial = makeKeyMaterial();
        keyMaterial.isInitiator = _isInitiator;
        cipher = std::make_unique<FramingCipher>(keyMaterial);
    }

    bcos::bytes encrypt(bcos::bytes const& _frameData) { return cipher->encryptFrame(_frameData); }

    bcos::bytes decrypt(bcos::bytes const& _wireData)
    {
        auto framePayloadSize = cipher->decryptHeader(ref(_wireData));
        auto wireFrameSize = FramingCipher::frameSize(framePayloadSize);
        BOOST_REQUIRE(_wireData.size() == FramingCipher::headerSize() + wireFrameSize);
        return cipher->decryptFrame(
            bytesConstRef(_wireData.data() + FramingCipher::headerSize(), wireFrameSize),
            framePayloadSize);
    }

    std::unique_ptr<FramingCipher> cipher;
};
}  // namespace

BOOST_AUTO_TEST_SUITE(FramingTest)

// Consecutive frames in both directions between the initiator and the
// recipient: exercises the continuous AES-CTR stream, the running Keccak MAC
// state, padding, and the egress/ingress hasher seeding symmetry.
BOOST_AUTO_TEST_CASE(roundTripBothDirections)
{
    CipherEnd initiator(true);
    CipherEnd recipient(false);

    // Empty, sub-block, exact-block, cross-block and multi-block payloads.
    std::vector<size_t> sizes = {0, 1, 15, 16, 17, 100, 1024};
    for (auto size : sizes)
    {
        bcos::bytes ping(size, 0);
        for (size_t i = 0; i < size; ++i)
        {
            ping[i] = static_cast<bcos::byte>(i & 0xff);
        }
        bcos::bytes pong(size + 3, 0xAB);

        // initiator -> recipient
        auto pingWire = initiator.encrypt(ping);
        BOOST_CHECK(
            pingWire.size() == FramingCipher::headerSize() + FramingCipher::frameSize(ping.size()));
        auto pingBack = recipient.decrypt(pingWire);
        BOOST_CHECK(pingBack == ping);

        // recipient -> initiator
        auto pongWire = recipient.encrypt(pong);
        auto pongBack = initiator.decrypt(pongWire);
        BOOST_CHECK(pongBack == pong);
    }
}

// A tampered header MAC must fail the header authentication.
BOOST_AUTO_TEST_CASE(tamperedHeaderMacThrows)
{
    CipherEnd initiator(true);
    CipherEnd recipient(false);

    auto wire = initiator.encrypt(fromHex("00112233445566778899aabbccddeeff"));
    wire[20] ^= 0x01;  // inside the 16B header MAC (bytes 16..31)
    BOOST_CHECK_EXCEPTION(
        recipient.decrypt(wire), std::runtime_error, [](std::runtime_error const& error) {
            return std::string(error.what()).find("invalid header MAC") != std::string::npos;
        });
}

// A tampered frame MAC must fail the frame authentication.
BOOST_AUTO_TEST_CASE(tamperedFrameMacThrows)
{
    CipherEnd initiator(true);
    CipherEnd recipient(false);

    auto wire = initiator.encrypt(fromHex("00112233445566778899aabbccddeeff"));
    wire.back() ^= 0x01;  // last byte of the trailing frame MAC
    BOOST_CHECK_EXCEPTION(
        recipient.decrypt(wire), std::runtime_error, [](std::runtime_error const& error) {
            return std::string(error.what()).find("invalid frame MAC") != std::string::npos;
        });
}

// A tampered frame ciphertext must fail the frame MAC check.
BOOST_AUTO_TEST_CASE(tamperedFrameCipherTextThrows)
{
    CipherEnd initiator(true);
    CipherEnd recipient(false);

    auto wire = initiator.encrypt(fromHex("00112233445566778899aabbccddeeff"));
    wire[FramingCipher::headerSize()] ^= 0x01;  // first byte of the frame ciphertext
    BOOST_CHECK_EXCEPTION(
        recipient.decrypt(wire), std::runtime_error, [](std::runtime_error const& error) {
            return std::string(error.what()).find("invalid frame MAC") != std::string::npos;
        });
}

// A read buffer holding coalesced bytes of the next frame must still decrypt
// the current frame, and the leftover frame must decrypt afterwards (the
// running ingress MAC state stays in sync).
BOOST_AUTO_TEST_CASE(trailingExtraBytesIgnored)
{
    CipherEnd initiator(true);
    CipherEnd recipient(false);

    auto payload = fromHex("00112233445566778899aabbccddeeff");
    auto nextPayload = fromHex("deadbeefcafe");
    auto wire = initiator.encrypt(payload);
    auto nextWire = initiator.encrypt(nextPayload);
    wire.insert(wire.end(), nextWire.begin(), nextWire.end());

    auto framePayloadSize = recipient.cipher->decryptHeader(ref(wire));
    auto wireFrameSize = FramingCipher::frameSize(framePayloadSize);
    BOOST_REQUIRE(wire.size() >= FramingCipher::headerSize() + wireFrameSize + nextWire.size());
    auto decrypted =
        recipient.cipher->decryptFrame(bytesConstRef(wire.data() + FramingCipher::headerSize(),
                                           wire.size() - FramingCipher::headerSize()),
            framePayloadSize);
    BOOST_CHECK(decrypted == payload);

    auto nextFramePayloadSize = recipient.cipher->decryptHeader(
        bytesConstRef(wire.data() + FramingCipher::headerSize() + wireFrameSize, nextWire.size()));
    auto nextWireFrameSize = FramingCipher::frameSize(nextFramePayloadSize);
    auto nextDecrypted = recipient.cipher->decryptFrame(
        bytesConstRef(
            wire.data() + FramingCipher::headerSize() + wireFrameSize + FramingCipher::headerSize(),
            nextWireFrameSize),
        nextFramePayloadSize);
    BOOST_CHECK(nextDecrypted == nextPayload);
}

// Frames over the 24-bit size limit must be rejected, not silently truncated.
BOOST_AUTO_TEST_CASE(oversizedFrameThrows)
{
    CipherEnd initiator(true);
    bcos::bytes oversized(0x1000000, 0);  // 2^24 bytes
    BOOST_CHECK_EXCEPTION(initiator.cipher->encryptFrame(std::move(oversized)), std::runtime_error,
        [](std::runtime_error const& error) {
            return std::string(error.what()).find("frame too large") != std::string::npos;
        });
}

BOOST_AUTO_TEST_SUITE_END()
