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
 * @file Discv4Service.cpp
 * @brief Node Discovery v4 service.
 * @date 2026/11/3
 */
#include "Discv4Service.h"
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/post.hpp>
#include <algorithm>
#include <stdexcept>

#define DISCV4_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("discv4")

namespace bcos::devp2p::disc
{
using boost::asio::ip::udp;

namespace
{
bcos::bytes ipBytes(boost::asio::ip::address const& _address)
{
    if (_address.is_v4())
    {
        auto raw = _address.to_v4().to_bytes();
        return bcos::bytes(raw.begin(), raw.end());
    }
    auto v6 = _address.to_v6();
    if (v6.is_v4_mapped())
    {
        auto raw = boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, v6).to_bytes();
        return bcos::bytes(raw.begin(), raw.end());
    }
    auto raw = v6.to_bytes();
    return bcos::bytes(raw.begin(), raw.end());
}

std::optional<boost::asio::ip::address> addressOf(bcos::bytes const& _ip)
{
    if (_ip.size() == 4)
    {
        boost::asio::ip::address_v4::bytes_type raw{};
        std::copy(_ip.begin(), _ip.end(), raw.begin());
        return boost::asio::ip::address(boost::asio::ip::address_v4(raw));
    }
    if (_ip.size() == 16)
    {
        boost::asio::ip::address_v6::bytes_type raw{};
        std::copy(_ip.begin(), _ip.end(), raw.begin());
        return boost::asio::ip::address(boost::asio::ip::address_v6(raw));
    }
    return std::nullopt;
}

bool isPrivateV4(boost::asio::ip::address_v4 const& _a)
{
    auto raw = _a.to_bytes();
    return raw[0] == 10 || (raw[0] == 172 && raw[1] >= 16 && raw[1] <= 31) ||
           (raw[0] == 192 && raw[1] == 168) || (raw[0] == 169 && raw[1] == 254) ||
           (raw[0] == 100 && raw[1] >= 64 && raw[1] <= 127);  // CGNAT
}

uint64_t nowMilliseconds()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string shortId(bcos::h256 const& _id)
{
    return _id.hex().substr(0, 16);
}
}  // namespace

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------
Discv4Service::Discv4Service(Discv4Config _config, rlpx::EccKeyPair _key)
  : m_config(std::move(_config)),
    m_key(std::move(_key)),
    m_selfId(KademliaTable::nodeIdOf(bcos::ref(m_key.publicKey()))),
    m_socket(m_io),
    m_lookupTimer(m_io),
    m_revalidateTimer(m_io),
    m_table(m_selfId),
    m_random(std::random_device{}())
{}

Discv4Service::Ptr Discv4Service::create(Discv4Config _config, rlpx::EccKeyPair _key)
{
    return Ptr(new Discv4Service(std::move(_config), std::move(_key)));
}

Discv4Service::~Discv4Service()
{
    stop();
}

void Discv4Service::start()
{
    if (m_running.exchange(true))
    {
        return;
    }
    try
    {
        udp::endpoint local(boost::asio::ip::make_address(m_config.listenIp), m_config.udpPort);
        if (!local.address().is_v4())
        {
            throw std::invalid_argument("discv4: listen_ip must be IPv4");
        }
        m_socket.open(udp::v4());
        m_socket.bind(local);
        m_boundPort = m_socket.local_endpoint().port();
    }
    catch (...)
    {
        boost::system::error_code ignored;
        m_socket.close(ignored);
        m_running.store(false);
        throw;
    }
    {
        std::lock_guard lock(m_mutex);
        // geth enode.DB.localSeq: the first seq is the current time in milliseconds so a
        // wiped node never reuses a sequence number its peers already saw.
        m_localSeq = nowMilliseconds();
        rebuildLocalRecord();
    }
    DISCV4_LOG(INFO) << LOG_DESC("discovery started") << LOG_KV("udpPort", m_boundPort)
                     << LOG_KV("nodeId", m_selfId.hex())
                     << LOG_KV("bootnodes", m_config.bootnodes.size())
                     << LOG_KV("enr", m_localRecord.toText());
    auto self = shared_from_this();
    m_io.restart();
    boost::asio::post(m_io, [self]() {
        self->receiveNext();
        self->seedBootnodes();
        self->scheduleLookupTimer(std::chrono::seconds(1));
        self->scheduleRevalidate();
    });
    m_thread = std::thread([self]() {
        try
        {
            self->m_io.run();
        }
        catch (std::exception const& e)
        {
            DISCV4_LOG(ERROR) << LOG_DESC("discovery thread exited on exception")
                              << LOG_KV("error", e.what());
        }
    });
}

void Discv4Service::stop()
{
    bool const wasRunning = m_running.exchange(false);
    if (wasRunning)
    {
        auto self = shared_from_this();
        boost::asio::post(m_io, [self]() {
            boost::system::error_code ignored;
            self->m_lookupTimer.cancel();
            self->m_revalidateTimer.cancel();
            for (auto& pending : self->m_pending)
            {
                pending.timer->cancel();
            }
            self->m_pending.clear();
            self->m_socket.close(ignored);
        });
    }
    if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id())
    {
        m_thread.join();
    }
    if (wasRunning)
    {
        auto stats = this->stats();
        DISCV4_LOG(INFO) << LOG_DESC("discovery stopped") << LOG_KV("table", stats.tableSize)
                         << LOG_KV("discovered", stats.nodesDiscovered)
                         << LOG_KV("bonded", stats.nodesBonded)
                         << LOG_KV("records", stats.recordsFetched)
                         << LOG_KV("lookups", stats.lookups);
    }
}

// ---------------------------------------------------------------------------
// public read side
// ---------------------------------------------------------------------------
Enr Discv4Service::localRecord() const
{
    std::lock_guard lock(m_mutex);
    return m_localRecord;
}

Discv4Service::Stats Discv4Service::stats() const
{
    std::lock_guard lock(m_mutex);
    auto stats = m_stats;
    stats.tableSize = m_table.size();
    return stats;
}

rlpx::PeerConfig Discv4Service::peerConfigOf(NodeEntry const& _node) const
{
    rlpx::PeerConfig config;
    config.host = Endpoint{_node.ip, _node.udp, _node.tcp}.ipString();
    config.port = _node.tcp;
    config.peerPublicKey = _node.publicKey;
    return config;
}

std::vector<rlpx::PeerConfig> Discv4Service::candidates(size_t _max) const
{
    auto const now = Clock::now();
    std::vector<NodeEntry> bonded;
    {
        std::lock_guard lock(m_mutex);
        for (auto const& entry : m_table.all())
        {
            if (entry.bonded(now) && entry.tcp != 0 && entry.ip.size() == 4)
            {
                bonded.push_back(entry);
            }
        }
    }
    std::sort(bonded.begin(), bonded.end(), [](NodeEntry const& _a, NodeEntry const& _b) {
        return _a.lastPongReceived > _b.lastPongReceived;
    });
    if (bonded.size() > _max)
    {
        bonded.resize(_max);
    }
    std::vector<rlpx::PeerConfig> out;
    out.reserve(bonded.size());
    for (auto const& entry : bonded)
    {
        out.push_back(peerConfigOf(entry));
    }
    return out;
}

void Discv4Service::setCandidateSink(CandidateSink _sink)
{
    std::lock_guard lock(m_mutex);
    m_sink = std::move(_sink);
}

void Discv4Service::addRecord(Enr const& _record)
{
    if (!_record.verify())
    {
        return;
    }
    auto key = _record.publicKey();
    auto ip = _record.ip();
    auto udpPort = _record.udp();
    if (!key || !ip || !udpPort || *udpPort == 0)
    {
        return;  // no reachable IPv4 UDP endpoint: nothing to bond with
    }
    NodeEntry entry;
    entry.publicKey = *key;
    entry.id = KademliaTable::nodeIdOf(bcos::ref(*key));
    entry.ip.assign(ip->begin(), ip->end());
    entry.udp = *udpPort;
    entry.tcp = _record.tcp().value_or(0);
    entry.enrSeq = _record.seq();
    entry.record = _record;
    if (!acceptableRelayAddress(std::nullopt, entry.ip))
    {
        return;
    }
    auto self = shared_from_this();
    boost::asio::post(m_io, [self, entry = std::move(entry)]() mutable {
        if (self->m_running.load())
        {
            self->addSeen(std::move(entry));
        }
    });
}

void Discv4Service::addSeed(SeedNode _seed)
{
    auto self = shared_from_this();
    boost::asio::post(m_io, [self, seed = std::move(_seed)]() {
        if (!self->m_running.load())
        {
            return;
        }
        if (auto entry = self->resolveSeed(seed))
        {
            self->addSeen(std::move(*entry));
        }
    });
}

void Discv4Service::lookup(std::optional<bcos::h256> _target)
{
    auto self = shared_from_this();
    boost::asio::post(m_io, [self, _target]() {
        if (!self->m_running.load())
        {
            return;
        }
        if (_target)
        {
            // Without the preimage key we cannot send FindNode for an arbitrary hash;
            // callers passing an id must be looking themselves up.
            self->startLookup(
                *_target, _target == self->m_selfId ? self->m_key.publicKey() : bcos::bytes(64, 0));
            return;
        }
        rlpx::EccKeyPair randomKey;
        self->startLookup(
            KademliaTable::nodeIdOf(bcos::ref(randomKey.publicKey())), randomKey.publicKey());
    });
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
uint64_t Discv4Service::nowUnix() const
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
            .count());
}

uint64_t Discv4Service::expiration() const
{
    return nowUnix() + static_cast<uint64_t>(c_packetExpiration.count());
}

bool Discv4Service::acceptableRelayAddress(
    std::optional<boost::asio::ip::address> const& _sender, bcos::bytes const& _ip)
{
    if (_ip.size() != 4)
    {
        return false;  // IPv4-only socket
    }
    boost::asio::ip::address_v4::bytes_type raw{};
    std::copy(_ip.begin(), _ip.end(), raw.begin());
    boost::asio::ip::address_v4 address(raw);
    if (address.is_unspecified() || address.is_multicast() ||
        address == boost::asio::ip::address_v4::broadcast())
    {
        return false;
    }
    bool senderLoopback = true;
    bool senderPrivate = true;
    if (_sender)
    {
        if (_sender->is_v4())
        {
            senderLoopback = _sender->to_v4().is_loopback();
            senderPrivate = senderLoopback || isPrivateV4(_sender->to_v4());
        }
        else
        {
            senderLoopback = _sender->to_v6().is_loopback();
            senderPrivate = senderLoopback || _sender->to_v6().is_link_local();
        }
    }
    if (address.is_loopback())
    {
        return senderLoopback;
    }
    if (isPrivateV4(address))
    {
        return senderPrivate;
    }
    return true;
}

NodeEntry Discv4Service::entryFrom(NeighborNode const& _node)
{
    NodeEntry entry;
    entry.publicKey = _node.id;
    entry.id = KademliaTable::nodeIdOf(bcos::ref(_node.id));
    entry.ip = _node.endpoint.ip;
    entry.udp = _node.endpoint.udp;
    entry.tcp = _node.endpoint.tcp;
    return entry;
}

std::optional<Discv4Service::UdpEndpoint> Discv4Service::endpointOf(NodeEntry const& _node)
{
    auto address = addressOf(_node.ip);
    if (!address || !address->is_v4() || _node.udp == 0)
    {
        return std::nullopt;
    }
    return UdpEndpoint(*address, _node.udp);
}

std::optional<NodeEntry> Discv4Service::resolveSeed(SeedNode const& _seed) const
{
    if (_seed.publicKey.size() != 64)
    {
        DISCV4_LOG(WARNING) << LOG_DESC("bootnode has no 64-byte public key, skipped")
                            << LOG_KV("host", _seed.host);
        return std::nullopt;
    }
    boost::system::error_code ec;
    auto address = boost::asio::ip::make_address(_seed.host, ec);
    if (ec)
    {
        try
        {
            udp::resolver resolver(const_cast<boost::asio::io_context&>(m_io));
            auto results = resolver.resolve(udp::v4(), _seed.host, "");
            if (results.empty())
            {
                throw std::runtime_error("no address");
            }
            address = results.begin()->endpoint().address();
        }
        catch (std::exception const& e)
        {
            DISCV4_LOG(WARNING) << LOG_DESC("cannot resolve bootnode host, skipped")
                                << LOG_KV("host", _seed.host) << LOG_KV("error", e.what());
            return std::nullopt;
        }
    }
    if (!address.is_v4())
    {
        DISCV4_LOG(WARNING) << LOG_DESC("bootnode is IPv6-only, skipped")
                            << LOG_KV("host", _seed.host);
        return std::nullopt;
    }
    NodeEntry entry;
    entry.publicKey = _seed.publicKey;
    entry.id = KademliaTable::nodeIdOf(bcos::ref(_seed.publicKey));
    entry.ip = ipBytes(address);
    entry.udp = _seed.udpPort;
    entry.tcp = _seed.tcpPort;
    return entry;
}

void Discv4Service::seedBootnodes()
{
    for (auto const& seed : m_config.bootnodes)
    {
        if (auto entry = resolveSeed(seed))
        {
            addSeen(std::move(*entry));
        }
    }
}

void Discv4Service::rebuildLocalRecord()
{
    Enr record;
    record.setSeq(m_localSeq);
    auto ip = m_config.externalIp ? m_config.externalIp : m_publishedIp;
    if (ip)
    {
        record.setIp(*ip);
    }
    record.setUdp(m_boundPort);
    if (m_config.tcpPort != 0)
    {
        record.setTcp(m_config.tcpPort);
    }
    if (m_config.forkId)
    {
        record.setEth(*m_config.forkId);
    }
    record.sign(bcos::ref(m_key.privateKey()));
    m_localRecord = std::move(record);
}

// ---------------------------------------------------------------------------
// socket
// ---------------------------------------------------------------------------
void Discv4Service::receiveNext()
{
    if (!m_running.load())
    {
        return;
    }
    auto self = shared_from_this();
    m_socket.async_receive_from(boost::asio::buffer(m_receiveBuffer), m_receiveFrom,
        [self](boost::system::error_code const& _ec, std::size_t _length) {
            if (_ec)
            {
                if (_ec != boost::asio::error::operation_aborted && self->m_running.load())
                {
                    DISCV4_LOG(TRACE)
                        << LOG_DESC("receive error") << LOG_KV("error", _ec.message());
                    self->receiveNext();
                }
                return;
            }
            try
            {
                self->onDatagram(_length);
            }
            catch (std::exception const& e)
            {
                DISCV4_LOG(WARNING)
                    << LOG_DESC("packet handler threw") << LOG_KV("error", e.what());
            }
            self->receiveNext();
        });
}

bcos::bytes Discv4Service::send(UdpEndpoint const& _to, Packet const& _packet)
{
    auto encoded = encodePacket(_packet, bcos::ref(m_key.privateKey()));
    boost::system::error_code ec;
    m_socket.send_to(boost::asio::buffer(encoded.datagram), _to, 0, ec);
    {
        std::lock_guard lock(m_mutex);
        ++m_stats.packetsSent;
    }
    if (ec)
    {
        DISCV4_LOG(TRACE) << LOG_DESC("send failed") << LOG_KV("to", _to.address().to_string())
                          << LOG_KV("type", packetName(packetType(_packet)))
                          << LOG_KV("error", ec.message());
    }
    return std::move(encoded.hash);
}

void Discv4Service::onDatagram(std::size_t _length)
{
    auto const from = m_receiveFrom;
    auto decoded = decodePacket(bytesConstRef(m_receiveBuffer.data(), _length));
    if (!decoded)
    {
        DISCV4_LOG(TRACE) << LOG_DESC("dropped packet")
                          << LOG_KV("from", from.address().to_string())
                          << LOG_KV("reason", decoded.error().message);
        return;
    }
    {
        std::lock_guard lock(m_mutex);
        ++m_stats.packetsReceived;
    }
    auto const sender = KademliaTable::nodeIdOf(bcos::ref(decoded->senderPublicKey));
    if (sender == m_selfId)
    {
        return;
    }
    auto const now = nowUnix();
    auto expired = [&](uint64_t _expiration) {
        if (isExpired(_expiration, now))
        {
            DISCV4_LOG(TRACE) << LOG_DESC("expired packet")
                              << LOG_KV("type", packetName(decoded->type))
                              << LOG_KV("from", from.address().to_string());
            return true;
        }
        return false;
    };
    switch (decoded->type)
    {
    case PacketType::Ping:
        if (!expired(std::get<PingPacket>(decoded->packet).expiration))
        {
            handlePing(*decoded, from, sender);
        }
        break;
    case PacketType::Pong:
        if (!expired(std::get<PongPacket>(decoded->packet).expiration))
        {
            deliverReply(*decoded, from, sender);
        }
        break;
    case PacketType::FindNode:
        if (!expired(std::get<FindNodePacket>(decoded->packet).expiration))
        {
            handleFindNode(*decoded, from, sender);
        }
        break;
    case PacketType::Neighbors:
        if (!expired(std::get<NeighborsPacket>(decoded->packet).expiration))
        {
            deliverReply(*decoded, from, sender);
        }
        break;
    case PacketType::EnrRequest:
        if (!expired(std::get<EnrRequestPacket>(decoded->packet).expiration))
        {
            handleEnrRequest(*decoded, from, sender);
        }
        break;
    case PacketType::EnrResponse:
        deliverReply(*decoded, from, sender);
        break;
    }
}

// ---------------------------------------------------------------------------
// inbound requests
// ---------------------------------------------------------------------------
void Discv4Service::handlePing(
    DecodedPacket const& _packet, UdpEndpoint const& _from, bcos::h256 const& _sender)
{
    auto const& ping = std::get<PingPacket>(_packet.packet);
    PongPacket pong;
    // geth: the pong's `to` mirrors the UDP envelope so the peer learns its external
    // address; the TCP port is whatever the peer announced.
    pong.to = Endpoint{ipBytes(_from.address()), _from.port(), ping.from.tcp};
    pong.pingHash = _packet.hash;
    pong.expiration = expiration();
    {
        std::lock_guard lock(m_mutex);
        pong.enrSeq = m_localSeq;
    }
    send(_from, pong);

    // A ping is unverified (the source address can be spoofed): it never inserts or
    // moves a table entry. A node already tabled at exactly this endpoint gets its
    // lastPingReceived/tcp refreshed; anyone else is pinged at the envelope address and
    // enters the table only when the pong proves it (onBonded).
    auto const now = Clock::now();
    auto const fromIp = ipBytes(_from.address());
    bool shouldPing = true;
    {
        std::lock_guard lock(m_mutex);
        auto* node = m_table.find(_sender);
        if (node != nullptr)
        {
            if (node->ip == fromIp && node->udp == _from.port())
            {
                node->lastPingReceived = now;  // from the proven endpoint: it holds our pong
                node->enrSeq = std::max(node->enrSeq, ping.enrSeq);
                if (ping.from.tcp != 0)
                {
                    node->tcp = ping.from.tcp;
                }
                m_table.touch(_sender);
                // discv4.md: ping back only when we hold no fresh proof of the sender.
                shouldPing = !node->bonded(now);
            }
            if (shouldPing && node->lastPingSent != Clock::time_point{} &&
                now - node->lastPingSent < c_pingRepeatInterval)
            {
                shouldPing = false;
            }
        }
    }
    if (shouldPing)
    {
        sendPing(_sender, _packet.senderPublicKey, _from, ping.from.tcp, [](bool, uint64_t) {});
    }
}

void Discv4Service::handleFindNode(
    DecodedPacket const& _packet, UdpEndpoint const& _from, bcos::h256 const& _sender)
{
    auto const& request = std::get<FindNodePacket>(_packet.packet);
    auto const now = Clock::now();
    std::vector<NodeEntry> closest;
    {
        std::lock_guard lock(m_mutex);
        // geth checkBond is keyed by id AND ip: the proof belongs to the endpoint that
        // answered our ping, so a request from any other source is ignored (no
        // Neighbors reflection toward a spoofed address).
        auto const* node = m_table.find(_sender);
        if (node == nullptr || !node->bonded(now) || node->ip != ipBytes(_from.address()) ||
            node->udp != _from.port())
        {
            DISCV4_LOG(TRACE) << LOG_DESC("findnode from unbonded endpoint ignored")
                              << LOG_KV("from", _from.address().to_string())
                              << LOG_KV("id", shortId(_sender));
            return;
        }
        closest = m_table.closest(
            KademliaTable::nodeIdOf(bcos::ref(request.target)), KademliaTable::c_bucketSize);
    }
    NeighborsPacket reply;
    reply.expiration = expiration();
    bool sent = false;
    for (auto const& entry : closest)
    {
        // Only nodes whose endpoint we have verified ourselves are relayed.
        if (!entry.bonded(now) || !acceptableRelayAddress(_from.address(), entry.ip))
        {
            continue;
        }
        reply.nodes.push_back(
            NeighborNode{Endpoint{entry.ip, entry.udp, entry.tcp}, entry.publicKey});
        if (reply.nodes.size() == c_maxNeighbors)
        {
            send(_from, reply);
            sent = true;
            reply.nodes.clear();
        }
    }
    if (!reply.nodes.empty() || !sent)
    {
        send(_from, reply);
    }
}

void Discv4Service::handleEnrRequest(
    DecodedPacket const& _packet, UdpEndpoint const& _from, bcos::h256 const& _sender)
{
    EnrResponsePacket reply;
    {
        std::lock_guard lock(m_mutex);
        auto const* node = m_table.find(_sender);
        if (node == nullptr || !node->bonded(Clock::now()) ||
            node->ip != ipBytes(_from.address()) || node->udp != _from.port())
        {
            DISCV4_LOG(TRACE) << LOG_DESC("enrrequest from unbonded endpoint ignored")
                              << LOG_KV("id", shortId(_sender));
            return;
        }
        reply.record = m_localRecord;
    }
    reply.requestHash = _packet.hash;
    send(_from, reply);
}

// ---------------------------------------------------------------------------
// request/reply matching
// ---------------------------------------------------------------------------
void Discv4Service::expectReply(Pending _pending)
{
    _pending.id = m_nextPendingId++;
    _pending.timer = std::make_shared<boost::asio::steady_timer>(m_io);
    _pending.timer->expires_after(m_config.responseTimeout);
    std::weak_ptr<Discv4Service> weak = shared_from_this();
    auto const id = _pending.id;
    _pending.timer->async_wait([weak, id](boost::system::error_code const& _ec) {
        if (_ec)
        {
            return;
        }
        auto self = weak.lock();
        if (!self)
        {
            return;
        }
        auto it = std::find_if(self->m_pending.begin(), self->m_pending.end(),
            [id](Pending const& _p) { return _p.id == id; });
        if (it == self->m_pending.end())
        {
            return;
        }
        auto onTimeout = std::move(it->onTimeout);
        self->m_pending.erase(it);
        if (onTimeout)
        {
            onTimeout();
        }
    });
    m_pending.push_back(std::move(_pending));
}

bool Discv4Service::deliverReply(
    DecodedPacket const& _packet, UdpEndpoint const& _from, bcos::h256 const& _sender)
{
    bytesConstRef replyHash;
    if (_packet.type == PacketType::Pong)
    {
        replyHash = bcos::ref(std::get<PongPacket>(_packet.packet).pingHash);
    }
    else if (_packet.type == PacketType::EnrResponse)
    {
        replyHash = bcos::ref(std::get<EnrResponsePacket>(_packet.packet).requestHash);
    }
    auto it = std::find_if(m_pending.begin(), m_pending.end(), [&](Pending const& _p) {
        if (_p.from != _sender || _p.type != _packet.type)
        {
            return false;
        }
        if (_p.expectFrom && *_p.expectFrom != _from)
        {
            return false;
        }
        return _p.matchHash.empty() || std::equal(_p.matchHash.begin(), _p.matchHash.end(),
                                           replyHash.begin(), replyHash.end());
    });
    if (it == m_pending.end())
    {
        DISCV4_LOG(TRACE) << LOG_DESC("unsolicited reply dropped")
                          << LOG_KV("type", packetName(_packet.type))
                          << LOG_KV("id", shortId(_sender));
        return false;
    }
    auto const id = it->id;
    auto onReply = it->onReply;  // the callback may add/remove pendings: do not hold `it`
    bool const done = onReply ? onReply(_packet.packet) : true;
    if (done)
    {
        auto again = std::find_if(
            m_pending.begin(), m_pending.end(), [id](Pending const& _p) { return _p.id == id; });
        if (again != m_pending.end())
        {
            again->timer->cancel();
            m_pending.erase(again);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// outbound requests
// ---------------------------------------------------------------------------
void Discv4Service::sendPing(bcos::h256 const& _id, bcos::bytes const& _publicKey,
    UdpEndpoint const& _to, uint16_t _announcedTcp, std::function<void(bool, uint64_t)> _done)
{
    PingPacket ping;
    {
        std::lock_guard lock(m_mutex);
        auto ip = m_config.externalIp ? m_config.externalIp : m_publishedIp;
        ping.from.ip = ip ? bcos::bytes(ip->begin(), ip->end()) : bcos::bytes{0, 0, 0, 0};
        ping.from.udp = m_boundPort;
        ping.from.tcp = m_config.tcpPort;
        ping.enrSeq = m_localSeq;
        if (auto* entry = m_table.find(_id); entry != nullptr)
        {
            entry->lastPingSent = Clock::now();
        }
    }
    ping.to = Endpoint{ipBytes(_to.address()), _to.port(), 0};
    ping.expiration = expiration();
    auto hash = send(_to, ping);

    std::weak_ptr<Discv4Service> weak = shared_from_this();
    Pending pending;
    pending.from = _id;
    pending.type = PacketType::Pong;
    pending.matchHash = std::move(hash);
    pending.expectFrom = _to;
    pending.onReply = [weak, id = _id, key = _publicKey, from = _to, tcp = _announcedTcp,
                          done = _done](Packet const& _reply) {
        auto self = weak.lock();
        if (!self)
        {
            return true;
        }
        auto const& pong = std::get<PongPacket>(_reply);
        self->onBonded(id, key, from, tcp, pong);
        done(true, pong.enrSeq);
        return true;
    };
    pending.onTimeout = [done = std::move(_done)]() { done(false, 0); };
    expectReply(std::move(pending));
}

void Discv4Service::onBonded(bcos::h256 const& _id, bcos::bytes const& _publicKey,
    UdpEndpoint const& _from, uint16_t _announcedTcp, PongPacket const& _pong)
{
    auto const now = Clock::now();
    std::optional<rlpx::PeerConfig> newCandidate;
    std::optional<NodeEntry> needsRecord;
    CandidateSink sink;
    {
        std::lock_guard lock(m_mutex);
        auto* node = m_table.find(_id);
        if (node == nullptr)
        {
            // First proof for this node (inbound ping path) or evicted while the ping
            // was in flight: table it with the endpoint just proven.
            NodeEntry entry;
            entry.publicKey = _publicKey;
            entry.id = _id;
            entry.ip = ipBytes(_from.address());
            entry.udp = _from.port();
            entry.tcp = _announcedTcp;
            if (m_table.add(std::move(entry)) == KademliaTable::AddResult::Added)
            {
                ++m_stats.nodesDiscovered;
            }
            node = m_table.find(_id);
            if (node == nullptr)
            {
                return;
            }
        }
        // The pong came from `_from`: that endpoint is now proven for this id. A changed
        // ip/udp resets the old bond inside updateEndpoint before it is re-stamped here.
        m_table.updateEndpoint(_id, ipBytes(_from.address()), _from.port(), _announcedTcp);
        bool const wasBonded = node->bonded(now);
        node->lastPongReceived = now;
        node->enrSeq = std::max(node->enrSeq, _pong.enrSeq);
        node->findNodeFailures = 0;
        m_table.touch(_id);
        ++m_stats.pongsReceived;
        if (m_everBonded.insert(_id).second)
        {
            ++m_stats.nodesBonded;
        }
        if (!wasBonded && node->tcp != 0 && node->ip.size() == 4)
        {
            newCandidate = peerConfigOf(*node);
            sink = m_sink;
        }
        if (_pong.enrSeq != 0 && (!node->record || node->record->seq() < _pong.enrSeq))
        {
            needsRecord = *node;
        }
    }
    noteOurEndpoint(_id, _pong.to);
    if (needsRecord)
    {
        sendEnrRequest(*needsRecord);
    }
    if (sink && newCandidate)
    {
        sink(*newCandidate);
    }
}

void Discv4Service::sendEnrRequest(NodeEntry const& _node)
{
    auto to = endpointOf(_node);
    if (!to)
    {
        return;
    }
    auto hash = send(*to, EnrRequestPacket{expiration()});
    std::weak_ptr<Discv4Service> weak = shared_from_this();
    Pending pending;
    pending.from = _node.id;
    pending.type = PacketType::EnrResponse;
    pending.matchHash = std::move(hash);
    pending.onReply = [weak, id = _node.id, key = _node.publicKey](Packet const& _reply) {
        auto self = weak.lock();
        if (!self)
        {
            return true;
        }
        auto const& response = std::get<EnrResponsePacket>(_reply);
        auto const& record = response.record;
        // EIP-868: the record must be signed by the node that sent the response.
        if (!record.verify() || record.publicKey() != key)
        {
            DISCV4_LOG(DEBUG) << LOG_DESC("enrresponse with a foreign or invalid record")
                              << LOG_KV("id", shortId(id));
            return true;
        }
        std::lock_guard lock(self->m_mutex);
        if (auto* node = self->m_table.find(id); node != nullptr)
        {
            node->record = record;
            node->enrSeq = std::max(node->enrSeq, record.seq());
            if (auto tcp = record.tcp(); tcp && *tcp != 0)
            {
                node->tcp = *tcp;
            }
            ++self->m_stats.recordsFetched;
            DISCV4_LOG(DEBUG) << LOG_DESC("fetched node record") << LOG_KV("id", shortId(id))
                              << LOG_KV("seq", record.seq())
                              << LOG_KV("eth", record.eth() ? toHex(record.eth()->hash) : "-");
        }
        return true;
    };
    expectReply(std::move(pending));
}

void Discv4Service::findNode(bcos::h256 const& _id, bcos::bytes const& _targetKey,
    std::function<void(std::vector<NeighborNode>)> _done)
{
    // Callbacks are never invoked under m_mutex: _done re-enters lookupStep -> findNode,
    // which takes the lock again (the mutex is not recursive).
    NodeEntry copy;
    bool needsProof = false;
    bool found = false;
    {
        std::lock_guard lock(m_mutex);
        if (auto const* node = m_table.find(_id); node != nullptr)
        {
            found = true;
            copy = *node;
            needsProof =
                !node->hasProofOfUs(Clock::now()) || node->findNodeFailures > c_maxFindNodeFailures;
        }
    }
    if (!found)
    {
        _done({});
        return;
    }
    if (!needsProof)
    {
        sendFindNode(copy, _targetKey, std::move(_done));
        return;
    }
    // geth ensureBond: ping, wait for the pong, then give the peer one response timeout
    // to ping us back (so it holds a proof for us) before asking.
    auto to = endpointOf(copy);
    if (!to)
    {
        _done({});
        return;
    }
    std::weak_ptr<Discv4Service> weak = shared_from_this();
    sendPing(copy.id, copy.publicKey, *to, 0,
        [weak, id = _id, target = _targetKey, done = std::move(_done)](bool _ok, uint64_t) {
            auto self = weak.lock();
            if (!self || !_ok || !self->m_running.load())
            {
                done({});
                return;
            }
            auto timer = std::make_shared<boost::asio::steady_timer>(self->m_io);
            timer->expires_after(self->m_config.responseTimeout);
            timer->async_wait(
                [weak, id, target, done, timer](boost::system::error_code const& _ec) {
                    auto self = weak.lock();
                    if (_ec || !self || !self->m_running.load())
                    {
                        done({});
                        return;
                    }
                    NodeEntry copy;
                    bool found = false;
                    {
                        std::lock_guard lock(self->m_mutex);
                        if (auto const* node = self->m_table.find(id); node != nullptr)
                        {
                            found = true;
                            copy = *node;
                        }
                    }
                    if (!found)
                    {
                        done({});
                        return;
                    }
                    self->sendFindNode(copy, target, done);
                });
        });
}

void Discv4Service::sendFindNode(NodeEntry const& _node, bcos::bytes const& _targetKey,
    std::function<void(std::vector<NeighborNode>)> _done)
{
    auto to = endpointOf(_node);
    if (!to)
    {
        _done({});
        return;
    }
    send(*to, FindNodePacket{_targetKey, expiration()});
    auto collected = std::make_shared<std::vector<NeighborNode>>();
    std::weak_ptr<Discv4Service> weak = shared_from_this();
    Pending pending;
    pending.from = _node.id;
    pending.type = PacketType::Neighbors;
    pending.onReply = [collected, done = _done](Packet const& _reply) {
        auto const& neighbors = std::get<NeighborsPacket>(_reply);
        collected->insert(collected->end(), neighbors.nodes.begin(), neighbors.nodes.end());
        if (collected->size() >= KademliaTable::c_bucketSize)
        {
            done(*collected);
            return true;
        }
        return false;  // geth: keep collecting until k nodes or the timeout
    };
    pending.onTimeout = [weak, collected, id = _node.id, done = std::move(_done)]() {
        if (collected->empty())
        {
            if (auto self = weak.lock())
            {
                std::lock_guard lock(self->m_mutex);
                if (auto* node = self->m_table.find(id); node != nullptr)
                {
                    if (++node->findNodeFailures > c_maxFindNodeFailures)
                    {
                        self->m_table.remove(id);
                        DISCV4_LOG(DEBUG) << LOG_DESC("node dropped after findnode failures")
                                          << LOG_KV("id", shortId(id));
                    }
                }
            }
        }
        done(*collected);
    };
    expectReply(std::move(pending));
}

// ---------------------------------------------------------------------------
// table maintenance
// ---------------------------------------------------------------------------
void Discv4Service::addSeen(NodeEntry _entry)
{
    if (_entry.ip.size() != 4 || _entry.udp == 0)
    {
        return;
    }
    auto const now = Clock::now();
    bool shouldPing = false;
    NodeEntry copy;
    {
        std::lock_guard lock(m_mutex);
        auto const id = _entry.id;
        auto const result = m_table.add(std::move(_entry));
        if (result == KademliaTable::AddResult::Self)
        {
            return;
        }
        if (result == KademliaTable::AddResult::Added)
        {
            ++m_stats.nodesDiscovered;
        }
        auto* node = m_table.find(id);
        if (node == nullptr)
        {
            return;  // replacement cache overflow
        }
        if (!node->bonded(now) && (node->lastPingSent == Clock::time_point{} ||
                                      now - node->lastPingSent > c_pingRepeatInterval))
        {
            shouldPing = true;
            copy = *node;
        }
    }
    if (shouldPing)
    {
        if (auto to = endpointOf(copy))
        {
            sendPing(copy.id, copy.publicKey, *to, 0, [](bool, uint64_t) {});
        }
    }
}

void Discv4Service::noteOurEndpoint(bcos::h256 const& _reporter, Endpoint const& _to)
{
    if (m_config.externalIp || !_to.isV4())
    {
        return;
    }
    boost::asio::ip::address_v4::bytes_type raw{};
    std::copy(_to.ip.begin(), _to.ip.end(), raw.begin());
    boost::asio::ip::address_v4 const stated(raw);
    if (stated.is_unspecified() || stated.is_multicast() ||
        stated == boost::asio::ip::address_v4::broadcast())
    {
        return;
    }
    // Only statements in our listen address's scope count: a global address always; a
    // loopback / private one only when we are bound to that scope ourselves (an
    // unspecified bind is treated as global-only).
    boost::system::error_code ec;
    auto const bound = boost::asio::ip::make_address_v4(m_config.listenIp, ec);
    bool const boundLoopback = !ec && bound.is_loopback();
    bool const boundPrivate = !ec && isPrivateV4(bound);
    if ((stated.is_loopback() && !boundLoopback) ||
        (isPrivateV4(stated) && !boundPrivate && !boundLoopback))
    {
        return;
    }
    std::array<uint8_t, 4> ip{};
    std::copy(raw.begin(), raw.end(), ip.begin());
    std::lock_guard lock(m_mutex);
    auto const now = Clock::now();
    m_ipTracker.addStatement(_reporter, ip, now);
    auto predicted = m_ipTracker.predict(now);
    if (!predicted || m_publishedIp == predicted)
    {
        return;
    }
    m_publishedIp = predicted;
    ++m_localSeq;
    rebuildLocalRecord();
    DISCV4_LOG(INFO) << LOG_DESC("external endpoint learned from peers, ENR republished")
                     << LOG_KV("ip", m_localRecord.ipString()) << LOG_KV("seq", m_localSeq)
                     << LOG_KV("statements", m_ipTracker.size());
}

// ---------------------------------------------------------------------------
// lookups and timers
// ---------------------------------------------------------------------------
void Discv4Service::startLookup(bcos::h256 _target, bcos::bytes _targetKey)
{
    auto lookup = std::make_shared<Lookup>();
    lookup->target = _target;
    lookup->targetKey = std::move(_targetKey);
    {
        std::lock_guard lock(m_mutex);
        for (auto const& entry : m_table.closest(_target, KademliaTable::c_bucketSize))
        {
            lookup->seen.emplace(entry.id, Lookup::Candidate{entry, false});
        }
    }
    lookupStep(std::move(lookup));
}

void Discv4Service::lookupStep(std::shared_ptr<Lookup> _lookup)
{
    if (!m_running.load())
    {
        return;
    }
    // The k closest nodes heard of so far, by distance to the target.
    std::vector<Lookup::Candidate*> ordered;
    ordered.reserve(_lookup->seen.size());
    for (auto& [id, candidate] : _lookup->seen)
    {
        ordered.push_back(&candidate);
    }
    std::sort(ordered.begin(), ordered.end(), [&](auto const* _a, auto const* _b) {
        return KademliaTable::xorDistance(_a->entry.id, _lookup->target) <
               KademliaTable::xorDistance(_b->entry.id, _lookup->target);
    });
    if (ordered.size() > KademliaTable::c_bucketSize)
    {
        ordered.resize(KademliaTable::c_bucketSize);
    }
    size_t asked = 0;
    std::weak_ptr<Discv4Service> weak = shared_from_this();
    for (auto* candidate : ordered)
    {
        if (candidate->queried || _lookup->inflight >= c_lookupConcurrency)
        {
            continue;
        }
        candidate->queried = true;
        ++_lookup->inflight;
        ++asked;
        auto const queriedId = candidate->entry.id;
        auto const queriedIp = addressOf(candidate->entry.ip);
        findNode(queriedId, _lookup->targetKey,
            [weak, lookup = _lookup, queriedIp](std::vector<NeighborNode> _nodes) {
                auto self = weak.lock();
                if (!self)
                {
                    return;
                }
                for (auto const& node : _nodes)
                {
                    auto entry = entryFrom(node);
                    if (entry.id == self->m_selfId ||
                        !acceptableRelayAddress(queriedIp, entry.ip) || entry.udp == 0)
                    {
                        continue;
                    }
                    if (lookup->seen.emplace(entry.id, Lookup::Candidate{entry, false}).second)
                    {
                        self->addSeen(entry);
                    }
                }
                --lookup->inflight;
                self->lookupStep(lookup);
            });
    }
    if (asked == 0 && _lookup->inflight == 0)
    {
        size_t tableSize = 0;
        {
            std::lock_guard lock(m_mutex);
            ++m_stats.lookups;
            tableSize = m_table.size();
        }
        DISCV4_LOG(DEBUG) << LOG_DESC("lookup finished")
                          << LOG_KV("target", shortId(_lookup->target))
                          << LOG_KV("seen", _lookup->seen.size()) << LOG_KV("table", tableSize);
    }
}

void Discv4Service::scheduleLookupTimer(std::chrono::milliseconds _delay)
{
    if (!m_running.load())
    {
        return;
    }
    std::weak_ptr<Discv4Service> weak = shared_from_this();
    m_lookupTimer.expires_after(_delay);
    m_lookupTimer.async_wait([weak](boost::system::error_code const& _ec) {
        if (_ec)
        {
            return;
        }
        if (auto self = weak.lock())
        {
            self->onLookupTimer();
        }
    });
}

void Discv4Service::onLookupTimer()
{
    if (!m_running.load())
    {
        return;
    }
    // geth doRefresh: one self lookup then random targets. Below the target table size
    // lookups run every lookupInterval; a filled table refreshes four times per
    // refreshInterval.
    if (m_lookupRound % 4 == 0)
    {
        startLookup(m_selfId, m_key.publicKey());
    }
    else
    {
        rlpx::EccKeyPair randomKey;
        startLookup(
            KademliaTable::nodeIdOf(bcos::ref(randomKey.publicKey())), randomKey.publicKey());
    }
    ++m_lookupRound;
    size_t tableSize = 0;
    {
        std::lock_guard lock(m_mutex);
        tableSize = m_table.size();
    }
    auto const next =
        tableSize < m_config.tableTargetSize ?
            std::chrono::duration_cast<std::chrono::milliseconds>(m_config.lookupInterval) :
            std::chrono::duration_cast<std::chrono::milliseconds>(m_config.refreshInterval) / 4;
    scheduleLookupTimer(next);
}

void Discv4Service::scheduleRevalidate()
{
    if (!m_running.load())
    {
        return;
    }
    std::weak_ptr<Discv4Service> weak = shared_from_this();
    m_revalidateTimer.expires_after(m_config.revalidateInterval);
    m_revalidateTimer.async_wait([weak](boost::system::error_code const& _ec) {
        if (_ec)
        {
            return;
        }
        if (auto self = weak.lock())
        {
            self->onRevalidate();
        }
    });
}

void Discv4Service::onRevalidate()
{
    if (!m_running.load())
    {
        return;
    }
    std::optional<NodeEntry> target;
    {
        std::lock_guard lock(m_mutex);
        if (auto id = m_table.revalidationCandidate(m_random()))
        {
            if (auto const* node = m_table.find(*id); node != nullptr)
            {
                target = *node;
            }
        }
    }
    if (auto to = target ? endpointOf(*target) : std::nullopt)
    {
        std::weak_ptr<Discv4Service> weak = shared_from_this();
        sendPing(
            target->id, target->publicKey, *to, 0, [weak, id = target->id](bool _ok, uint64_t) {
                if (_ok)
                {
                    return;  // onBonded already refreshed the entry
                }
                if (auto self = weak.lock())
                {
                    std::lock_guard lock(self->m_mutex);
                    if (self->m_table.remove(id))
                    {
                        DISCV4_LOG(DEBUG) << LOG_DESC("revalidation failed, node dropped")
                                          << LOG_KV("id", shortId(id));
                    }
                }
            });
    }
    scheduleRevalidate();
}
}  // namespace bcos::devp2p::disc
