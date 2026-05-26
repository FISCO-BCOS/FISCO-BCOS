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
 * @brief Regression tests for FIB-146: Re-queued future PBFT packets cause a
 *        CPU busy-spin and OOM because the stub Stage 2 LRU dedup always passes
 *        duplicate messages through. The fix replaces the stub PeerLRUCache with
 *        a real per-peer LRU cache that drops recently-seen (type:index:hash) keys.
 * @file FIB146_LRUDedupTest.cpp
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

// Reuse minimal stub message (same structure as FIB145 test)
class LRUStubMsg : public PBFTBaseMessageInterface
{
public:
    using Ptr = std::shared_ptr<LRUStubMsg>;

    LRUStubMsg(PacketType type, int64_t index, KeyInterface::Ptr from, HashType hash = {})
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
    std::string toDebugString() const override { return "LRUStubMsg"; }

private:
    PacketType m_type;
    int64_t m_index;
    KeyInterface::Ptr m_from;
    HashType m_hash;
};

static KeyInterface::Ptr makeLRUPeer(std::string const& tag)
{
    return std::make_shared<KeyImpl>(bytes(tag.begin(), tag.end()));
}

static LRUStubMsg::Ptr makeLRUMsg(
    PacketType type, int64_t index, KeyInterface::Ptr const& from, HashType hash = {})
{
    return std::make_shared<LRUStubMsg>(type, index, from, std::move(hash));
}

BOOST_FIXTURE_TEST_SUITE(FIB146Test, TestPromptFixture)

// A duplicate message (same type, index, hash, peer) must be dropped by Stage 2.
// With the stub (seenAndInsert always false), this test FAILS.
// After the real LRU is implemented, it PASSES.
BOOST_AUTO_TEST_CASE(duplicate_message_dropped_by_lru)
{
    // Use small LRU capacity to keep test fast
    PBFTPipeline::Config cfg;
    cfg.lruCapacity = 8;
    cfg.perPeerCapacity = 100;  // don't hit Stage 3
    PBFTPipeline pipeline(cfg);

    auto peer = makeLRUPeer("peerX");
    HashType hashA;
    hashA[0] = 0xAA;

    // First occurrence of (PrePrepare, index=10, hashA) from peer → admitted
    auto msg1 = makeLRUMsg(PacketType::PrePreparePacket, 10, peer, hashA);
    BOOST_CHECK_MESSAGE(pipeline.admit(msg1, 0), "FIB-146: first occurrence must be admitted");

    // Second occurrence of the identical message → must be dropped (dedup)
    auto msg2 = makeLRUMsg(PacketType::PrePreparePacket, 10, peer, hashA);
    BOOST_CHECK_MESSAGE(!pipeline.admit(msg2, 0),
        "FIB-146 Stage2: duplicate (type:index:hash) from same peer must be dropped");
}

// Different (type, index, hash) tuples from the same peer must NOT interfere.
BOOST_AUTO_TEST_CASE(distinct_messages_all_admitted)
{
    PBFTPipeline::Config cfg;
    cfg.lruCapacity = 16;
    cfg.perPeerCapacity = 100;
    PBFTPipeline pipeline(cfg);

    auto peer = makeLRUPeer("peerY");
    HashType hashB;
    hashB[0] = 0xBB;
    HashType hashC;
    hashC[0] = 0xCC;

    // Same type and index but different hash → different dedup key → both admitted
    auto msg1 = makeLRUMsg(PacketType::PrePreparePacket, 20, peer, hashB);
    auto msg2 = makeLRUMsg(PacketType::PrePreparePacket, 20, peer, hashC);
    BOOST_CHECK(pipeline.admit(msg1, 0));
    BOOST_CHECK_MESSAGE(pipeline.admit(msg2, 0),
        "FIB-146: same (type,index) but different hash must both be admitted");

    // Same type and hash but different index → different dedup key → admitted
    HashType hashD;
    hashD[0] = 0xDD;
    auto msg3 = makeLRUMsg(PacketType::PrePreparePacket, 21, peer, hashD);
    auto msg4 = makeLRUMsg(PacketType::PrePreparePacket, 22, peer, hashD);
    BOOST_CHECK(pipeline.admit(msg3, 0));
    BOOST_CHECK(pipeline.admit(msg4, 0));
}

// LRU eviction: when the cache is full, the least-recently-used entry is evicted.
// After eviction, the re-submitted evicted key should be re-admitted (not deduped).
BOOST_AUTO_TEST_CASE(lru_eviction_allows_reentry)
{
    PBFTPipeline::Config cfg;
    cfg.lruCapacity = 4;  // small cache to force eviction quickly
    cfg.perPeerCapacity = 200;
    PBFTPipeline pipeline(cfg);

    auto peer = makeLRUPeer("peerZ");

    // Submit 4 unique messages to fill the LRU cache
    std::vector<HashType> hashes(4);
    for (int i = 0; i < 4; i++)
    {
        hashes[i][0] = static_cast<uint8_t>(i + 1);
        auto msg = makeLRUMsg(PacketType::PreparePacket, 30 + i, peer, hashes[i]);
        BOOST_CHECK(pipeline.admit(msg, 0));  // all unique → admitted
    }

    // Submit a 5th unique message → causes LRU eviction of the oldest (hashes[0])
    HashType hashNew;
    hashNew[0] = 0xFF;
    auto newMsg = makeLRUMsg(PacketType::PreparePacket, 34, peer, hashNew);
    BOOST_CHECK(pipeline.admit(newMsg, 0));

    // The oldest entry (hashes[0]) was evicted. Re-submitting it should be admitted
    // again (not treated as a duplicate), because it fell out of the LRU cache.
    auto evictedMsg = makeLRUMsg(PacketType::PreparePacket, 30, peer, hashes[0]);
    BOOST_CHECK_MESSAGE(pipeline.admit(evictedMsg, 0),
        "FIB-146: evicted key must be re-admitted after LRU eviction");
}

// FIB-146 Stage 1b: messages with index > lastApplied + 2 * waterMarkLimit are
// dropped at admission so they never enter m_msgQueue. This prevents a Byzantine
// peer from filling the queue with far-future PBFT packets that the worker
// would otherwise repeatedly pop/re-push (CPU busy-spin path).
BOOST_AUTO_TEST_CASE(far_future_preprepare_dropped_at_admission)
{
    PBFTPipeline::Config cfg;
    cfg.perPeerCapacity = 100;
    cfg.lruCapacity = 8;
    PBFTPipeline pipeline(cfg);

    auto peer = makeLRUPeer("peerFuture");
    int64_t lastApplied = 100;
    int64_t waterMark = 50;
    int64_t maxFuture = lastApplied + 2 * waterMark;  // 200

    // index = maxFuture → admitted (boundary inclusive)
    HashType hashAtBound;
    hashAtBound[0] = 0x01;
    auto atBound = makeLRUMsg(PacketType::PrePreparePacket, maxFuture, peer, hashAtBound);
    BOOST_CHECK_MESSAGE(pipeline.admit(atBound, lastApplied, maxFuture),
        "FIB-146 Stage1b: PrePrepare at exactly maxFuture must be admitted");

    // index = maxFuture + 1 → dropped
    HashType hashJustOver;
    hashJustOver[0] = 0x02;
    auto justOver = makeLRUMsg(PacketType::PrePreparePacket, maxFuture + 1, peer, hashJustOver);
    BOOST_CHECK_MESSAGE(!pipeline.admit(justOver, lastApplied, maxFuture),
        "FIB-146 Stage1b: PrePrepare beyond maxFuture must be dropped");

    // Very far future → dropped
    HashType hashFar;
    hashFar[0] = 0x03;
    auto farFuture = makeLRUMsg(PacketType::PrePreparePacket, lastApplied + 10000, peer, hashFar);
    BOOST_CHECK_MESSAGE(!pipeline.admit(farFuture, lastApplied, maxFuture),
        "FIB-146 Stage1b: PrePrepare at lastApplied+10000 must be dropped");
}

// Safety-critical Commit/CheckPoint must bypass Stage 1b (same rationale as
// Stage 1): never drop messages required for consensus liveness.
BOOST_AUTO_TEST_CASE(far_future_commit_still_admitted)
{
    PBFTPipeline::Config cfg;
    cfg.perPeerCapacity = 100;
    cfg.lruCapacity = 8;
    PBFTPipeline pipeline(cfg);

    auto peer = makeLRUPeer("peerCommit");
    int64_t lastApplied = 100;
    int64_t maxFuture = 200;

    HashType hashCommit;
    hashCommit[0] = 0x10;
    auto farCommit = makeLRUMsg(PacketType::CommitPacket, 99999, peer, hashCommit);
    BOOST_CHECK_MESSAGE(pipeline.admit(farCommit, lastApplied, maxFuture),
        "FIB-146 Stage1b: Commit must bypass future-height filter");

    HashType hashChk;
    hashChk[0] = 0x11;
    auto farChk = makeLRUMsg(PacketType::CheckPoint, 99999, peer, hashChk);
    BOOST_CHECK_MESSAGE(pipeline.admit(farChk, lastApplied, maxFuture),
        "FIB-146 Stage1b: CheckPoint must bypass future-height filter");
}

// Default maxFutureIndex (when caller omits the third arg) must preserve
// backward-compatibility — no future-filter is applied. Verifies the default
// argument path so older test cases that pass only two args keep passing.
BOOST_AUTO_TEST_CASE(default_max_future_disables_filter)
{
    PBFTPipeline pipeline;
    auto peer = makeLRUPeer("peerDefault");
    HashType h;
    h[0] = 0x20;
    auto msg = makeLRUMsg(PacketType::PrePreparePacket, 1'000'000'000, peer, h);
    BOOST_CHECK_MESSAGE(
        pipeline.admit(msg, 0), "FIB-146 Stage1b: two-arg admit() must not apply future-filter");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
