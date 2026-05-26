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
 * @brief regression tests for FIB-120 and FIB-123: PBFTProposal::decode()
 *        must reject repeated fields that exceed MAX_PBFT_REPEATED_FIELD_SIZE
 * @file FIB120_PBFTProposalDecodeBoundsTest.cpp
 * @author: kyonRay
 * @date 2026-05-07
 */
#include "bcos-pbft/pbft/protocol/PB/PBFTProposal.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include "bcos-pbft/pbft/utilities/Common.h"
#include <bcos-utilities/Exceptions.h>
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

/// Build a serialised PBFTRawProposal with `sigCount` signaturelist entries
/// and `nodeCount` nodelist entries.  All entries are trivially sized.
static bytes makePBFTProposalBytes(int sigCount, int nodeCount)
{
    PBFTRawProposal raw;
    // fill a minimal inner proposal so decodePBObject succeeds
    raw.mutable_proposal()->set_index(1);
    raw.mutable_proposal()->set_hash("hash");

    for (int i = 0; i < sigCount; ++i)
    {
        raw.add_signaturelist("sig");
    }
    for (int i = 0; i < nodeCount; ++i)
    {
        raw.add_nodelist(static_cast<int64_t>(i));
    }

    std::string serialised = raw.SerializeAsString();
    return bytes(serialised.begin(), serialised.end());
}

// ---------------------------------------------------------------------------
// test suite
// ---------------------------------------------------------------------------

BOOST_FIXTURE_TEST_SUITE(FIB120_PBFTProposalDecodeBounds, TestPromptFixture)

/// FIB-120: signatureList with more than MAX_PBFT_REPEATED_FIELD_SIZE entries
/// must be rejected by PBFTProposal::decode().
BOOST_AUTO_TEST_CASE(decode_rejects_excessive_signatureList)
{
    // 100001 > MAX_PBFT_REPEATED_FIELD_SIZE (100000) — must throw
    auto data = makePBFTProposalBytes(100001, 100001);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_THROW(PBFTProposal{ref}, bcos::Exception);
}

/// Normal-sized lists (100 entries each) must be accepted without exception.
BOOST_AUTO_TEST_CASE(decode_accepts_normal_signatureList)
{
    auto data = makePBFTProposalBytes(100, 100);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_NO_THROW(PBFTProposal{ref});
}

/// FIB-123: nodeList with more than MAX_PBFT_REPEATED_FIELD_SIZE entries
/// must be rejected by PBFTProposal::decode().
BOOST_AUTO_TEST_CASE(decode_rejects_excessive_nodeList_FIB123)
{
    // nodeList overflows; signatureList is also above the cap to keep them in
    // sync with the FIB-120 mismatch rule — what we are exercising here is the
    // size cap, not the mismatch check.
    auto data = makePBFTProposalBytes(100001, 100001);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_THROW(PBFTProposal{ref}, bcos::Exception);
}

/// FIB-120: the exact attack scenario described by CertiK — a crafted protobuf
/// where signatureList.size() > nodeList.size().  Without the mismatch check,
/// downstream code iterating `i < signatureProofSize()` would index `nodelist(i)`
/// out of bounds.  decode() must reject the object.
BOOST_AUTO_TEST_CASE(decode_rejects_signatureList_larger_than_nodeList_FIB120)
{
    auto data = makePBFTProposalBytes(/*sigCount=*/5, /*nodeCount=*/2);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_THROW(PBFTProposal{ref}, bcos::Exception);
}

/// FIB-120: the inverse direction — signatureList.size() < nodeList.size().
/// signatureProof(i) is bound by signatureProofSize() so this direction is not
/// the original OOB path, but it still indicates a malformed proposal and must
/// be rejected for symmetry and to satisfy CertiK's "1:1 correspondence" rule.
BOOST_AUTO_TEST_CASE(decode_rejects_signatureList_smaller_than_nodeList_FIB120)
{
    auto data = makePBFTProposalBytes(/*sigCount=*/2, /*nodeCount=*/5);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_THROW(PBFTProposal{ref}, bcos::Exception);
}

/// FIB-120: a particularly nasty case — non-empty signatureList with empty
/// nodeList.  signatureProof(0) would read nodelist(0) on an empty repeated
/// field, which is the textbook UB CertiK describes.
BOOST_AUTO_TEST_CASE(decode_rejects_nonempty_signatureList_empty_nodeList_FIB120)
{
    auto data = makePBFTProposalBytes(/*sigCount=*/1, /*nodeCount=*/0);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_THROW(PBFTProposal{ref}, bcos::Exception);
}

/// FIB-120 defensive: signatureProof() must not be reachable with an
/// out-of-bounds index, regardless of how the underlying proposal got there.
/// Build a PBFTRawProposal manually with mismatched sizes and confirm
/// signatureProof() throws rather than triggering UB.
BOOST_AUTO_TEST_CASE(signatureProof_throws_on_out_of_bounds_FIB120)
{
    auto raw = std::make_shared<PBFTRawProposal>();
    raw->mutable_proposal()->set_index(1);
    raw->mutable_proposal()->set_hash("hash");
    // signature is set but nodelist is empty — index 0 is OOB on nodelist.
    raw->add_signaturelist("sig");

    PBFTProposal proposal{raw};  // direct construction bypasses decode()

    BOOST_CHECK_THROW(proposal.signatureProof(0), bcos::Exception);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
