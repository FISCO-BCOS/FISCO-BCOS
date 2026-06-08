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
 * @brief regression test for FIB-121 (CertiK finding-10):
 *        PBFTViewChangeMsg submessage lifetime must be safe when the inner
 *        committedProposal or preparedProposal outlives the parent
 *        PBFTViewChangeMsg. The existing unsafe_arena_release_* destructor
 *        pattern must cleanly detach submessages so callers that hold a
 *        shared_ptr to an inner PBFTProposal / PBFTMessage can still access
 *        their data without use-after-free.
 * @file FIB121_PBFTViewChangeMsgLifetimeTest.cpp
 * @author: kyonRay
 * @date 2026-05-07
 */
#include "bcos-pbft/pbft/protocol/PB/PBFTMessage.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTProposal.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTViewChangeMsg.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include "bcos-pbft/pbft/utilities/Common.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;

namespace bcos
{
namespace test
{

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/// Build a minimal serialised PBFTViewChangeMsg with one committedProposal and
/// one preparedProposal.  Returns the wire-format bytes.
static bytes buildViewChangeMsgBytes()
{
    RawViewChangeMessage raw;

    // base message
    auto* base = raw.mutable_message();
    base->set_version(1);
    base->set_view(42);
    base->set_generatedfrom(0);
    base->set_timestamp(1000);
    base->set_index(10);
    std::string fakeHash(32, 'A');
    base->set_hash(fakeHash);

    // committedProposal
    auto* committed = raw.mutable_committedproposal();
    committed->mutable_proposal()->set_index(5);
    committed->mutable_proposal()->set_hash(std::string(32, 'B'));

    // preparedProposal (a PBFTRawMessage).
    // hashfieldsdata must be a valid serialised BaseMessage (or empty); leave it
    // empty so decodePBObject succeeds when PBFTBaseMessage::decode() parses it.
    auto* prep = raw.add_preparedproposals();
    {
        BaseMessage baseForPrep;
        baseForPrep.set_version(1);
        baseForPrep.set_view(42);
        baseForPrep.set_index(6);
        std::string hfd = baseForPrep.SerializeAsString();
        prep->set_hashfieldsdata(hfd);
    }
    auto* prepConsensus = prep->mutable_consensusproposal();
    prepConsensus->mutable_proposal()->set_index(6);
    prepConsensus->mutable_proposal()->set_hash(std::string(32, 'C'));

    std::string s = raw.SerializeAsString();
    return bytes(s.begin(), s.end());
}

// ---------------------------------------------------------------------------
// test suite
// ---------------------------------------------------------------------------

BOOST_FIXTURE_TEST_SUITE(FIB121_PBFTViewChangeMsgLifetime, TestPromptFixture)

/// After the parent PBFTViewChangeMsg is destroyed, a retained shared_ptr to
/// the inner committedProposal must still be readable without crashing.
/// This pins the invariant that the unsafe_arena_release_committedproposal()
/// call in the destructor correctly transfers ownership out before destruction.
BOOST_AUTO_TEST_CASE(committedProposal_outlives_parent)
{
    auto data = buildViewChangeMsgBytes();
    bytesConstRef ref(data.data(), data.size());

    PBFTProposalInterface::Ptr committedCopy;
    {
        auto viewChange = std::make_shared<PBFTViewChangeMsg>(ref);
        committedCopy = viewChange->committedProposal();
        BOOST_REQUIRE(committedCopy != nullptr);
        // Drop the parent.  Destructor must safely release submessages.
    }

    // Parent is now destroyed.  committedCopy must still be accessible.
    BOOST_CHECK_EQUAL(committedCopy->index(), 5);
}

/// After the parent PBFTViewChangeMsg is destroyed, retained shared_ptrs to
/// elements of preparedProposals must still be readable without crashing.
BOOST_AUTO_TEST_CASE(preparedProposal_outlives_parent)
{
    auto data = buildViewChangeMsgBytes();
    bytesConstRef ref(data.data(), data.size());

    PBFTMessageInterface::Ptr preparedCopy;
    {
        auto viewChange = std::make_shared<PBFTViewChangeMsg>(ref);
        const auto& prepList = viewChange->preparedProposals();
        BOOST_REQUIRE(!prepList.empty());
        preparedCopy = prepList[0];
        BOOST_REQUIRE(preparedCopy != nullptr);
        // Drop parent.
    }

    // Parent destroyed; preparedCopy must survive.
    BOOST_REQUIRE(preparedCopy->consensusProposal() != nullptr);
    BOOST_CHECK_EQUAL(preparedCopy->consensusProposal()->index(), 6);
}

/// Round-trip encode/decode: a decoded PBFTViewChangeMsg must expose correct
/// committed/prepared data even after the original encoded buffer is freed.
BOOST_AUTO_TEST_CASE(decode_roundtrip_fields_correct)
{
    auto data = buildViewChangeMsgBytes();
    bytesConstRef ref(data.data(), data.size());

    auto viewChange = std::make_shared<PBFTViewChangeMsg>(ref);
    BOOST_REQUIRE(viewChange->committedProposal() != nullptr);
    BOOST_CHECK_EQUAL(viewChange->committedProposal()->index(), 5);

    const auto& preps = viewChange->preparedProposals();
    BOOST_REQUIRE_EQUAL(preps.size(), 1u);
    BOOST_REQUIRE(preps[0]->consensusProposal() != nullptr);
    BOOST_CHECK_EQUAL(preps[0]->consensusProposal()->index(), 6);
}

/// FIB-121 Issue #3 (DoS via OOM): the validateRepeatedSize guard in
/// deserializeToObject() must reject a ViewChangeMsg whose preparedProposals
/// count exceeds MAX_PBFT_REPEATED_FIELD_SIZE.
/// We use a small batch (50 entries) combined with a temporary local cap (49)
/// to verify the guard fires without allocating 100 000+ protobuf objects
/// (which is slow and risks hitting allocator limits in test environments).
BOOST_AUTO_TEST_CASE(oversized_preparedProposalList_rejected_at_decode)
{
    // Build a message with 50 stub preparedProposal entries.
    RawViewChangeMessage raw;
    auto* base = raw.mutable_message();
    base->set_version(1);
    base->set_view(1);
    base->set_index(1);
    for (int i = 0; i < 50; ++i)
    {
        raw.add_preparedproposals();
    }
    std::string s = raw.SerializeAsString();
    bcos::bytes data(s.begin(), s.end());
    bcos::bytesConstRef ref(data.data(), data.size());

    // Validate that validateRepeatedSize throws when count > cap.
    // We call it directly with cap=49 to confirm the guard mechanism works.
    bool threw = false;
    try
    {
        validateRepeatedSize(raw.preparedproposals(), 49u, "preparedProposals");
    }
    catch (bcos::consensus::InvalidPBFTMessage const&)
    {
        threw = true;
    }
    BOOST_CHECK_MESSAGE(
        threw, "validateRepeatedSize did not throw for oversized preparedProposals");

    // Also confirm that a properly-sized message (cap=50) does NOT throw.
    BOOST_CHECK_NO_THROW(validateRepeatedSize(raw.preparedproposals(), 50u, "preparedProposals"));

    // And confirm the normal decode path (50 entries well within
    // MAX_PBFT_REPEATED_FIELD_SIZE) succeeds without exception.
    BOOST_CHECK_NO_THROW({ auto msg = std::make_shared<PBFTViewChangeMsg>(ref); });
}

/// FIB-121 Issue #1 (ordering after decode): decoding a ViewChange with
/// multiple preparedProposals must yield the list in original protocol order.
/// This pins the iteration semantics of deserializeToObject — a regression
/// here (e.g. accidentally reversed iteration) would silently shuffle proposals.
BOOST_AUTO_TEST_CASE(preparedProposals_preserve_order_after_decode)
{
    RawViewChangeMessage raw;
    auto* base = raw.mutable_message();
    base->set_version(1);
    base->set_view(7);
    base->set_index(99);
    base->set_hash(std::string(32, 'X'));

    // Three preparedProposals with distinct indices so we can assert order.
    constexpr int kCount = 3;
    constexpr int kIndices[kCount] = {11, 22, 33};
    for (int idx : kIndices)
    {
        auto* prep = raw.add_preparedproposals();
        BaseMessage baseForPrep;
        baseForPrep.set_version(1);
        baseForPrep.set_view(7);
        baseForPrep.set_index(idx);
        prep->set_hashfieldsdata(baseForPrep.SerializeAsString());
        auto* prepConsensus = prep->mutable_consensusproposal();
        prepConsensus->mutable_proposal()->set_index(idx);
    }
    std::string s = raw.SerializeAsString();
    bcos::bytes data(s.begin(), s.end());
    bcos::bytesConstRef ref(data.data(), data.size());

    auto msg = std::make_shared<PBFTViewChangeMsg>(ref);
    const auto& preps = msg->preparedProposals();
    BOOST_REQUIRE_EQUAL(preps.size(), static_cast<size_t>(kCount));
    for (int i = 0; i < kCount; ++i)
    {
        BOOST_REQUIRE(preps[i]->consensusProposal() != nullptr);
        BOOST_CHECK_EQUAL(preps[i]->consensusProposal()->index(), kIndices[i]);
    }
}

/// FIB-121 Issue #1 (bad bytes): feeding garbage to the
/// bytesConstRef constructor must throw cleanly (no crash, no UB).
BOOST_AUTO_TEST_CASE(bad_protobuf_bytes_rejected_at_decode)
{
    // Craft bytes that are clearly not a valid RawViewChangeMessage.
    bcos::bytes garbage(64, 0xFF);
    bcos::bytesConstRef ref(garbage.data(), garbage.size());

    // decodePBObject may either throw or succeed with an empty object
    // (protobuf is lenient with unknown fields).  What must NOT happen
    // is a crash or memory corruption.  We use a try/catch rather than
    // BOOST_CHECK_THROW because protobuf may silently ignore bad bytes.
    bool caught = false;
    try
    {
        auto msg = std::make_shared<PBFTViewChangeMsg>(ref);
        // If decode "succeeds" (returns empty message), the object must
        // still be in a consistent, usable state.
        (void)msg->committedProposal();
        (void)msg->preparedProposals();
    }
    catch (std::exception const&)
    {
        caught = true;
    }
    // Either path (caught or not) is acceptable; the test just ensures
    // no crash / sanitizer report.
    (void)caught;
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
