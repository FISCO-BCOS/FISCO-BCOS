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
 * @brief Regression test for FIB-149 (PBFT side): Proposal.h and
 *        PBFTBaseMessage.h must reject any non-canonical hash length on
 *        deserialization. Previously Proposal.h used `<` (accepting oversize
 *        silently truncated to 32 bytes) and PBFTBaseMessage.h used `>=`
 *        (also truncating oversize). Both are now strict `!= HashType::SIZE`.
 * @file FIB149_HashLengthStrictTest.cpp
 */
#include "bcos-pbft/core/Proposal.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTBaseMessage.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTProposal.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
/// Concrete PBFTBaseMessage subtype: PBFTBaseMessage's deserializeToObject is
/// virtual but the base class has no abstract methods triggered through the
/// constructor. We can directly instantiate via the protected baseMessage ctor.
class FibTestPBFTBaseMessage : public PBFTBaseMessage
{
public:
    using PBFTBaseMessage::PBFTBaseMessage;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB149_HashLengthStrict, TestPromptFixture)

BOOST_AUTO_TEST_CASE(oversize_proposal_hash_rejected)
{
    // 64-byte (oversize) raw hash inside a RawProposal. Pre-fix Proposal.h used
    // `<` so oversize was silently truncated to the first 32 bytes; the test
    // asserts the strict reject path leaves m_hash default-constructed.
    auto raw = std::make_shared<RawProposal>();
    std::string oversize(64, '\xab');
    raw->set_hash(oversize.data(), oversize.size());

    Proposal proposal(raw);
    BOOST_CHECK_EQUAL(proposal.hash(), HashType{});
}

BOOST_AUTO_TEST_CASE(undersize_proposal_hash_rejected)
{
    // Already rejected pre-fix (the `<` predicate caught undersize), but we
    // keep this case to lock in behavior post-fix.
    auto raw = std::make_shared<RawProposal>();
    std::string undersize(16, '\xab');
    raw->set_hash(undersize.data(), undersize.size());

    Proposal proposal(raw);
    BOOST_CHECK_EQUAL(proposal.hash(), HashType{});
}

BOOST_AUTO_TEST_CASE(exact_proposal_hash_accepted)
{
    auto raw = std::make_shared<RawProposal>();
    std::string exact(HashType::SIZE, '\xcd');
    raw->set_hash(exact.data(), exact.size());

    Proposal proposal(raw);
    HashType expected((byte const*)exact.data(), HashType::SIZE);
    BOOST_CHECK_EQUAL(proposal.hash(), expected);
}

BOOST_AUTO_TEST_CASE(oversize_pbft_basemsg_hash_rejected)
{
    // Pre-fix PBFTBaseMessage used `>=` so oversize input was silently
    // truncated. The strict `!=` check leaves m_hash default-constructed.
    auto base = std::make_shared<BaseMessage>();
    std::string oversize(64, '\xab');
    base->set_hash(oversize.data(), oversize.size());

    FibTestPBFTBaseMessage msg(base);
    BOOST_CHECK_EQUAL(msg.hash(), HashType{});
}

BOOST_AUTO_TEST_CASE(undersize_pbft_basemsg_hash_rejected)
{
    // Pre-fix `>=` predicate let undersize fall through (m_hash stayed default
    // because the construct did not run). After fix the same is true via the
    // explicit `==` predicate. Locks in canonical behavior.
    auto base = std::make_shared<BaseMessage>();
    std::string undersize(16, '\xab');
    base->set_hash(undersize.data(), undersize.size());

    FibTestPBFTBaseMessage msg(base);
    BOOST_CHECK_EQUAL(msg.hash(), HashType{});
}

BOOST_AUTO_TEST_CASE(oversize_pbft_basemsg_signaturehash_rejected)
{
    auto base = std::make_shared<BaseMessage>();
    std::string okHash(HashType::SIZE, '\x11');
    base->set_hash(okHash.data(), okHash.size());
    // signaturehash oversize (64) — pre-fix would silently truncate to 32 bytes.
    std::string sigOversize(64, '\xab');
    base->set_signaturehash(sigOversize.data(), sigOversize.size());

    FibTestPBFTBaseMessage msg(base);
    BOOST_CHECK_EQUAL(msg.signatureDataHash(), HashType{});
    // hash itself is canonical; ensure independent rejection.
    HashType expectedHash((byte const*)okHash.data(), HashType::SIZE);
    BOOST_CHECK_EQUAL(msg.hash(), expectedHash);
}

BOOST_AUTO_TEST_CASE(exact_pbft_basemsg_hashes_accepted)
{
    auto base = std::make_shared<BaseMessage>();
    std::string h(HashType::SIZE, '\x42');
    std::string s(HashType::SIZE, '\x84');
    base->set_hash(h.data(), h.size());
    base->set_signaturehash(s.data(), s.size());

    FibTestPBFTBaseMessage msg(base);
    HashType eh((byte const*)h.data(), HashType::SIZE);
    HashType es((byte const*)s.data(), HashType::SIZE);
    BOOST_CHECK_EQUAL(msg.hash(), eh);
    BOOST_CHECK_EQUAL(msg.signatureDataHash(), es);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
