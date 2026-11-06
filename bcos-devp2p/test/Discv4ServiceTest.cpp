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
 * @file Discv4ServiceTest.cpp
 * @brief Discv4Service tests: three loopback services bond and discover each other,
 *        an unbonded FindNode/ENRRequest is ignored, and an opt-in live probe against
 *        the Sepolia bootnodes (FISCO_DISCV4_LIVE=1).
 * @date 2026/11/3
 */
#include <bcos-devp2p/disc/Discv4Service.h>
#include <bcos-devp2p/disc/Discv4Wire.h>
#include <bcos-devp2p/disc/Enr.h>
#include <bcos-devp2p/sync/Bootnodes.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

using namespace bcos;
using namespace bcos::devp2p;
using namespace bcos::devp2p::disc;
using boost::asio::ip::udp;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(Discv4ServiceTest)

namespace
{
Discv4Config loopbackConfig(uint16_t _tcpPort)
{
    Discv4Config config;
    config.listenIp = "127.0.0.1";
    config.udpPort = 0;
    config.tcpPort = _tcpPort;
    config.responseTimeout = std::chrono::milliseconds(500);
    config.lookupInterval = std::chrono::seconds(1);
    config.revalidateInterval = std::chrono::seconds(2);
    config.forkId = eth::ForkId{0xfc64ec04, 1150000};
    return config;
}

SeedNode seedOf(Discv4Service const& _service, rlpx::EccKeyPair const& _key, uint16_t _tcp)
{
    return SeedNode{"127.0.0.1", _service.udpPort(), _tcp, _key.publicKey()};
}

bool hasPeer(std::vector<rlpx::PeerConfig> const& _peers, bcos::bytes const& _publicKey)
{
    return std::any_of(_peers.begin(), _peers.end(),
        [&](rlpx::PeerConfig const& _p) { return _p.peerPublicKey == _publicKey; });
}

template <typename Predicate>
bool waitFor(Predicate _predicate, std::chrono::seconds _timeout)
{
    auto const deadline = std::chrono::steady_clock::now() + _timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (_predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return _predicate();
}

// A hand-driven discv4 endpoint: raw UDP socket + the wire codec.
struct RawNode
{
    rlpx::EccKeyPair key;
    boost::asio::io_context io;
    RawNode() = default;
    explicit RawNode(rlpx::EccKeyPair _key) : key(std::move(_key)) {}
    udp::socket socket{io, udp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)};
    udp::endpoint lastFrom;

    uint16_t port() const { return socket.local_endpoint().port(); }

    bcos::bytes send(udp::endpoint const& _to, Packet const& _packet)
    {
        auto encoded = encodePacket(_packet, bcos::ref(key.privateKey()));
        socket.send_to(boost::asio::buffer(encoded.datagram), _to);
        return encoded.hash;
    }

    std::optional<DecodedPacket> receive(std::chrono::milliseconds _timeout)
    {
        std::array<bcos::byte, 2048> buffer{};
        std::optional<DecodedPacket> result;
        io.restart();
        socket.async_receive_from(boost::asio::buffer(buffer), lastFrom,
            [&](boost::system::error_code const& _ec, std::size_t _length) {
                if (_ec)
                {
                    return;
                }
                auto decoded = decodePacket(bytesConstRef(buffer.data(), _length));
                if (decoded)
                {
                    result = std::move(*decoded);
                }
            });
        io.run_for(_timeout);
        if (!result)
        {
            socket.cancel();
            io.run();
        }
        return result;
    }

    // Wait for a packet of `_type`, answering A's revalidation pings on the way.
    std::optional<DecodedPacket> receiveType(
        PacketType _type, udp::endpoint const& _peer, std::chrono::milliseconds _timeout)
    {
        auto const deadline = std::chrono::steady_clock::now() + _timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto packet = receive(std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()));
            if (!packet)
            {
                return std::nullopt;
            }
            if (packet->type == _type)
            {
                return packet;
            }
            if (packet->type == PacketType::Ping)
            {
                PongPacket pong;
                pong.to = Endpoint{{127, 0, 0, 1}, _peer.port(), 0};
                pong.pingHash = packet->hash;
                pong.expiration = expiry();
                send(_peer, pong);
            }
        }
        return std::nullopt;
    }

    static uint64_t expiry()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                       .count()) +
               20;
    }
};
}  // namespace

BOOST_AUTO_TEST_CASE(threeNodesBondAndDiscover)
{
    rlpx::EccKeyPair keyA;
    rlpx::EccKeyPair keyB;
    rlpx::EccKeyPair keyC;
    // A: no bootnodes. B seeds from A. C seeds from B only and must learn A through a
    // lookup (B answers C's FindNode with A).
    auto a = Discv4Service::create(loopbackConfig(30301), keyA);
    a->start();
    BOOST_REQUIRE_NE(a->udpPort(), 0);
    BOOST_CHECK(a->localRecord().verify());
    BOOST_CHECK(a->localRecord().eth().has_value());
    BOOST_CHECK_EQUAL(*a->localRecord().tcp(), 30301);
    BOOST_CHECK_EQUAL(*a->localRecord().udp(), a->udpPort());

    auto configB = loopbackConfig(30302);
    configB.bootnodes.push_back(seedOf(*a, keyA, 30301));
    auto b = Discv4Service::create(configB, keyB);
    b->start();

    auto configC = loopbackConfig(30303);
    configC.bootnodes.push_back(seedOf(*b, keyB, 30302));
    auto c = Discv4Service::create(configC, keyC);
    c->start();

    // C discovers A (never configured on C) via B.
    BOOST_CHECK(waitFor(
        [&]() { return hasPeer(c->candidates(16), keyA.publicKey()); }, std::chrono::seconds(15)));
    BOOST_CHECK(hasPeer(c->candidates(16), keyB.publicKey()));
    // A learned both (B by its ping, C by C's bonding ping / lookup).
    BOOST_CHECK(waitFor(
        [&]() {
            auto peers = a->candidates(16);
            return hasPeer(peers, keyB.publicKey()) && hasPeer(peers, keyC.publicKey());
        },
        std::chrono::seconds(10)));
    auto peersOfA = a->candidates(16);
    BOOST_CHECK(!hasPeer(peersOfA, keyA.publicKey()));  // never ourselves
    for (auto const& peer : peersOfA)
    {
        BOOST_CHECK_EQUAL(peer.host, "127.0.0.1");
        BOOST_CHECK(peer.port == 30302 || peer.port == 30303);
    }
    BOOST_CHECK_EQUAL(a->candidates(1).size(), 1u);
    // EIP-868: pongs carried the ENR seq, so records were fetched and verified.
    BOOST_CHECK(waitFor([&]() { return b->stats().recordsFetched >= 1; }, std::chrono::seconds(5)));
    auto statsA = a->stats();
    BOOST_CHECK_GE(statsA.nodesBonded, 2u);
    BOOST_CHECK_GE(statsA.tableSize, 2u);
    BOOST_CHECK_GE(statsA.pongsReceived, 2u);
    BOOST_CHECK(waitFor([&]() { return c->stats().lookups >= 1; }, std::chrono::seconds(5)));

    c->stop();
    b->stop();
    a->stop();
    BOOST_CHECK(!a->running());
    a->stop();  // idempotent
}

BOOST_AUTO_TEST_CASE(unbondedFindNodeIsIgnored)
{
    rlpx::EccKeyPair keyA;
    auto a = Discv4Service::create(loopbackConfig(30311), keyA);
    a->start();
    udp::endpoint const target(boost::asio::ip::make_address("127.0.0.1"), a->udpPort());

    RawNode raw;
    FindNodePacket findNode{raw.key.publicKey(), RawNode::expiry()};
    raw.send(target, findNode);
    BOOST_CHECK(!raw.receive(std::chrono::milliseconds(700)).has_value());
    raw.send(target, EnrRequestPacket{RawNode::expiry()});
    BOOST_CHECK(!raw.receive(std::chrono::milliseconds(700)).has_value());

    // Bond: ping A, answer A's ping back with a pong carrying its hash.
    PingPacket ping;
    ping.from = Endpoint{{127, 0, 0, 1}, raw.port(), 30399};
    ping.to = Endpoint{{127, 0, 0, 1}, a->udpPort(), 0};
    ping.expiration = RawNode::expiry();
    auto pingHash = raw.send(target, ping);
    bool gotPong = false;
    bool answeredPing = false;
    for (int i = 0; i < 4 && !(gotPong && answeredPing); ++i)
    {
        auto packet = raw.receive(std::chrono::milliseconds(1000));
        if (!packet)
        {
            break;
        }
        BOOST_CHECK(packet->senderPublicKey == keyA.publicKey());
        if (packet->type == PacketType::Pong)
        {
            auto const& pong = std::get<PongPacket>(packet->packet);
            BOOST_CHECK(pong.pingHash == pingHash);
            BOOST_CHECK_EQUAL(pong.to.ipString(), "127.0.0.1");
            BOOST_CHECK_EQUAL(pong.to.udp, raw.port());
            BOOST_CHECK_EQUAL(pong.to.tcp, 30399);
            BOOST_CHECK_NE(pong.enrSeq, 0u);
            gotPong = true;
        }
        else if (packet->type == PacketType::Ping)
        {
            PongPacket pong;
            pong.to = Endpoint{{127, 0, 0, 1}, a->udpPort(), 0};
            pong.pingHash = packet->hash;
            pong.expiration = RawNode::expiry();
            raw.send(target, pong);
            answeredPing = true;
        }
    }
    BOOST_CHECK(gotPong);
    BOOST_CHECK(answeredPing);
    BOOST_CHECK(waitFor([&]() { return a->stats().nodesBonded >= 1; }, std::chrono::seconds(3)));

    // Bonded now: FindNode is answered (our own entry is the closest node A knows) and
    // ENRRequest returns A's signed record.
    raw.send(target, findNode);
    auto neighbors =
        raw.receiveType(PacketType::Neighbors, target, std::chrono::milliseconds(2000));
    BOOST_REQUIRE(neighbors.has_value());
    auto const& nodes = std::get<NeighborsPacket>(neighbors->packet).nodes;
    BOOST_REQUIRE_EQUAL(nodes.size(), 1u);
    BOOST_CHECK(nodes[0].id == raw.key.publicKey());
    BOOST_CHECK_EQUAL(nodes[0].endpoint.udp, raw.port());
    BOOST_CHECK_EQUAL(nodes[0].endpoint.tcp, 30399);

    auto requestHash = raw.send(target, EnrRequestPacket{RawNode::expiry()});
    auto response =
        raw.receiveType(PacketType::EnrResponse, target, std::chrono::milliseconds(2000));
    BOOST_REQUIRE(response.has_value());
    auto const& enrResponse = std::get<EnrResponsePacket>(response->packet);
    BOOST_CHECK(enrResponse.requestHash == requestHash);
    BOOST_CHECK(enrResponse.record.verify());
    BOOST_CHECK(enrResponse.record.publicKey() == keyA.publicKey());
    BOOST_CHECK((enrResponse.record.eth() == eth::ForkId{0xfc64ec04, 1150000}));
    BOOST_CHECK_EQUAL(*enrResponse.record.tcp(), 30311);
    // We announced tcp 30399 from a proven endpoint: A hands us out as a candidate.
    auto peers = a->candidates(4);
    BOOST_REQUIRE_EQUAL(peers.size(), 1u);
    BOOST_CHECK_EQUAL(peers[0].port, 30399);
    BOOST_CHECK(peers[0].peerPublicKey == raw.key.publicKey());

    // An expired FindNode is dropped even though we are bonded.
    FindNodePacket stale{raw.key.publicKey(), 1};
    raw.send(target, stale);
    BOOST_CHECK(!raw.receive(std::chrono::milliseconds(500)).has_value());
    a->stop();
}

BOOST_AUTO_TEST_CASE(pingAloneDoesNotTableTheSender)
{
    rlpx::EccKeyPair keyA;
    auto a = Discv4Service::create(loopbackConfig(30321), keyA);
    a->start();
    udp::endpoint const target(boost::asio::ip::make_address("127.0.0.1"), a->udpPort());
    RawNode raw;
    PingPacket ping;
    ping.from = Endpoint{{127, 0, 0, 1}, raw.port(), 30399};
    ping.to = Endpoint{{127, 0, 0, 1}, a->udpPort(), 0};
    ping.expiration = RawNode::expiry();
    raw.send(target, ping);
    // A answers with a pong and (lacking a proof of us) its own ping — which we leave
    // unanswered: the source address alone must not create a table entry.
    bool gotPong = false;
    bool gotPing = false;
    for (int i = 0; i < 2; ++i)
    {
        auto packet = raw.receive(std::chrono::milliseconds(1000));
        if (!packet)
        {
            break;
        }
        gotPong |= packet->type == PacketType::Pong;
        gotPing |= packet->type == PacketType::Ping;
    }
    BOOST_CHECK(gotPong);
    BOOST_CHECK(gotPing);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));  // past the response timeout
    auto stats = a->stats();
    BOOST_CHECK_EQUAL(stats.tableSize, 0u);
    BOOST_CHECK_EQUAL(stats.nodesDiscovered, 0u);
    BOOST_CHECK_EQUAL(stats.nodesBonded, 0u);
    BOOST_CHECK(a->candidates(4).empty());
    // And a FindNode from that unverified source is still ignored.
    raw.send(target, FindNodePacket{raw.key.publicKey(), RawNode::expiry()});
    BOOST_CHECK(!raw.receive(std::chrono::milliseconds(600)).has_value());
    a->stop();
}

BOOST_AUTO_TEST_CASE(pingBackIsThrottledPerEndpoint)
{
    rlpx::EccKeyPair keyA;
    auto a = Discv4Service::create(loopbackConfig(30325), keyA);
    a->start();
    udp::endpoint const target(boost::asio::ip::make_address("127.0.0.1"), a->udpPort());
    RawNode raw;
    PingPacket ping;
    ping.from = Endpoint{{127, 0, 0, 1}, raw.port(), 30399};
    ping.to = Endpoint{{127, 0, 0, 1}, a->udpPort(), 0};
    ping.expiration = RawNode::expiry();
    // A spoofed source could repeat this at will: every ping earns a pong, but only the
    // first one earns a ping back at that endpoint.
    for (int i = 0; i < 5; ++i)
    {
        raw.send(target, ping);
    }
    size_t pongs = 0;
    size_t pings = 0;
    while (auto packet = raw.receive(std::chrono::milliseconds(500)))
    {
        pongs += packet->type == PacketType::Pong ? 1 : 0;
        pings += packet->type == PacketType::Ping ? 1 : 0;
    }
    BOOST_CHECK_EQUAL(pongs, 5u);
    BOOST_CHECK_EQUAL(pings, 1u);
    a->stop();
}

BOOST_AUTO_TEST_CASE(bondedIdFromOtherSourcePortIsIgnored)
{
    rlpx::EccKeyPair keyA;
    auto a = Discv4Service::create(loopbackConfig(30331), keyA);
    a->start();
    udp::endpoint const target(boost::asio::ip::make_address("127.0.0.1"), a->udpPort());
    RawNode raw;
    // Bond from raw's socket.
    PingPacket ping;
    ping.from = Endpoint{{127, 0, 0, 1}, raw.port(), 30399};
    ping.to = Endpoint{{127, 0, 0, 1}, a->udpPort(), 0};
    ping.expiration = RawNode::expiry();
    raw.send(target, ping);
    for (int i = 0; i < 2; ++i)
    {
        auto packet = raw.receive(std::chrono::milliseconds(1000));
        if (packet && packet->type == PacketType::Ping)
        {
            PongPacket pong;
            pong.to = Endpoint{{127, 0, 0, 1}, a->udpPort(), 0};
            pong.pingHash = packet->hash;
            pong.expiration = RawNode::expiry();
            raw.send(target, pong);
        }
    }
    BOOST_REQUIRE(waitFor([&]() { return a->stats().nodesBonded >= 1; }, std::chrono::seconds(3)));
    BOOST_CHECK_EQUAL(a->stats().tableSize, 1u);

    // The same identity from another UDP port: no Neighbors, no ENR (geth checkBond is
    // keyed by id + ip; ours by id + ip + port).
    RawNode replay(raw.key);
    BOOST_REQUIRE_NE(replay.port(), raw.port());
    replay.send(target, FindNodePacket{raw.key.publicKey(), RawNode::expiry()});
    BOOST_CHECK(!replay.receive(std::chrono::milliseconds(700)).has_value());
    replay.send(target, EnrRequestPacket{RawNode::expiry()});
    BOOST_CHECK(!replay.receive(std::chrono::milliseconds(700)).has_value());
    // The genuine endpoint is still served (A's revalidation pings are answered on the
    // way, see receiveType).
    raw.send(target, FindNodePacket{raw.key.publicKey(), RawNode::expiry()});
    auto neighbors =
        raw.receiveType(PacketType::Neighbors, target, std::chrono::milliseconds(2000));
    BOOST_REQUIRE(neighbors.has_value());
    // The replayed ping from the other port did not move the stored endpoint either.
    auto peers = a->candidates(4);
    BOOST_REQUIRE_EQUAL(peers.size(), 1u);
    BOOST_CHECK_EQUAL(peers[0].port, 30399);
    a->stop();
}

// Opt-in live probe: FISCO_DISCV4_LIVE=1 seeds from go-ethereum params/bootnodes.go
// (commit 5dcdd05159ac162239e6c4ba2e7de03fc40b6581) and runs for 60 seconds.
BOOST_AUTO_TEST_CASE(liveSepoliaProbe)
{
    if (std::getenv("FISCO_DISCV4_LIVE") == nullptr)
    {
        return;
    }
    static char const* const c_sepoliaBootnodes[] = {
        "enr:-KG4QHn1Os72NN5Z44Ehf9CHPmN490GBphTFirfk33pFuWnkY9UNrQMfHkuCw6a-"
        "ykeRhGKrPL5ppMXiZQchgPr"
        "CDJQBgmlkgnY0gmlwhNRj2kKDaXA2kCoAHKALAA0CAAAAAAAAAF6Jc2VjcDI1NmsxoQJK_ye9jx9mela-ME_"
        "Kt5e017"
        "Ywv3hYH_5rwNhIUrtzNoN1ZHCCTriEdWRwNoJOuA",
        "enr:-KG4QCpiRltiyX7LgozCkQ8XBrcDzejBXbE7YRUQFmIEgv46SC8B4CJLcOO3O2bH-_u-B242GTzO_hQPhv3rp"
        "yvAHXMBgmlkgnY0gmlwhIHUpj2DaXA2kCYEqIAABAHQAAAAA2GdUACJc2VjcDI1NmsxoQNmVWXve5c0uvsn_"
        "agjTKQ8"
        "pR6gl9DynZyTQNruBDfJ4YN1ZHCCdsOEdWRwNoJ2ww",
        "enr:-KG4QJUWvc8qvCUr7x-qCFWee6HPLgmRUfVn-M_yqXD-sjSoAalKCV3NgUhmlqozjTJIj_nPr7J_uT8WJwbWi"
        "Ix3Cv4BgmlkgnY0gmlwhJB-_BiDaXA2kCQAYYABAADQAAAAAYEgYAGJc2VjcDI1NmsxoQKOQetrA-"
        "97TELUzuGegVD1"
        "_cHKKNnjOmJ9CYdeSTor7YN1ZHCCdsOEdWRwNoJ2ww",
        "enr:-KG4QIBsIiST0JBBvgSNxvHQOeH5PkRgWeufahVXYQQWj7GLYPFDsviuQ6qU-3Xx4qnARaZiI-qqVRyw_2qqm"
        "OKRY1UBgmlkgnY0gmlwhLKc14yDaXA2kCoBBP8A9DxKAAAAAAAAAAGJc2VjcDI1NmsxoQKx4n3wy0KtwnuZCHmlxMm"
        "c"
        "4b0VvfC4Ka_f3sUPvjUq1oN1ZHCCdsOEdWRwNoJ2ww",
        "enr:-KG4QPM5C6wuTxu5yk4jkI9Mm64iSUKMPNt8y8kmoFzOlYrnYV4SBeZK7UiS4YWrkX4y2FD9_eH7xiPGvLQ-n"
        "XoXIcYBgmlkgnY0gmlwhAXfXlGDaXA2kCoBBP8C8BytAAAAAAAAAAGJc2VjcDI1NmsxoQIC3FMD8Si9DIBVofsSbJk"
        "p"
        "vXTUAfyi8q66XXQgjRhUsIN1ZHCCdsOEdWRwNoJ2ww",
        "enode://"
        "4e5e92199ee224a01932a377160aa432f31d0b351f84ab413a8e0a42f4f36476f8fb1cbe914af0d9aef0"
        "d51665c214cf653c651c4bbd9d5550a934f241f1682b@138.197.51.181:30303",
        "enode://"
        "143e11fb766781d22d92a2e33f8f104cddae4411a122295ed1fdb6638de96a6ce65f5b7c964ba3763bba"
        "27961738fef7d3ecc739268f3e5e771fb4c87b6234ba@146.190.1.103:30303",
        "enode://"
        "8b61dc2d06c3f96fddcbebb0efb29d60d3598650275dc469c22229d3e5620369b0d3dedafd929835fe7f"
        "489618f19f456fe7c0df572bf2d914a9f4e006f783a9@170.64.250.88:30303",
        "enode://"
        "10d62eff032205fcef19497f35ca8477bea0eadfff6d769a147e895d8b2b8f8ae6341630c645c30f5df6"
        "e67547c03494ced3d9c5764e8622a26587b083b028e8@139.59.49.206:30303",
        "enode://"
        "9e9492e2e8836114cc75f5b929784f4f46c324ad01daf87d956f98b3b6c5fcba95524d6e5cf9861dc96a"
        "2c8a171ea7105bb554a197455058de185fa870970c7c@138.68.123.152:30303",
    };
    Discv4Config config;
    config.listenIp = "0.0.0.0";
    config.udpPort = 0;
    config.tcpPort = 30303;
    config.lookupInterval = std::chrono::seconds(5);
    // Sepolia Prague fork id is irrelevant for discovery itself; publish none.
    for (auto const* text : c_sepoliaBootnodes)
    {
        auto seed = sync::parseBootnode(text);
        config.bootnodes.push_back(SeedNode{seed.host, seed.udpPort, seed.tcpPort, seed.publicKey});
    }
    rlpx::EccKeyPair key;
    auto service = Discv4Service::create(config, key);
    service->start();
    for (int second = 10; second <= 60; second += 10)
    {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        auto stats = service->stats();
        std::cout << "[live] t=" << second << "s table=" << stats.tableSize
                  << " discovered=" << stats.nodesDiscovered << " bonded=" << stats.nodesBonded
                  << " records=" << stats.recordsFetched << " lookups=" << stats.lookups
                  << " pongs=" << stats.pongsReceived << " rx=" << stats.packetsReceived
                  << " tx=" << stats.packetsSent
                  << " candidates=" << service->candidates(1000).size() << std::endl;
    }
    auto stats = service->stats();
    std::cout << "[live] final enr=" << service->localRecord().toText() << std::endl;
    service->stop();
    BOOST_CHECK_MESSAGE(stats.pongsReceived > 0,
        "no pong in 60s: UDP egress blocked or bootnodes unreachable (rx=" << stats.packetsReceived
                                                                           << ")");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
