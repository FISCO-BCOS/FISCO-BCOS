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
 * @file FIB174_MalformedHashRejectTest.cpp
 * @brief FIB-174: a malformed (non-canonical length) proposal hash must be
 *        rejected at decode rather than silently accepted with a stale or
 *        default hash identity. See audit/findings/FIB-174.md.
 */
#include "bcos-pbft/core/Proposal.h"
#include "bcos-pbft/core/proto/Consensus.pb.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTProposal.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::consensus;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
/// Serialize a PBFTRawProposal whose inner RawProposal carries a `hashLen`-byte
/// hash.  `hashLen != HashType::SIZE` exercises the malformed-decode path of
/// PBFTProposal::decode (which parses a PBFTRawProposal then defers to
/// Proposal::deserializeObject for the inner proposal).
bytes makePBFTProposalBytesWithHashLen(std::size_t hashLen)
{
    PBFTRawProposal raw;
    auto* inner = raw.mutable_proposal();
    inner->set_index(1);
    std::string hash(hashLen, '\xab');
    inner->set_hash(hash.data(), hash.size());
    std::string serialised = raw.SerializeAsString();
    return bytes(serialised.begin(), serialised.end());
}

/// Serialize a bare RawProposal with a `hashLen`-byte hash.  Used by the
/// Proposal::decode reuse case, which parses a RawProposal directly.
bytes makeRawProposalBytesWithHashLen(std::size_t hashLen)
{
    RawProposal raw;
    raw.set_index(1);
    std::string hash(hashLen, '\xab');
    raw.set_hash(hash.data(), hash.size());
    std::string serialised = raw.SerializeAsString();
    return bytes(serialised.begin(), serialised.end());
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB174_MalformedHashReject, TestPromptFixture)

BOOST_AUTO_TEST_CASE(oversize_hash_rejected_on_decode)
{
    // 64-byte hash inside the serialized proposal — decode must throw.
    auto data = makePBFTProposalBytesWithHashLen(64);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_THROW(PBFTProposal{ref}, bcos::Exception);
}

BOOST_AUTO_TEST_CASE(undersize_hash_rejected_on_decode)
{
    auto data = makePBFTProposalBytesWithHashLen(16);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_THROW(PBFTProposal{ref}, bcos::Exception);
}

BOOST_AUTO_TEST_CASE(canonical_hash_accepted_on_decode)
{
    auto data = makePBFTProposalBytesWithHashLen(HashType::SIZE);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_NO_THROW(PBFTProposal{ref});
}

/// An empty hash field (size 0) is the not-yet-set state of a freshly
/// constructed proposal — it must NOT be treated as malformed, otherwise the
/// normal "construct empty, then setHash()" build path (e.g. a default
/// PBFTProposal{}) would throw.  The decoded proposal carries a default
/// (zeroed) hash identity.
BOOST_AUTO_TEST_CASE(empty_hash_accepted_as_unset)
{
    auto data = makeRawProposalBytesWithHashLen(0);
    Proposal proposal{bytesConstRef(data.data(), data.size())};
    BOOST_CHECK_EQUAL(proposal.hash(), HashType{});

    // The default-constructed PBFTProposal (hash never set) must also construct
    // without throwing.
    BOOST_CHECK_NO_THROW(PBFTProposal{});
}

/// FIB-174 core scenario: a Proposal object that has already decoded a valid
/// proposal hash must NOT retain that hash if it is later reused to decode a
/// malformed proposal.  The malformed decode throws, and m_hash must not keep
/// the previous (now stale) identity.
BOOST_AUTO_TEST_CASE(stale_hash_not_retained_after_malformed_reuse)
{
    // First decode: a canonical hash.  Capture the accepted identity.
    auto goodData = makeRawProposalBytesWithHashLen(HashType::SIZE);
    Proposal proposal{bytesConstRef(goodData.data(), goodData.size())};
    std::string goodHashBytes(HashType::SIZE, '\xab');
    HashType good((byte const*)goodHashBytes.data(), HashType::SIZE);
    BOOST_CHECK_EQUAL(proposal.hash(), good);
    BOOST_CHECK(proposal.hash() != HashType{});

    // Second decode on the SAME object with a malformed (oversize) hash must
    // throw, and must clear the previously accepted identity rather than leave
    // the stale hash in place.
    auto badData = makeRawProposalBytesWithHashLen(64);
    BOOST_CHECK_THROW(
        proposal.decode(bytesConstRef(badData.data(), badData.size())), bcos::Exception);
    BOOST_CHECK_EQUAL(proposal.hash(), HashType{});
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
