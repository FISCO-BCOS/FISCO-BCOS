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
 * @brief regression tests for FIB-122 (CertiK finding): PBFTProposal must use
 *        an aliasing shared_ptr for the inner proposal submessage so that a
 *        second call to decode() (which triggers ParseFromArray → clears the
 *        proposal field → would delete the submessage still owned by
 *        Proposal::m_rawProposal) does not cause double-free / UAF.
 * @file FIB122_PBFTProposalSubmessageOwnershipTest.cpp
 * @author: kyonRay
 * @date 2026-05-07
 */
#include "bcos-pbft/pbft/protocol/PB/PBFTProposal.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include "bcos-pbft/pbft/utilities/Common.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::consensus;

namespace bcos
{
namespace test
{

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/// Build a serialised PBFTRawProposal with the given index, a stable 32-byte
/// hash, and one signaturelist / nodelist entry.
static bytes makePBFTProposalBytes(int64_t index, const std::string& sig = "sigdata01")
{
    PBFTRawProposal raw;
    raw.mutable_proposal()->set_index(index);
    raw.mutable_proposal()->set_hash(std::string(32, static_cast<char>('A' + (index % 26))));
    raw.add_signaturelist(sig);
    raw.add_nodelist(static_cast<int64_t>(index));
    std::string s = raw.SerializeAsString();
    return bytes(s.begin(), s.end());
}

// ---------------------------------------------------------------------------
// test suite
// ---------------------------------------------------------------------------

BOOST_FIXTURE_TEST_SUITE(FIB122_PBFTProposalSubmessageOwnership, TestPromptFixture)

/// CertiK item #4 (double-decode): constructing a default PBFTProposal then
/// calling decode() twice must not cause a double-free or dangling-pointer
/// crash.  On the unfixed code the second ParseFromArray call deletes the
/// RawProposal still referenced by Proposal::m_rawProposal, corrupting the
/// heap on the setRawProposal call or on destruction.
BOOST_AUTO_TEST_CASE(decode_then_decode_no_double_free)
{
    auto bytes1 = makePBFTProposalBytes(10, "sig_first");
    auto bytes2 = makePBFTProposalBytes(20, "sig_second");

    PBFTProposal p;

    // First decode — initialises the proposal submessage.
    p.decode(bytesConstRef(bytes1.data(), bytes1.size()));
    BOOST_CHECK_EQUAL(p.signatureProofSize(), 1u);

    // Second decode — on unfixed code: ParseFromArray clears the proposal
    // field, freeing the protobuf-owned RawProposal that
    // Proposal::m_rawProposal still holds a non-aliasing owning ptr to.
    // This causes double-free when m_rawProposal's refcount drops to zero.
    BOOST_CHECK_NO_THROW(p.decode(bytesConstRef(bytes2.data(), bytes2.size())));

    // After second decode the fields must reflect bytes2.
    BOOST_CHECK_EQUAL(p.signatureProofSize(), 1u);
    auto [nodeIdx, sigRef] = p.signatureProof(0);
    BOOST_CHECK_EQUAL(nodeIdx, 20);
    std::string sigStr(sigRef.begin(), sigRef.end());
    BOOST_CHECK_EQUAL(sigStr, "sig_second");

    // Third decode to be extra sure (CertiK noted re-entrant calls).
    auto bytes3 = makePBFTProposalBytes(30, "sig_third");
    BOOST_CHECK_NO_THROW(p.decode(bytesConstRef(bytes3.data(), bytes3.size())));
    BOOST_CHECK_EQUAL(p.signatureProofSize(), 1u);
    // p goes out of scope here — must not double-free.
}

/// Default-construct and immediately destroy: verifies the default ctor +
/// dtor pair does not double-free the proposal submessage.  The unfixed code
/// calls set_allocated_proposal(rawProposal().get()) in the default ctor so
/// protobuf owns the memory, then the dtor calls
/// unsafe_arena_release_proposal() to detach — but if that call is absent or
/// wrong, the destructor of PBFTRawProposal and Proposal both try to free the
/// same RawProposal.
BOOST_AUTO_TEST_CASE(default_constructed_then_destroyed_no_leak)
{
    // Simply constructing and destroying PBFTProposal must not crash or
    // trigger an ASan/valgrind report.  We do it multiple times to increase
    // the probability of a deterministic heap-corruption signal.
    for (int i = 0; i < 5; ++i)
    {
        PBFTProposal p;
        // Access a field to ensure the object is non-trivially used.
        BOOST_CHECK_EQUAL(p.signatureProofSize(), 0u);
    }
    // All five instances went out of scope cleanly; no assertions from
    // BOOST mean no crash was observed.
    BOOST_CHECK(true);
}

/// Decode once then access signatureProof; re-decode with different data and
/// verify the new values are correct and the old shared_ptr (m_rawProposal
/// from Proposal base) is not dangling.
BOOST_AUTO_TEST_CASE(decode_followed_by_proposal_access)
{
    auto bytes1 = makePBFTProposalBytes(42, "proof_A");
    PBFTProposal p;
    p.decode(bytesConstRef(bytes1.data(), bytes1.size()));

    // Verify initial decode fields.
    BOOST_REQUIRE_EQUAL(p.signatureProofSize(), 1u);
    {
        auto [idx, sigRef] = p.signatureProof(0);
        BOOST_CHECK_EQUAL(idx, 42);
        std::string sigStr(sigRef.begin(), sigRef.end());
        BOOST_CHECK_EQUAL(sigStr, "proof_A");
    }

    // Encode → re-decode in a second independent object; no shared global
    // state should be corrupted by the first decode.
    auto encoded = p.encode();
    BOOST_REQUIRE(encoded && !encoded->empty());
    PBFTProposal p2;
    p2.decode(bytesConstRef(encoded->data(), encoded->size()));
    BOOST_REQUIRE_EQUAL(p2.signatureProofSize(), 1u);
    {
        auto [idx2, sigRef2] = p2.signatureProof(0);
        BOOST_CHECK_EQUAL(idx2, 42);
        std::string sigStr2(sigRef2.begin(), sigRef2.end());
        BOOST_CHECK_EQUAL(sigStr2, "proof_A");
    }

    // Now re-decode p with different data and confirm no corruption.
    auto bytes2 = makePBFTProposalBytes(99, "proof_B");
    BOOST_CHECK_NO_THROW(p.decode(bytesConstRef(bytes2.data(), bytes2.size())));
    BOOST_REQUIRE_EQUAL(p.signatureProofSize(), 1u);
    {
        auto [idx3, sigRef3] = p.signatureProof(0);
        BOOST_CHECK_EQUAL(idx3, 99);
        std::string sigStr3(sigRef3.begin(), sigRef3.end());
        BOOST_CHECK_EQUAL(sigStr3, "proof_B");
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
