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
 * @file Discv4WireTest.cpp
 * @brief discv4 packet codec tests: geth p2p/discover/v4wire EIP-8 forward-compatibility
 *        vectors, round trips for every packet type, hash/signature/expiry checks.
 * @date 2026/11/3
 */
#include <bcos-devp2p/disc/Discv4Wire.h>
#include <bcos-devp2p/rlpx/Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::devp2p;
using namespace bcos::devp2p::disc;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(Discv4WireTest)

// geth v4wire_test.go: every vector is signed by this key.
constexpr std::string_view c_testKey =
    "b71c71a67e1177ad4e901695e1b4b9ee17ae16c6668d313eac2f96dbcda3f291";

bcos::bytes testPublicKey()
{
    return rlpx::EccKeyPair(fromHex(std::string(c_testKey))).publicKey();
}

template <typename R>
std::string errorOf(R const& _result)
{
    return _result ? std::string() : _result.error().message;
}

bcos::bytes ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return {a, b, c, d};
}

BOOST_AUTO_TEST_CASE(eip8PingWithoutSeq)
{
    auto input = fromHex(std::string(
        "71dbda3a79554728d4f94411e42ee1f8b0d561c10e1e5f5893367948c6a7d70bb87b235fa28a77070271b6c1"
        "64a2dce8c7e13a5739b53b5e96f2e5acb0e458a02902f5965d55ecbeb2ebb6cabb8b2b232896a36b737666c5"
        "5265ad0a68412f250001ea04cb847f000001820cfa8215a8d790000000000000000000000000000000018208"
        "ae820d058443b9a355"));
    auto decoded = decodePacket(bcos::ref(input));
    BOOST_REQUIRE_MESSAGE(decoded.has_value(), errorOf(decoded));
    BOOST_CHECK(decoded->type == PacketType::Ping);
    BOOST_CHECK(decoded->senderPublicKey == testPublicKey());
    auto const& ping = std::get<PingPacket>(decoded->packet);
    BOOST_CHECK_EQUAL(ping.version, 4u);
    BOOST_CHECK(ping.from.ip == ipv4(127, 0, 0, 1));
    BOOST_CHECK_EQUAL(ping.from.udp, 3322);
    BOOST_CHECK_EQUAL(ping.from.tcp, 5544);
    BOOST_CHECK(ping.to.isV6());
    BOOST_CHECK_EQUAL(ping.to.ipString(), "::1");
    BOOST_CHECK_EQUAL(ping.to.udp, 2222);
    BOOST_CHECK_EQUAL(ping.to.tcp, 3333);
    BOOST_CHECK_EQUAL(ping.expiration, 1136239445u);
    BOOST_CHECK_EQUAL(ping.enrSeq, 0u);
    BOOST_CHECK_EQUAL(toHex(decoded->hash), toHex(bcos::bytes(input.begin(), input.begin() + 32)));
}

BOOST_AUTO_TEST_CASE(eip8PingWithSeqAndExtraElement)
{
    auto input = fromHex(std::string(
        "e9614ccfd9fc3e74360018522d30e1419a143407ffcce748de3e22116b7e8dc92ff74788c0b6663aaa3d67d6"
        "41936511c8f8d6ad8698b820a7cf9e1be7155e9a241f556658c55428ec0563514365799a4be2be5a685a8097"
        "1ddcfa80cb422cdd0101ec04cb847f000001820cfa8215a8d790000000000000000000000000000000018208"
        "ae820d058443b9a3550102"));
    auto decoded = decodePacket(bcos::ref(input));
    BOOST_REQUIRE_MESSAGE(decoded.has_value(), errorOf(decoded));
    BOOST_CHECK(decoded->senderPublicKey == testPublicKey());
    auto const& ping = std::get<PingPacket>(decoded->packet);
    BOOST_CHECK_EQUAL(ping.enrSeq, 1u);  // the trailing 0x02 element is ignored
    BOOST_CHECK_EQUAL(ping.expiration, 1136239445u);
}

BOOST_AUTO_TEST_CASE(eip8FindNodeWithExtraElements)
{
    auto input = fromHex(std::string(
        "c7c44041b9f7c7e41934417ebac9a8e1a4c6298f74553f2fcfdcae6ed6fe53163eb3d2b52e39fe91831b8a92"
        "7bf4fc222c3902202027e5e9eb812195f95d20061ef5cd31d502e47ecb61183f74a504fe04c51e73df81f25c"
        "4d506b26db4517490103f84eb840ca634cae0d49acb401d8a4c6b6fe8c55b70d115bf400769cc1400f3258cd"
        "31387574077f301b421bc84df7266c44e9e6d569fc56be00812904767bf5ccd1fc7f8443b9a3558299998399"
        "9999280dc62cc8255c73471e0a61da0c89acdc0e035e260add7fc0c04ad9ebf3919644c91cb247affc82b69b"
        "d2ca235c71eab8e49737c937a2c396"));
    auto decoded = decodePacket(bcos::ref(input));
    BOOST_REQUIRE_MESSAGE(decoded.has_value(), errorOf(decoded));
    BOOST_CHECK(decoded->type == PacketType::FindNode);
    BOOST_CHECK(decoded->senderPublicKey == testPublicKey());
    auto const& findNode = std::get<FindNodePacket>(decoded->packet);
    BOOST_CHECK_EQUAL(toHex(findNode.target),
        "ca634cae0d49acb401d8a4c6b6fe8c55b70d115bf400769cc1400f3258cd31387574077f301b421bc84df7266c"
        "44e9e6d569fc56be00812904767bf5ccd1fc7f");
    BOOST_CHECK_EQUAL(findNode.expiration, 1136239445u);
}

BOOST_AUTO_TEST_CASE(eip8NeighborsWithIpv6AndExtraElements)
{
    auto input = fromHex(std::string(
        "c679fc8fe0b8b12f06577f2e802d34f6fa257e6137a995f6f4cbfc9ee50ed3710faf6e66f932c4c8d81d6434"
        "3f429651328758b47d3dbc02c4042f0fff6946a50f4a49037a72bb550f3a7872363a83e1b9ee6469856c24eb"
        "4ef80b7535bcf99c0004f9015bf90150f84d846321163782115c82115db8403155e1427f85f10a5c9a775587"
        "7748041af1bcd8d474ec065eb33df57a97babf54bfd2103575fa829115d224c523596b401065a97f74010610"
        "fce76382c0bf32f84984010203040101b840312c55512422cf9b8a4097e9a6ad79402e87a15ae909a4bfefa2"
        "2398f03d20951933beea1e4dfa6f968212385e829f04c2d314fc2d4e255e0d3bc08792b069dbf85990200"
        "10db83c4d001500000000abcdef12820d05820d05b84038643200b172dcfef857492156971f0e6aa2c538d8"
        "b74010f8e140811d53b98c765dd2d96126051913f44582e8c199ad7c6d6819e9a56483f637feaac9448aacf8"
        "599020010db885a308d313198a2e037073488203e78203e8b8408dcab8618c3253b558d459da53bd8fa68935"
        "a719aff8b811197101a4b2b47dd2d47295286fc00cc081bb542d760717d1bdd6bec2c37cd72eca367d6dd3b9"
        "df738443b9a355010203b525a138aa34383fec3d2719a0"));
    auto decoded = decodePacket(bcos::ref(input));
    BOOST_REQUIRE_MESSAGE(decoded.has_value(), errorOf(decoded));
    BOOST_CHECK(decoded->type == PacketType::Neighbors);
    BOOST_CHECK(decoded->senderPublicKey == testPublicKey());
    auto const& neighbors = std::get<NeighborsPacket>(decoded->packet);
    BOOST_REQUIRE_EQUAL(neighbors.nodes.size(), 4u);
    BOOST_CHECK_EQUAL(neighbors.nodes[0].endpoint.ipString(), "99.33.22.55");
    BOOST_CHECK_EQUAL(neighbors.nodes[0].endpoint.udp, 4444);
    BOOST_CHECK_EQUAL(neighbors.nodes[0].endpoint.tcp, 4445);
    BOOST_CHECK_EQUAL(toHex(neighbors.nodes[0].id),
        "3155e1427f85f10a5c9a7755877748041af1bcd8d474ec065eb33df57a97babf54bfd2103575fa829115d224c5"
        "23596b401065a97f74010610fce76382c0bf32");
    BOOST_CHECK_EQUAL(neighbors.nodes[1].endpoint.ipString(), "1.2.3.4");
    BOOST_CHECK_EQUAL(neighbors.nodes[1].endpoint.udp, 1);
    BOOST_CHECK_EQUAL(neighbors.nodes[1].endpoint.tcp, 1);
    BOOST_CHECK_EQUAL(neighbors.nodes[2].endpoint.ipString(), "2001:db8:3c4d:15::abcd:ef12");
    BOOST_CHECK_EQUAL(neighbors.nodes[2].endpoint.udp, 3333);
    BOOST_CHECK_EQUAL(
        neighbors.nodes[3].endpoint.ipString(), "2001:db8:85a3:8d3:1319:8a2e:370:7348");
    BOOST_CHECK_EQUAL(neighbors.nodes[3].endpoint.udp, 999);
    BOOST_CHECK_EQUAL(neighbors.nodes[3].endpoint.tcp, 1000);
    BOOST_CHECK_EQUAL(neighbors.expiration, 1136239445u);
}

BOOST_AUTO_TEST_CASE(roundTripEveryType)
{
    rlpx::EccKeyPair key(fromHex(std::string(c_testKey)));
    auto privateKey = bcos::ref(key.privateKey());

    PingPacket ping;
    ping.from = {ipv4(10, 0, 0, 1), 30303, 30303};
    ping.to = {ipv4(10, 0, 0, 2), 30304, 0};
    ping.expiration = 1'700'000'000;
    ping.enrSeq = 42;
    auto encodedPing = encodePacket(ping, privateKey);
    BOOST_CHECK_LE(encodedPing.datagram.size(), c_maxPacketSize);
    auto decodedPing = decodePacket(bcos::ref(encodedPing.datagram));
    BOOST_REQUIRE(decodedPing.has_value());
    BOOST_CHECK(decodedPing->senderPublicKey == key.publicKey());
    BOOST_CHECK(decodedPing->hash == encodedPing.hash);
    auto const& p = std::get<PingPacket>(decodedPing->packet);
    BOOST_CHECK(p.from == ping.from);
    BOOST_CHECK(p.to == ping.to);
    BOOST_CHECK_EQUAL(p.enrSeq, 42u);
    BOOST_CHECK_EQUAL(p.version, 4u);

    PongPacket pong;
    pong.to = ping.from;
    pong.pingHash = encodedPing.hash;
    pong.expiration = 1'700'000'000;
    auto encodedPong = encodePacket(pong, privateKey);
    auto decodedPong = decodePacket(bcos::ref(encodedPong.datagram));
    BOOST_REQUIRE(decodedPong.has_value());
    auto const& q = std::get<PongPacket>(decodedPong->packet);
    BOOST_CHECK(q.pingHash == encodedPing.hash);
    BOOST_CHECK_EQUAL(q.enrSeq, 0u);  // omitted on the wire when zero
    BOOST_CHECK(q.to == ping.from);

    FindNodePacket findNode;
    findNode.target = key.publicKey();
    findNode.expiration = 1'700'000'000;
    auto encodedFind = encodePacket(findNode, privateKey);
    auto decodedFind = decodePacket(bcos::ref(encodedFind.datagram));
    BOOST_REQUIRE(decodedFind.has_value());
    BOOST_CHECK(std::get<FindNodePacket>(decodedFind->packet).target == key.publicKey());

    NeighborsPacket neighbors;
    for (size_t i = 0; i < c_maxNeighbors; ++i)
    {
        NeighborNode node;
        node.endpoint = {bcos::bytes(16, static_cast<bcos::byte>(i + 1)), 65535, 65535};
        node.id = bcos::bytes(64, static_cast<bcos::byte>(i));
        neighbors.nodes.push_back(node);
    }
    neighbors.expiration = std::numeric_limits<uint64_t>::max();
    auto encodedNeighbors = encodePacket(neighbors, privateKey);
    // The geth MaxNeighbors derivation: 12 worst-case nodes still fit 1280 bytes.
    BOOST_CHECK_LE(encodedNeighbors.datagram.size(), c_maxPacketSize);
    auto decodedNeighbors = decodePacket(bcos::ref(encodedNeighbors.datagram));
    BOOST_REQUIRE(decodedNeighbors.has_value());
    BOOST_CHECK(std::get<NeighborsPacket>(decodedNeighbors->packet).nodes == neighbors.nodes);

    EnrRequestPacket request{1'700'000'000};
    auto encodedRequest = encodePacket(request, privateKey);
    auto decodedRequest = decodePacket(bcos::ref(encodedRequest.datagram));
    BOOST_REQUIRE(decodedRequest.has_value());
    BOOST_CHECK_EQUAL(
        std::get<EnrRequestPacket>(decodedRequest->packet).expiration, 1'700'000'000u);

    Enr record;
    record.setSeq(3);
    record.setIp({10, 0, 0, 1});
    record.setUdp(30303);
    record.setTcp(30303);
    record.setEth({0xfc64ec04, 0});
    record.sign(privateKey);
    EnrResponsePacket response;
    response.requestHash = encodedRequest.hash;
    response.record = record;
    auto encodedResponse = encodePacket(response, privateKey);
    auto decodedResponse = decodePacket(bcos::ref(encodedResponse.datagram));
    BOOST_REQUIRE(decodedResponse.has_value());
    auto const& r = std::get<EnrResponsePacket>(decodedResponse->packet);
    BOOST_CHECK(r.requestHash == encodedRequest.hash);
    BOOST_CHECK(r.record.verify());
    BOOST_CHECK_EQUAL(r.record.seq(), 3u);
    BOOST_CHECK(r.record.publicKey() == key.publicKey());
    BOOST_CHECK(r.record.encode() == record.encode());
}

BOOST_AUTO_TEST_CASE(rejectsCorruptPackets)
{
    rlpx::EccKeyPair key(fromHex(std::string(c_testKey)));
    PingPacket ping;
    ping.from = {ipv4(10, 0, 0, 1), 30303, 30303};
    ping.to = {ipv4(10, 0, 0, 2), 30304, 0};
    ping.expiration = 1;
    auto encoded = encodePacket(ping, bcos::ref(key.privateKey()));

    // Hash mismatch.
    auto badHash = encoded.datagram;
    badHash[0] ^= 0x01;
    BOOST_CHECK(!decodePacket(bcos::ref(badHash)).has_value());
    // Signature tampering changes the recovered sender (or fails recovery); the hash
    // covers the signature, so re-hash to isolate the signature path.
    auto badSig = encoded.datagram;
    badSig[40] ^= 0x01;
    auto rehash = rlpx::keccak256(bytesConstRef(badSig.data() + 32, badSig.size() - 32));
    std::copy(rehash.begin(), rehash.end(), badSig.begin());
    auto decodedBadSig = decodePacket(bcos::ref(badSig));
    BOOST_CHECK(!decodedBadSig.has_value() || decodedBadSig->senderPublicKey != key.publicKey());
    // Unknown type.
    auto badType = encoded.datagram;
    badType[97] = 0x09;
    auto rehash2 = rlpx::keccak256(bytesConstRef(badType.data() + 32, badType.size() - 32));
    std::copy(rehash2.begin(), rehash2.end(), badType.begin());
    BOOST_CHECK(!decodePacket(bcos::ref(badType)).has_value());
    // Too small / too large.
    bcos::bytes tiny(97, 0);
    BOOST_CHECK(!decodePacket(bcos::ref(tiny)).has_value());
    bcos::bytes huge(1281, 0);
    BOOST_CHECK(!decodePacket(bcos::ref(huge)).has_value());
    // Truncated packet-data (valid framing, list payload cut short).
    auto truncated = encoded.datagram;
    truncated.resize(truncated.size() - 3);
    auto rehash3 = rlpx::keccak256(bytesConstRef(truncated.data() + 32, truncated.size() - 32));
    std::copy(rehash3.begin(), rehash3.end(), truncated.begin());
    BOOST_CHECK(!decodePacket(bcos::ref(truncated)).has_value());
    // Expiry predicate.
    BOOST_CHECK(isExpired(99, 100));
    BOOST_CHECK(!isExpired(100, 100));
    BOOST_CHECK(!isExpired(101, 100));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
