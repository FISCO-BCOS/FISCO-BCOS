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
 * @file ClientServerTest.cpp
 * @brief End-to-end loopback test: RLPx client <-> server over TCP, covering
 *        the encrypted handshake, Hello/Status exchange, snappy framing and an
 *        eth/68 GetBlockHeaders request/response round trip.
 * @date 2026/8/18
 */
#include <bcos-devp2p/eth/Protocol.h>
#include <bcos-devp2p/rlpx/Client.h>
#include <bcos-devp2p/rlpx/MessageCodec.h>
#include <bcos-devp2p/rlpx/Messages.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <stdexcept>
#include <thread>

using namespace bcos;
using namespace bcos::devp2p;
using namespace bcos::devp2p::rlpx;

BOOST_AUTO_TEST_SUITE(ClientServerTest)

// The devp2p wire snappy is the raw-block format: a LEB128 uvarint of the
// uncompressed length followed by the tag sequence. Each tag: type = tag&3
// (0 literal, 1 copy-1, 2 copy-2, 3 copy-4), data = tag>>2. This is the exact
// format geth (Go snappy.Decode), ethrex (Rust snap::raw) and the vcpkg snappy
// port (RawCompress/RawUncompress) emit/consume.
BOOST_AUTO_TEST_CASE(snappyRawBlockFormatCompatibility)
{
    // Raw snappy for "abababababababab" (8 bytes):
    //   uvarint(8)=0x08, literal "ab" (type 0, len 2 -> tag (2-1)<<2 = 0x04)
    //   then copy-2 (type 2, len 6 -> tag (5<<2)|2 = 0x16), offset 02 00.
    bcos::bytes const rawSnappy = {0x08, 0x04, 0x61, 0x62, 0x16, 0x02, 0x00};
    bcos::bytes const expected = {0x61, 0x62, 0x61, 0x62, 0x61, 0x62, 0x61, 0x62};

    // Decode a frame whose payload is the raw snappy block: RLP(0x10) + block.
    bcos::bytes frame;
    frame.push_back(0x10);
    frame.insert(frame.end(), rawSnappy.begin(), rawSnappy.end());
    rlpx::MessageCodec codec;
    codec.enableCompression();
    auto msg = codec.decode(bcos::bytesConstRef(frame.data(), frame.size()));
    BOOST_CHECK_EQUAL(msg.id, 0x10u);
    BOOST_CHECK(msg.data == expected);

    // Encode the same payload: our literal-only output must be a valid raw
    // snappy block — uvarint(8)=0x08 then literal tag (7<<2)=0x1c + 8 literal
    // bytes.
    bcos::bytes frameOut = codec.encode(rlpx::Message{0x10, expected});
    // frameOut = RLP(0x10) || 08 1c <8 bytes>
    BOOST_REQUIRE(frameOut.size() == 1 + 10);
    BOOST_CHECK_EQUAL(frameOut[0], 0x10u);
    BOOST_CHECK_EQUAL(frameOut[1], 0x08u);
    BOOST_CHECK_EQUAL(frameOut[2], 0x1cu);
    BOOST_CHECK(bcos::bytes(frameOut.begin() + 3, frameOut.end()) == expected);
}

BOOST_AUTO_TEST_CASE(loopbackHandshakeAndMessageExchange)
{
    rlpx::EccKeyPair serverKey;
    rlpx::EccKeyPair clientKey;

    rlpx::PeerConfig serverConfig;
    serverConfig.clientId = "FISCO-BCOS-devp2p-server/v0.1.0";
    serverConfig.networkId = 11155111;
    serverConfig.genesisHash = h256(
        std::string_view("0x25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9"),
        h256::FromHex);
    serverConfig.forkId = {0x12345678, 0};

    rlpx::RlpxServer server(serverKey, 0, serverConfig);
    uint16_t port = server.port();

    // The server thread: accept + handshake/hello/status, then answer one
    // GetBlockHeaders request with a canned BlockHeaders response.
    std::atomic<bool> serverOk{false};
    std::thread serverThread([&] {
        try
        {
            auto established = server.accept();
            auto msg = established.session.recvMessage();
            if (msg.id != eth::frameId(eth::msg::GetBlockHeaders))
            {
                throw std::runtime_error("server: expected GetBlockHeaders");
            }
            auto request = eth::decodeGetBlockHeaders(ref(msg.data));

            eth::BlockHeadersMessage response;
            response.requestId = request.requestId;
            // A complete, well-formed Sepolia header (block 1) RLP element.
            response.headers = {fromHex(
                "f901fda025a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9a01dcc4d"
                "e8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347942f14582947e292a2ecd2"
                "0c430b46f2d27cfe213ca0c91d4ecd59dce3067d340b3aadfc0542974b4fb4db98af39f980a91ea0"
                "0db9dca056e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421a056e81f"
                "171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421b901000000000000000000"
                "00000000000000000000000000000000000000000000000000000000000000000000000000000000"
                "00000000000000000000000000000000000000000000000000000000000000000000000000000000"
                "00000000000000000000000000000000000000000000000000000000000000000000000000000000"
                "00000000000000000000000000000000000000000000000000000000000000000000000000000000"
                "00000000000000000000000000000000000000000000000000000000000000000000000000000000"
                "00000000000000000000000000000000000000000000000000000000000000000000000000000000"
                "000000000000000083020000018401c9c38080846173603a80a0cd039d5508e92723db0f078b5205"
                "da89144e3a6fee3a34124c966f53c35ce42c88c7faaf72b456848084342770c0")};
            established.session.sendMessage(
                Message{static_cast<uint8_t>(eth::frameId(eth::msg::BlockHeaders)),
                    eth::encodeBlockHeaders(response)});
            serverOk = true;
        }
        catch (std::exception const& e)
        {
            BOOST_ERROR(std::string("server: ") + e.what());
        }
    });

    rlpx::PeerConfig config;
    config.host = "127.0.0.1";
    config.port = port;
    config.peerPublicKey = serverKey.publicKey();
    config.networkId = 11155111;
    config.genesisHash = h256(
        std::string_view("0x25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9"),
        h256::FromHex);
    config.forkId = {0x12345678, 0};

    rlpx::RlpxClient client(std::move(clientKey), config);
    auto established = client.connect();

    // Verify the peer identity and status.
    BOOST_REQUIRE(established.peerHello.id.size() == 64);
    BOOST_CHECK(established.peerHello.id == serverKey.publicKey());
    BOOST_CHECK_EQUAL(established.peerStatus.networkId, 11155111u);
    BOOST_CHECK(established.peerStatus.genesisHash == config.genesisHash);
    BOOST_CHECK_EQUAL(established.peerStatus.protocolVersion, eth::kProtocolVersion);

    // Send a GetBlockHeaders request and verify the response.
    eth::GetBlockHeadersMessage request;
    request.requestId = 42;
    request.originNumber = 0;
    request.amount = 1;
    established.session.sendMessage(
        Message{static_cast<uint8_t>(eth::frameId(eth::msg::GetBlockHeaders)),
            eth::encodeGetBlockHeaders(request)});

    auto responseMsg = established.session.recvMessage();
    BOOST_CHECK_EQUAL(responseMsg.id, eth::frameId(eth::msg::BlockHeaders));
    auto response = eth::decodeBlockHeaders(ref(responseMsg.data));
    BOOST_CHECK_EQUAL(response.requestId, 42u);
    BOOST_REQUIRE_EQUAL(response.headers.size(), 1u);

    serverThread.join();
    BOOST_CHECK(serverOk);
}

BOOST_AUTO_TEST_SUITE_END()
