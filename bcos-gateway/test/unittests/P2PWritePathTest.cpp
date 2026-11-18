/*
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
 */

// Tests for the write-path / broadcast-fan-out work in this branch:
//   - Message::encodeHeaderSize / P2PMessageOptions::encodeSize (single-allocation header encoding)
//   - KeyImpl::hex() caching and its invalidation
//   - prepareFanoutPayload (compress once per broadcast, with the no-gain fallback)
//   - PeersRouterTable::broadcastMessage sharing one Message across peers and stamping each peer's
//     destination before sending

#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-framework/protocol/ProtocolInfo.h>
#include <bcos-gateway/gateway/PeersRouterTable.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libnetwork/Message.h>
#include <bcos-gateway/protocol/GatewayNodeStatus.h>
#include <bcos-tars-protocol/protocol/GroupNodeInfoImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/ZstdCompress.h>
#include <boost/test/unit_test.hpp>
#include <random>
#include <vector>

using namespace bcos;
using namespace bcos::gateway;

namespace bcos::test
{

namespace
{

// ---------------------------------------------------------------------------------------------
// header / options sizing
// ---------------------------------------------------------------------------------------------

Message makeMessage(uint16_t version, uint16_t packetType, std::size_t idLength, int dstCount)
{
    Message message;
    message.setVersion(version);
    message.setPacketType(packetType);
    message.setSeq(0x1234);
    message.setSrcP2PNodeID(std::string(idLength, 'a'));
    message.setDstP2PNodeID(std::string(idLength, 'b'));

    P2PMessageOptions options;
    options.setGroupID("group0");
    options.setModuleID(1000);
    options.setSrcNodeID(bytes(idLength / 2, 0x11));
    for (int i = 0; i < dstCount; ++i)
    {
        options.mutableDstNodeIDs().push_back(bytes(idLength / 2, static_cast<byte>(0x20 + i)));
    }
    message.setOptions(std::move(options));
    return message;
}

// ---------------------------------------------------------------------------------------------
// a P2PInterface that records what a fan-out actually handed it
// ---------------------------------------------------------------------------------------------

struct SentRecord
{
    P2pID peer;
    std::string dstP2PNodeID;
    PreparedPayload const* prepared{nullptr};  ///< null when no shared payload was supplied
};

class RecordingP2P : public P2PInterface
{
public:
    std::vector<SentRecord> sent;

    void start() override {}
    void stop() override {}
    P2pID id() const override { return "self"; }

    task::Task<std::optional<Message>> sendMessageByNodeID(P2pID nodeID, Message& header,
        ::ranges::any_view<bytesConstRef> /*payloads*/, Options options) override
    {
        // Read the destination HERE, not in the caller: the fan-out reuses one Message and stamps it
        // once per peer, so a record taken at call time is what proves the stamping works.
        sent.push_back(SentRecord{nodeID, header.dstP2PNodeID(), options.prepared.get()});
        co_return std::nullopt;
    }

    task::Task<void> broadcastMessageToAll(Message::Ptr /*message*/,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> /*payloads*/,
        Options /*options*/) override
    {
        co_return;
    }
    task::Task<void> sendMessageByNodeIDs(uint16_t /*_type*/,
        const std::vector<P2pID>& /*_nodeIDs*/, bcos::bytes /*_payload*/,
        Options /*options*/) override
    {
        co_return;
    }
    P2PInfos sessionInfos() override { return {}; }
    P2PInfo localP2pInfo() override { return {}; }
    bool isConnected(P2pID const& /*_nodeID*/) const override { return false; }
    bool isReachable(P2pID const& /*_nodeID*/) const override { return false; }
    std::shared_ptr<Host> host() override { return nullptr; }
    uint32_t newSeq() override { return ++m_seq; }
    std::shared_ptr<P2PSession> getP2PSessionByNodeId(P2pID const& /*_nodeID*/) const override
    {
        return nullptr;
    }
    bool registerHandlerByMsgType(uint16_t /*_type*/, MessageHandler const& /*_msgHandler*/) override
    {
        return true;
    }
    void eraseHandlerByMsgType(uint16_t /*_type*/) override {}
    void sendRespMessageBySession(bytesConstRef /*_payload*/, uint32_t /*_requestSeq*/,
        std::string /*_requestSrcP2PNodeID*/, std::shared_ptr<P2PSession> /*_p2pSession*/) override
    {}
    void updatePeerBlacklist(const std::set<std::string>& /*_strList*/, const bool /*_enable*/) override
    {}
    void updatePeerWhitelist(const std::set<std::string>& /*_strList*/, const bool /*_enable*/) override
    {}

private:
    uint32_t m_seq{0};
};

// updateGatewayInfo is protected; expose it so the fan-out can be driven without a full node.
class ExposedPeersRouterTable : public PeersRouterTable
{
public:
    ExposedPeersRouterTable(std::string uuid, P2PInterface::Ptr p2p)
      : PeersRouterTable(std::move(uuid), nullptr, std::move(p2p))
    {}
    using PeersRouterTable::updateGatewayInfo;
};

GatewayNodeStatus::Ptr makeNodeStatus(std::string const& uuid, std::string const& groupID,
    std::vector<std::string> nodeIDs)
{
    auto groupInfo = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>();
    groupInfo->setGroupID(groupID);
    // type 0 == GROUP_WITH_CONSENSUS_NODE, which is the bucket
    // randomChooseP2PNode(CONSENSUS_NODE, ...) looks in
    groupInfo->setType(static_cast<uint16_t>(GroupType::GROUP_WITH_CONSENSUS_NODE));
    std::vector<protocol::ProtocolInfo::ConstPtr> protocols;
    for (std::size_t i = 0; i < nodeIDs.size(); ++i)
    {
        protocols.push_back(std::make_shared<protocol::ProtocolInfo>(
            protocol::ProtocolModuleID::NodeService, 3, 3));
    }
    groupInfo->setNodeIDList(std::move(nodeIDs));
    groupInfo->setNodeProtocolList(std::move(protocols));

    auto status = std::make_shared<GatewayNodeStatus>();
    status->setUUID(uuid);
    status->setSeq(1);
    std::vector<GroupNodeInfo::Ptr> groupInfos;
    groupInfos.emplace_back(std::move(groupInfo));
    status->setGroupNodeInfos(std::move(groupInfos));
    return status;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(P2PWritePathTest)

// encodeHeaderSize / encodeSize must be exactly the number of bytes the encoder appends. This is
// what lets the encoder size its buffer once instead of growing it in ~8 insert() steps.
BOOST_AUTO_TEST_CASE(encodeSizeMatchesEncodedLength)
{
    struct Case
    {
        uint16_t version;
        uint16_t packetType;
        std::size_t idLength;
        int dstCount;
    };
    // V0 carries no extended header; P2P/broadcast packet types additionally carry the options block
    std::vector<Case> cases{
        {static_cast<uint16_t>(protocol::ProtocolVersion::V0),
            GatewayMessageType::Heartbeat, 128, 1},
        {static_cast<uint16_t>(protocol::ProtocolVersion::V2),
            GatewayMessageType::PeerToPeerMessage, 128, 1},
        {static_cast<uint16_t>(protocol::ProtocolVersion::V3),
            GatewayMessageType::PeerToPeerMessage, 32, 1},
        {static_cast<uint16_t>(protocol::ProtocolVersion::V3),
            GatewayMessageType::BroadcastMessage, 32, 4},
        {static_cast<uint16_t>(protocol::ProtocolVersion::V2),
            GatewayMessageType::Heartbeat, 128, 0},
    };

    for (auto const& c : cases)
    {
        auto message = makeMessage(c.version, c.packetType, c.idLength, c.dstCount);

        // options block on its own
        bytes optionsBuffer;
        BOOST_REQUIRE(message.options().encode(optionsBuffer));
        BOOST_CHECK_EQUAL(optionsBuffer.size(), message.options().encodeSize());

        bytes headerBuffer;
        BOOST_REQUIRE(message.encodeHeader(headerBuffer));
        BOOST_CHECK_EQUAL(headerBuffer.size(), message.encodeHeaderSize());

        // A V0 heartbeat has neither the extended header nor options: exactly the fixed base header.
        if (c.version == static_cast<uint16_t>(protocol::ProtocolVersion::V0) &&
            c.packetType == GatewayMessageType::Heartbeat)
        {
            BOOST_CHECK_EQUAL(message.encodeHeaderSize(), Message::MESSAGE_HEADER_LENGTH);
        }

        // A V2/V3 frame with options is strictly larger than the base header.
        if (c.packetType == GatewayMessageType::PeerToPeerMessage)
        {
            BOOST_CHECK_GT(message.encodeHeaderSize(), Message::MESSAGE_HEADER_LENGTH);
        }
    }
}

// KeyImpl::hex() caches: repeated calls return the same string, and the cache is invalidated when
// the key material changes. The cache must be per-instance -- a function-local static would hand the
// first key's hex to every other key, so the two-instance case below is the regression guard.
BOOST_AUTO_TEST_CASE(keyHexIsCachedAndInvalidated)
{
    crypto::KeyImpl first(bytes{0x01, 0x02});
    crypto::KeyImpl second(bytes{0xab, 0xcd});

    BOOST_CHECK_EQUAL(first.hex(), "0102");
    BOOST_CHECK_EQUAL(second.hex(), "abcd");

    // repeated calls are stable
    auto const cached = first.hex();
    for (int i = 0; i < 5; ++i)
    {
        BOOST_CHECK_EQUAL(first.hex(), cached);
    }

    // decode() drops the cache
    first.decode(bytes{0x03, 0x04});
    BOOST_CHECK_EQUAL(first.hex(), "0304");

    // writing through mutableData() drops it too
    auto* raw = first.mutableData();
    raw[0] = 0x05;
    BOOST_CHECK_EQUAL(first.hex(), "0504");
}

// prepareFanoutPayload compresses once for the whole fan-out, and returns nullptr (so each session
// keeps the original zero-copy views) when compression is off, pointless, or counter-productive.
BOOST_AUTO_TEST_CASE(prepareFanoutPayloadBehaviour)
{
    auto single = [](bytes& payload) { return ::ranges::views::single(bcos::ref(payload)); };

    // 1) compressible and above the threshold -> a shared zstd frame that round-trips
    {
        bytes payload(8 * 1024, 0xAB);
        auto prepared = prepareFanoutPayload(single(payload), /*enableCompress=*/true);
        BOOST_REQUIRE(prepared);
        BOOST_CHECK(prepared->compressed);
        BOOST_REQUIRE(prepared->data);
        BOOST_CHECK_LT(prepared->data->size(), payload.size());

        bytes restored;
        BOOST_REQUIRE(ZstdCompress::uncompress(bcos::ref(*prepared->data), restored));
        BOOST_CHECK(restored == payload);
    }

    // 2) high-entropy input expands under zstd -> no gain, so no prepared payload
    {
        bytes payload(8 * 1024);
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& b : payload)
        {
            b = static_cast<byte>(dist(rng));
        }
        BOOST_CHECK(!prepareFanoutPayload(single(payload), /*enableCompress=*/true));
    }

    // 3) below the compress threshold -> nothing to do
    {
        bytes payload(512, 0xAB);
        BOOST_CHECK(!prepareFanoutPayload(single(payload), /*enableCompress=*/true));
    }

    // 4) compression disabled by configuration
    {
        bytes payload(8 * 1024, 0xAB);
        BOOST_CHECK(!prepareFanoutPayload(single(payload), /*enableCompress=*/false));
    }
}

// The fan-out hands every peer the SAME message object and stamps each destination just before
// sending. Verifies both halves: per-peer destination correctness and a single shared Message (the
// object identity is observable through the pointer each record captured).
BOOST_AUTO_TEST_CASE(broadcastSharesMessageAndStampsPerPeer)
{
    auto p2p = std::make_shared<RecordingP2P>();
    // broadcastMessage() calls shared_from_this() to keep the table alive across its fan-out
    auto table = std::make_shared<ExposedPeersRouterTable>("self-uuid", p2p);

    // the table creates/updates its own GatewayStatus from the node status
    table->updateGatewayInfo("p2pA", makeNodeStatus("gwA", "group0", {"nodeA"}));
    table->updateGatewayInfo("p2pB", makeNodeStatus("gwB", "group0", {"nodeB"}));

    auto message = std::make_shared<Message>();
    message->setPacketType(GatewayMessageType::BroadcastMessage);
    message->setSeq(1);
    bytes payload(32, 'x');
    auto payloads = ::ranges::views::single(bcos::ref(payload));

    task::syncWait(table->broadcastMessage(static_cast<uint16_t>(protocol::NodeType::CONSENSUS_NODE),
        "group0", 1000, message, payloads));

    BOOST_REQUIRE_EQUAL(p2p->sent.size(), 2U);

    // both peers were visited, each with its own destination stamped onto the shared message
    std::set<std::string> visited;
    for (auto const& record : p2p->sent)
    {
        visited.insert(record.peer);
        BOOST_CHECK_EQUAL(record.dstP2PNodeID, record.peer);
    }
    BOOST_CHECK(visited == (std::set<std::string>{"p2pA", "p2pB"}));

    // a 32-byte payload is below the compress threshold, so nothing was pre-compressed
    for (auto const& record : p2p->sent)
    {
        BOOST_CHECK(record.prepared == nullptr);
    }
}

// A payload above the threshold is compressed once and the SAME PreparedPayload is handed to every
// peer, i.e. zstd does not run per peer.
BOOST_AUTO_TEST_CASE(broadcastCompressesOnceForAllPeers)
{
    auto p2p = std::make_shared<RecordingP2P>();
    // broadcastMessage() calls shared_from_this() to keep the table alive across its fan-out
    auto table = std::make_shared<ExposedPeersRouterTable>("self-uuid", p2p);

    // the table creates/updates its own GatewayStatus from the node status
    table->updateGatewayInfo("p2pA", makeNodeStatus("gwA", "group0", {"nodeA"}));
    table->updateGatewayInfo("p2pB", makeNodeStatus("gwB", "group0", {"nodeB"}));

    auto message = std::make_shared<Message>();
    message->setPacketType(GatewayMessageType::BroadcastMessage);
    message->setSeq(1);
    bytes payload(8 * 1024, 0xCD);
    auto payloads = ::ranges::views::single(bcos::ref(payload));

    task::syncWait(table->broadcastMessage(static_cast<uint16_t>(protocol::NodeType::CONSENSUS_NODE),
        "group0", 1000, message, payloads));

    BOOST_REQUIRE_EQUAL(p2p->sent.size(), 2U);
    BOOST_REQUIRE(p2p->sent[0].prepared != nullptr);
    BOOST_REQUIRE(p2p->sent[1].prepared != nullptr);
    // identical pointer == one compression shared by both peers
    BOOST_CHECK_EQUAL(p2p->sent[0].prepared, p2p->sent[1].prepared);
}

// The switch has to gate the fan-out pre-compression, otherwise the knob would be half-applied.
BOOST_AUTO_TEST_CASE(broadcastCompressionSwitchIsHonoured)
{
    auto p2p = std::make_shared<RecordingP2P>();
    // broadcastMessage() calls shared_from_this() to keep the table alive across its fan-out
    auto table = std::make_shared<ExposedPeersRouterTable>("self-uuid", p2p);
    table->setEnableCompressForBroadcast(false);
    BOOST_CHECK(!table->enableCompressForBroadcast());

    table->updateGatewayInfo("p2pA", makeNodeStatus("gwA", "group0", {"nodeA"}));

    auto message = std::make_shared<Message>();
    message->setPacketType(GatewayMessageType::BroadcastMessage);
    message->setSeq(1);
    bytes payload(8 * 1024, 0xCD);
    auto payloads = ::ranges::views::single(bcos::ref(payload));

    task::syncWait(table->broadcastMessage(static_cast<uint16_t>(protocol::NodeType::CONSENSUS_NODE),
        "group0", 1000, message, payloads));

    BOOST_REQUIRE_EQUAL(p2p->sent.size(), 1U);
    BOOST_CHECK(p2p->sent[0].prepared == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
