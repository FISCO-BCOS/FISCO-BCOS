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
 * @brief Tests for PBFTPipeline peer-cache LRU eviction (FIB-146 follow-up).
 *        Verifies that m_lruCaches is capped at maxPeers and that the
 *        least-recently-used peer's dedup state is dropped when the cap is hit.
 * @file PBFTPipelineEvictionTest.cpp
 * @author: kyonRay
 * @date 2026-05-12
 */
#include "bcos-pbft/pbft/utilities/PBFTPipeline.h"
#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{

class EvictionStubMsg : public PBFTBaseMessageInterface
{
public:
    EvictionStubMsg(int64_t index, KeyInterface::Ptr from, HashType hash)
      : m_index(index), m_from(std::move(from)), m_hash(hash)
    {}
    PacketType packetType() const override { return PacketType::PrePreparePacket; }
    int64_t index() const override { return m_index; }
    bcos::crypto::HashType const& hash() const override { return m_hash; }
    ViewType view() const override { return 0; }
    IndexType generatedFrom() const override { return 0; }
    int64_t timestamp() const override { return 0; }
    int32_t version() const override { return 0; }
    void setIndex(int64_t newIndex) override { m_index = newIndex; }
    void setTimestamp(int64_t) override {}
    void setVersion(int32_t) override {}
    void setView(ViewType) override {}
    void setGeneratedFrom(IndexType) override {}
    void setHash(bcos::crypto::HashType const& newHash) override { m_hash = newHash; }
    void setPacketType(PacketType) override {}
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
    std::string toDebugString() const override { return "EvictionStubMsg"; }

private:
    int64_t m_index;
    KeyInterface::Ptr m_from;
    HashType m_hash;
};

static KeyInterface::Ptr peer(std::string const& tag)
{
    return std::make_shared<KeyImpl>(bytes(tag.begin(), tag.end()));
}

static std::shared_ptr<EvictionStubMsg> mkMsg(
    int64_t idx, KeyInterface::Ptr peerId, uint8_t hashByte)
{
    HashType hash;
    hash[0] = hashByte;
    return std::make_shared<EvictionStubMsg>(idx, std::move(peerId), hash);
}

BOOST_FIXTURE_TEST_SUITE(PBFTPipelineEvictionTest, TestPromptFixture)

// (a) When maxPeers is hit, the least-recently-used peer's cache must be
// evicted. After eviction, re-admitting that peer's previously-seen key
// returns true (newly inserted) rather than false (deduped).
BOOST_AUTO_TEST_CASE(peer_cache_lru_evicts_least_recently_used)
{
    PBFTPipeline::Config cfg;
    cfg.maxPeers = 3;            // tight cap to drive eviction
    cfg.perPeerCapacity = 1000;  // don't trigger Stage 3
    cfg.lruCapacity = 8;
    PBFTPipeline pipeline(cfg);

    auto pA = peer("A");
    auto pB = peer("B");
    auto pC = peer("C");
    auto pD = peer("D");

    // Seed dedup keys for A, B, C (order: A oldest, C newest)
    BOOST_CHECK(pipeline.admit(mkMsg(10, pA, 0x01), 0));
    BOOST_CHECK(pipeline.admit(mkMsg(10, pB, 0x02), 0));
    BOOST_CHECK(pipeline.admit(mkMsg(10, pC, 0x03), 0));

    // Re-admit A's same key — deduped (returns false). This also bumps A to MRU.
    BOOST_CHECK_MESSAGE(
        !pipeline.admit(mkMsg(10, pA, 0x01), 0), "A's key still in cache before eviction");

    // Order is now B (oldest), C, A (newest). Adding D should evict B.
    BOOST_CHECK(pipeline.admit(mkMsg(10, pD, 0x04), 0));

    // BEFORE re-admitting B (which would trigger another eviction at cap),
    // verify A and C are still present. Their old keys must still be deduped.
    BOOST_CHECK_MESSAGE(!pipeline.admit(mkMsg(10, pC, 0x03), 0),
        "C's cache should still be present after D evicts B");
    BOOST_CHECK_MESSAGE(
        !pipeline.admit(mkMsg(10, pA, 0x01), 0), "A's cache should still be present (was MRU)");

    // Now re-admit B's old key. B was evicted, so the key must NOT be deduped.
    // (Note: this itself will evict whoever is now the LRU peer, but we've
    // already verified A/C/D above.)
    BOOST_CHECK_MESSAGE(pipeline.admit(mkMsg(10, pB, 0x02), 0),
        "B's cache should have been evicted (LRU) and the old key should not dedup");
}

// Sanity check: with default maxPeers (1024), no eviction at small scale.
BOOST_AUTO_TEST_CASE(peer_cache_no_eviction_under_default_cap)
{
    PBFTPipeline pipeline;  // default Config
    for (int i = 0; i < 100; ++i)
    {
        auto pId = peer("peer" + std::to_string(i));
        BOOST_CHECK(pipeline.admit(mkMsg(10, pId, static_cast<uint8_t>(i)), 0));
    }
    // Re-admit each peer's first key — all should still be deduped.
    for (int i = 0; i < 100; ++i)
    {
        auto pId = peer("peer" + std::to_string(i));
        BOOST_CHECK(!pipeline.admit(mkMsg(10, pId, static_cast<uint8_t>(i)), 0));
    }
}

// (d) reset() clears Stage 2 LRU caches AND Stage 3 counters.
BOOST_AUTO_TEST_CASE(reset_clears_all_pipeline_state)
{
    PBFTPipeline::Config cfg;
    cfg.maxPeers = 100;
    cfg.lruCapacity = 8;
    cfg.perPeerCapacity = 1000;
    PBFTPipeline pipeline(cfg);

    auto pA = peer("RA");
    auto pB = peer("RB");

    // Seed cache for A and B
    BOOST_CHECK(pipeline.admit(mkMsg(10, pA, 0x01), 0));
    BOOST_CHECK(pipeline.admit(mkMsg(10, pB, 0x02), 0));
    // Confirm dedup works before reset
    BOOST_CHECK(!pipeline.admit(mkMsg(10, pA, 0x01), 0));

    // reset() — all dedup state must be cleared
    pipeline.reset();

    BOOST_CHECK_MESSAGE(
        pipeline.admit(mkMsg(10, pA, 0x01), 0), "reset() must clear Stage 2 LRU caches");
    BOOST_CHECK_MESSAGE(
        pipeline.admit(mkMsg(10, pB, 0x02), 0), "reset() must clear all peer caches, not just one");
}

// Stage 2 dedup eviction-on-consume.
//
// Before fix: any message that reached admit() permanently poisoned the LRU,
// so a transient downstream rejection (txpool verify, signature, etc.) caused
// the leader's retry to be silently dropped, and ViewChange messages whose
// dedup key (type=3:index:propHash) collides across consecutive toView rounds
// got dedup-trapped. After fix: consumed() releases the LRU slot so the next
// arrival of the same key is admitted.
BOOST_AUTO_TEST_CASE(consumed_evicts_stage2_dedup_entry)
{
    PBFTPipeline::Config cfg;
    cfg.lruCapacity = 8;
    cfg.perPeerCapacity = 100;
    PBFTPipeline pipeline(cfg);

    auto pX = peer("RX");
    auto first = mkMsg(10, pX, 0x01);

    BOOST_CHECK(pipeline.admit(first, 0));
    // Same key arriving while the previous one is still in m_msgQueue
    // (not yet consumed): must dedup.
    BOOST_CHECK_MESSAGE(!pipeline.admit(mkMsg(10, pX, 0x01), 0),
        "duplicate arrival before consumed() must still be deduped");

    // Worker pops the first message — Stage 2 LRU entry is released.
    pipeline.consumed(first);

    // A subsequent arrival of the same key MUST be admitted (this is the
    // PBFTEngineTest/testHandlePrePrepareMsg regression case).
    BOOST_CHECK_MESSAGE(pipeline.admit(mkMsg(10, pX, 0x01), 0),
        "after consumed(), the same dedup key must be admitted on re-arrival");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
