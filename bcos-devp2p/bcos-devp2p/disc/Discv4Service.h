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
 * @file Discv4Service.h
 * @brief Node Discovery v4 over UDP: endpoint proof (ping/pong bonding), inbound
 *        Ping/FindNode/ENRRequest service, Kademlia lookups seeded from the bootnodes,
 *        bucket revalidation and the local ENR. Produces RLPx dial candidates.
 * @date 2026/11/3
 */
#pragma once

#include "../eth/ForkId.h"
#include "../rlpx/Client.h"
#include "../rlpx/Crypto.h"
#include "Discv4Wire.h"
#include "Enr.h"
#include "KademliaTable.h"
#include <bcos-utilities/Common.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace bcos::devp2p::disc
{
// A bootstrap node: the enode:// host/ports (udp = ?discport, default = the tcp port).
struct SeedNode
{
    std::string host;
    uint16_t udpPort{30303};
    uint16_t tcpPort{30303};
    bcos::bytes publicKey;  // 64 bytes
};

struct Discv4Config
{
    std::string listenIp{"0.0.0.0"};
    uint16_t udpPort{30303};  // 0 = ephemeral (tests)
    // The RLPx port advertised in our ENR (`tcp`) and in Ping.from; 0 = not advertised.
    uint16_t tcpPort{0};
    // Fixed public IPv4 for the ENR `ip` key; unset = learn it from the Pong `to`
    // endpoints our peers report (c_ipVotesRequired distinct reporters).
    std::optional<std::array<uint8_t, 4>> externalIp;
    std::vector<SeedNode> bootnodes;
    // EIP-2124 fork id published as the ENR `eth` entry (nullopt = no `eth` key).
    std::optional<eth::ForkId> forkId;

    std::chrono::milliseconds responseTimeout{1000};
    std::chrono::seconds lookupInterval{10};   // while the table is below tableTargetSize
    std::chrono::minutes refreshInterval{30};  // geth refreshInterval
    std::chrono::seconds revalidateInterval{10};
    size_t tableTargetSize{64};
};

class Discv4Service : public std::enable_shared_from_this<Discv4Service>
{
public:
    using Ptr = std::shared_ptr<Discv4Service>;
    // Called (off the lock, on the discovery thread) when a node with a TCP port becomes
    // bonded for the first time.
    using CandidateSink = std::function<void(rlpx::PeerConfig const&)>;

    struct Stats
    {
        size_t tableSize{0};
        uint64_t nodesDiscovered{0};  // distinct node ids ever added to the table
        uint64_t nodesBonded{0};      // distinct node ids that ever answered our ping
        uint64_t lookups{0};          // completed lookups
        uint64_t pongsReceived{0};
        uint64_t recordsFetched{0};  // verified ENRs received via ENRResponse
        uint64_t packetsReceived{0};
        uint64_t packetsSent{0};
    };

    static Ptr create(Discv4Config _config, rlpx::EccKeyPair _key);
    ~Discv4Service();
    Discv4Service(Discv4Service const&) = delete;
    Discv4Service& operator=(Discv4Service const&) = delete;

    // Bind the socket, spawn the I/O thread, ping the bootnodes and arm the lookup /
    // revalidation timers. Throws when the port cannot be bound.
    void start();
    // Cancel timers, close the socket and join the thread. Idempotent.
    void stop();
    bool running() const { return m_running.load(); }

    uint16_t udpPort() const { return m_boundPort; }
    bcos::h256 const& selfId() const { return m_selfId; }
    Enr localRecord() const;
    Stats stats() const;

    // Bonded nodes (pong within 12h) with a TCP port, most recently verified first.
    std::vector<rlpx::PeerConfig> candidates(size_t _max) const;
    void setCandidateSink(CandidateSink _sink);

    // Thread-safe entry points; the work runs on the discovery thread.
    // A verified record from another source (EIP-1459 DNS): tabled and pinged.
    void addRecord(Enr const& _record);
    void addSeed(SeedNode _seed);
    // Start a lookup for `_target` (nullopt = a random target).
    void lookup(std::optional<bcos::h256> _target = std::nullopt);

    // Distinct Pong reporters that must agree before an unconfigured external IP is
    // published in the ENR (geth's ipTracker wants 10; small nets never reach that).
    static constexpr size_t c_ipVotesRequired = 3;
    static constexpr size_t c_lookupConcurrency = 3;      // alpha
    static constexpr uint32_t c_maxFindNodeFailures = 5;  // geth maxFindnodeFailures
    static constexpr std::chrono::seconds c_packetExpiration{20};
    static constexpr std::chrono::seconds c_pingRepeatInterval{10};

private:
    Discv4Service(Discv4Config _config, rlpx::EccKeyPair _key);

    using Clock = NodeEntry::Clock;
    using UdpEndpoint = boost::asio::ip::udp::endpoint;

    // Outstanding request awaiting a reply from `from` of type `type`.
    struct Pending
    {
        uint64_t id{0};
        bcos::h256 from;
        PacketType type{};
        bcos::bytes matchHash;  // Pong.pingHash / ENRResponse.requestHash; empty = any
        // Pong only: the reply must come from the endpoint the ping went to (geth matches
        // replies on id + ip), so a pong is proof for exactly that endpoint.
        std::optional<UdpEndpoint> expectFrom;
        // Returns true when the request is satisfied (removed); false keeps waiting.
        std::function<bool(Packet const&)> onReply;
        std::function<void()> onTimeout;
        std::shared_ptr<boost::asio::steady_timer> timer;
    };

    struct Lookup
    {
        bcos::h256 target;
        bcos::bytes targetKey;  // the 64-byte key hashing to `target`
        struct Candidate
        {
            NodeEntry entry;
            bool queried{false};
        };
        std::map<bcos::h256, Candidate> seen;
        size_t inflight{0};
    };

    // --- I/O thread ---
    void receiveNext();
    void onDatagram(std::size_t _length);
    void handlePing(
        DecodedPacket const& _packet, UdpEndpoint const& _from, bcos::h256 const& _sender);
    void handleFindNode(
        DecodedPacket const& _packet, UdpEndpoint const& _from, bcos::h256 const& _sender);
    void handleEnrRequest(
        DecodedPacket const& _packet, UdpEndpoint const& _from, bcos::h256 const& _sender);
    bool deliverReply(
        DecodedPacket const& _packet, UdpEndpoint const& _from, bcos::h256 const& _sender);

    bcos::bytes send(UdpEndpoint const& _to, Packet const& _packet);
    void expectReply(Pending _pending);
    uint64_t nowUnix() const;
    uint64_t expiration() const;

    // Ping `_id` at `_to`; `_done(true, enrSeq)` on a pong from `_to`, `_done(false, 0)`
    // on timeout. A pong proves `_to` for `_id` (table.updateEndpoint).
    void sendPing(bcos::h256 const& _id, bcos::bytes const& _publicKey, UdpEndpoint const& _to,
        std::function<void(bool, uint64_t)> _done);
    // FindNode with endpoint proof first when the peer has none of us.
    void findNode(bcos::h256 const& _id, bcos::bytes const& _targetKey,
        std::function<void(std::vector<NeighborNode>)> _done);
    void sendFindNode(NodeEntry const& _node, bcos::bytes const& _targetKey,
        std::function<void(std::vector<NeighborNode>)> _done);
    void sendEnrRequest(NodeEntry const& _node);

    // Insert/refresh a node; ping it when it has no fresh endpoint proof.
    void addSeen(NodeEntry _entry);
    void onBonded(bcos::h256 const& _id, bcos::bytes const& _publicKey, UdpEndpoint const& _from,
        PongPacket const& _pong);
    void noteOurEndpoint(bcos::h256 const& _reporter, Endpoint const& _to);
    void rebuildLocalRecord();
    void seedBootnodes();
    std::optional<NodeEntry> resolveSeed(SeedNode const& _seed) const;

    void startLookup(bcos::h256 _target, bcos::bytes _targetKey);
    void lookupStep(std::shared_ptr<Lookup> _lookup);
    void scheduleLookupTimer(std::chrono::milliseconds _delay);
    void onLookupTimer();
    void scheduleRevalidate();
    void onRevalidate();

    // geth netutil.CheckRelayAddr: a node address may only be relayed by a sender in the
    // same scope (loopback by loopback, LAN by LAN/loopback); nullopt sender = local source.
    static bool acceptableRelayAddress(
        std::optional<boost::asio::ip::address> const& _sender, bcos::bytes const& _ip);
    static NodeEntry entryFrom(NeighborNode const& _node);
    static std::optional<UdpEndpoint> endpointOf(NodeEntry const& _node);
    rlpx::PeerConfig peerConfigOf(NodeEntry const& _node) const;

    Discv4Config m_config;
    rlpx::EccKeyPair m_key;
    bcos::h256 m_selfId;

    boost::asio::io_context m_io;
    boost::asio::ip::udp::socket m_socket;
    boost::asio::steady_timer m_lookupTimer;
    boost::asio::steady_timer m_revalidateTimer;
    std::thread m_thread;
    std::atomic_bool m_running{false};
    uint16_t m_boundPort{0};

    std::array<bcos::byte, 2048> m_receiveBuffer{};
    UdpEndpoint m_receiveFrom;

    // Guards m_table, m_localRecord, m_stats, m_bondedIds and the sink.
    mutable std::mutex m_mutex;
    KademliaTable m_table;
    Enr m_localRecord;
    uint64_t m_localSeq{0};
    std::optional<std::array<uint8_t, 4>> m_publishedIp;
    std::map<std::array<uint8_t, 4>, std::set<bcos::h256>> m_ipVotes;
    Stats m_stats;
    std::set<bcos::h256> m_everBonded;
    CandidateSink m_sink;

    // I/O-thread only.
    std::list<Pending> m_pending;
    uint64_t m_nextPendingId{1};
    size_t m_lookupRound{0};
    std::mt19937_64 m_random;
};
}  // namespace bcos::devp2p::disc
