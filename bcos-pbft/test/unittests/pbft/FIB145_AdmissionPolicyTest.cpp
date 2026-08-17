/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Regression tests for FIB-145: Unbounded m_msgQueue growth while syncing
 *        enables memory DoS. Tests the 3-stage PBFTPipeline admission policy
 *        (Stage 1: stale-height filter; Stage 2: passthrough stub; Stage 3: per-peer
 *        capacity with backpressure for PrePrepare/Prepare; Commit/CheckPoint always
 *        admitted).
 * @file FIB145_AdmissionPolicyTest.cpp
 * @author: claude
 * @date 2026-05-07
 */
#include "bcos-pbft/pbft/utilities/PBFTPipeline.h"
#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{

// Minimal PBFTBaseMessageInterface stub for pipeline tests.
// We use KeyImpl directly for the "from" node identity, avoiding the need
// to subclass KeyInterface (which has many pure virtuals).
class StubPBFTMsg : public PBFTBaseMessageInterface
{
public:
    using Ptr = std::shared_ptr<StubPBFTMsg>;

    StubPBFTMsg(PacketType type, int64_t index, KeyInterface::Ptr from, HashType hash = {})
      : m_type(type), m_index(index), m_from(std::move(from)), m_hash(std::move(hash))
    {}

    PacketType packetType() const override { return m_type; }
    int64_t index() const override { return m_index; }
    bcos::crypto::HashType const& hash() const override { return m_hash; }
    ViewType view() const override { return 0; }
    IndexType generatedFrom() const override { return 0; }
    int64_t timestamp() const override { return 0; }
    int32_t version() const override { return 0; }
    void setIndex(int64_t v) override { m_index = v; }
    void setTimestamp(int64_t) override {}
    void setVersion(int32_t) override {}
    void setView(ViewType) override {}
    void setGeneratedFrom(IndexType) override {}
    void setHash(bcos::crypto::HashType const& h) override { m_hash = h; }
    void setPacketType(PacketType t) override { m_type = t; }
    bytesPointer encode(
        bcos::crypto::CryptoSuite::Ptr, bcos::crypto::KeyPairInterface::Ptr) const override
    {
        return nullptr;
    }
    void decode(bytesConstRef) override {}
    bytesConstRef signatureData() override { return {}; }
    bcos::crypto::HashType const& signatureDataHash() override { return m_hash; }
    void setSignatureData(bytes&&) override {}
    void setSignatureData(bytes const&) override {}
    void setSignatureDataHash(bcos::crypto::HashType const&) override {}
    bool verifySignature(bcos::crypto::CryptoSuite::Ptr, bcos::crypto::PublicPtr) override
    {
        return true;
    }
    void setFrom(bcos::crypto::PublicPtr from) override { m_from = std::move(from); }
    bcos::crypto::PublicPtr from() const override { return m_from; }
    uint64_t liveTimeInMilliseconds() const override { return 0; }
    std::string toDebugString() const override { return "StubPBFTMsg"; }

private:
    PacketType m_type;
    int64_t m_index;
    KeyInterface::Ptr m_from;
    HashType m_hash;
};

// Create a KeyImpl "node id" from a short ASCII tag.
// The hex of the key will be the hex-encoded tag bytes — consistent for the same tag,
// different across different tags. That is all PBFTPipeline needs.
static KeyInterface::Ptr makePeerKey(std::string const& tag)
{
    return std::make_shared<KeyImpl>(bytes(tag.begin(), tag.end()));
}

// Helper to create a stub message with a given peer key
static StubPBFTMsg::Ptr makeMsg(
    PacketType type, int64_t index, KeyInterface::Ptr const& from, HashType hash = {})
{
    return std::make_shared<StubPBFTMsg>(type, index, from, std::move(hash));
}

BOOST_FIXTURE_TEST_SUITE(FIB145Test, TestPromptFixture)

// Stage 1: messages with index < lastApplied must be dropped (stale).
// Commit and CheckPoint at any height must pass through.
BOOST_AUTO_TEST_CASE(stale_height_message_dropped)
{
    PBFTPipeline pipeline;
    BlockNumber lastApplied = 100;
    auto peer = makePeerKey("peerA");

    // PrePrepare with index < lastApplied → dropped
    auto stalePrep = makeMsg(PacketType::PrePreparePacket, 50, peer);
    BOOST_CHECK_MESSAGE(!pipeline.admit(stalePrep, lastApplied),
        "FIB-145 Stage1: stale PrePrepare (index=50 < lastApplied=100) must be dropped");

    // PrePrepare with index == lastApplied → admitted (filter is strictly less-than)
    auto currentPrep = makeMsg(PacketType::PrePreparePacket, 100, peer);
    BOOST_CHECK_MESSAGE(pipeline.admit(currentPrep, lastApplied),
        "FIB-145 Stage1: PrePrepare at lastApplied height must be admitted (not stale)");

    // PrePrepare with index > lastApplied → admitted
    auto freshPrep = makeMsg(PacketType::PrePreparePacket, 101, peer);
    BOOST_CHECK_MESSAGE(pipeline.admit(freshPrep, lastApplied),
        "FIB-145 Stage1: fresh PrePrepare (index=101 > lastApplied=100) must be admitted");

    // Commit at stale height → must NOT be dropped (safety-critical)
    auto staleCommit = makeMsg(PacketType::CommitPacket, 50, peer);
    BOOST_CHECK_MESSAGE(pipeline.admit(staleCommit, lastApplied),
        "FIB-145 Stage1: Commit must never be dropped regardless of height");

    // CheckPoint at stale height → must NOT be dropped
    auto staleCheckPt = makeMsg(PacketType::CheckPoint, 50, peer);
    BOOST_CHECK_MESSAGE(pipeline.admit(staleCheckPt, lastApplied),
        "FIB-145 Stage1: CheckPoint must never be dropped regardless of height");
}

// Stage 3: flooding PrePrepare/Prepare from one peer until overflow triggers
// backpressure. After that, further PrePrepare/Prepare from that peer are
// suppressed. Commit/CheckPoint from the same peer continue to be admitted.
// Other peers are unaffected.
BOOST_AUTO_TEST_CASE(commit_overflow_sets_peer_backpressure_for_lower_priority_packets)
{
    // Use a small capacity so we can trigger overflow easily
    PBFTPipeline::Config cfg;
    cfg.perPeerCapacity = 4;
    PBFTPipeline pipeline(cfg);

    BlockNumber lastApplied = 0;
    auto peerA = makePeerKey("peerA");
    auto peerB = makePeerKey("peerB");

    // Admit `perPeerCapacity` messages from peerA — all should succeed
    for (size_t i = 0; i < cfg.perPeerCapacity; i++)
    {
        auto msg = makeMsg(PacketType::PrePreparePacket, static_cast<int64_t>(100 + i), peerA);
        BOOST_CHECK(pipeline.admit(msg, lastApplied));
    }

    // Now peerA is at capacity. Next PrePrepare from peerA must be rejected and
    // backpressure must be set.
    auto overflowMsg = makeMsg(PacketType::PrePreparePacket, 200, peerA);
    BOOST_CHECK_MESSAGE(!pipeline.admit(overflowMsg, lastApplied),
        "FIB-145 Stage3: overflow message from peerA must be dropped");
    BOOST_CHECK_MESSAGE(pipeline.isPeerBackpressured(peerA->hex()),
        "FIB-145 Stage3: peerA must be under backpressure after overflow");

    // Commit from peerA must still be admitted despite backpressure
    auto commit = makeMsg(PacketType::CommitPacket, 200, peerA);
    BOOST_CHECK_MESSAGE(pipeline.admit(commit, lastApplied),
        "FIB-145 Stage3: Commit from backpressured peerA must still be admitted");

    // CheckPoint from peerA must still be admitted
    auto chkpt = makeMsg(PacketType::CheckPoint, 200, peerA);
    BOOST_CHECK_MESSAGE(pipeline.admit(chkpt, lastApplied),
        "FIB-145 Stage3: CheckPoint from backpressured peerA must still be admitted");

    // Another PrePrepare from peerA must be suppressed (still backpressured)
    auto anotherPrep = makeMsg(PacketType::PrePreparePacket, 201, peerA);
    BOOST_CHECK_MESSAGE(!pipeline.admit(anotherPrep, lastApplied),
        "FIB-145 Stage3: further PrePrepare from backpressured peerA must be suppressed");

    // peerB is unaffected by peerA's backpressure
    auto peerBMsg = makeMsg(PacketType::PrePreparePacket, 201, peerB);
    BOOST_CHECK_MESSAGE(pipeline.admit(peerBMsg, lastApplied),
        "FIB-145 Stage3: peerB must be unaffected by peerA backpressure");
}

// Commit and CheckPoint are NEVER dropped by any stage.
BOOST_AUTO_TEST_CASE(commit_and_checkpoint_never_dropped)
{
    PBFTPipeline::Config cfg;
    cfg.perPeerCapacity = 2;
    PBFTPipeline pipeline(cfg);

    BlockNumber lastApplied = 1000;  // very high to exercise Stage 1
    auto peer = makePeerKey("testPeer");

    // Under backpressure (fill and overflow)
    for (size_t i = 0; i < cfg.perPeerCapacity + 1; i++)
    {
        auto msg = makeMsg(PacketType::PrePreparePacket, static_cast<int64_t>(1001 + i), peer);
        pipeline.admit(msg, lastApplied);
    }
    BOOST_CHECK(pipeline.isPeerBackpressured(peer->hex()));

    // Commit must always pass through
    for (int i = 0; i < 10; i++)
    {
        auto commit = makeMsg(PacketType::CommitPacket, 500 + i, peer);  // even stale heights
        BOOST_CHECK_MESSAGE(pipeline.admit(commit, lastApplied),
            "FIB-145: Commit must never be dropped (index=" + std::to_string(500 + i) + ")");
    }
    // CheckPoint must always pass through
    for (int i = 0; i < 10; i++)
    {
        auto chk = makeMsg(PacketType::CheckPoint, 500 + i, peer);
        BOOST_CHECK_MESSAGE(pipeline.admit(chk, lastApplied),
            "FIB-145: CheckPoint must never be dropped (index=" + std::to_string(500 + i) + ")");
    }
}

// Under normal load (below capacity), all packet types must be admitted.
BOOST_AUTO_TEST_CASE(non_overflow_admits_all)
{
    PBFTPipeline::Config cfg;
    cfg.perPeerCapacity = 100;
    PBFTPipeline pipeline(cfg);

    BlockNumber lastApplied = 10;
    auto peer = makePeerKey("normalPeer");

    // Admit a variety of packet types under normal load
    auto prePrepare = makeMsg(PacketType::PrePreparePacket, 11, peer);
    auto prepare = makeMsg(PacketType::PreparePacket, 11, peer);
    auto commit = makeMsg(PacketType::CommitPacket, 11, peer);
    auto checkpt = makeMsg(PacketType::CheckPoint, 11, peer);

    BOOST_CHECK(pipeline.admit(prePrepare, lastApplied));
    BOOST_CHECK(pipeline.admit(prepare, lastApplied));
    BOOST_CHECK(pipeline.admit(commit, lastApplied));
    BOOST_CHECK(pipeline.admit(checkpt, lastApplied));
    BOOST_CHECK(!pipeline.isPeerBackpressured(peer->hex()));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
