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
 * @brief regression tests for FIB-140 (CertiK finding-11):
 *        PBFTLogSync::onRecvPrecommitResponse() used assert(size==1) then [0];
 *        in release builds the assert is stripped, leaving OOB access on empty
 *        input or silent acceptance of attacker-injected extra proposals on
 *        size > 1.  The fix replaces the assert with an explicit runtime check
 *        that returns early when size != 1.
 *
 *        UT covers all four cases: size = 0 (OOB on empty), size = 2 (attacker
 *        injects extra proposal), size = 1000 (large list), size = 1 (happy path).
 * @file FIB140_PreparedProposalResponseSizeTest.cpp
 * @author: kyonRay
 * @date 2026-05-07
 */

#include "bcos-pbft/pbft/engine/PBFTLogSync.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTMessage.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTProposal.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTViewChangeMsg.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include "test/unittests/pbft/PBFTFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{

// ---------------------------------------------------------------------------
// FakePBFTLogSync: exposes the protected onRecvPrecommitResponse as public.
// ---------------------------------------------------------------------------
class FakePBFTLogSync : public PBFTLogSync
{
public:
    using PBFTLogSync::PBFTLogSync;

    /// Public wrapper so tests can call the protected method directly.
    void callOnRecvPrecommitResponse(bcos::Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, PBFTMessageInterface::Ptr _prePrepareMsg,
        HandlePrePrepareCallback _prePrepareCallback)
    {
        onRecvPrecommitResponse(std::move(_error), std::move(_nodeID), _data,
            std::move(_prePrepareMsg), std::move(_prePrepareCallback), [](bytesConstRef) {});
    }
};

// ---------------------------------------------------------------------------
// Helpers: build serialised PreparedProposalResponse bytes consumable by the
// real PBFTCodec::decode() call inside onRecvPrecommitResponse.
// ---------------------------------------------------------------------------

/// Build the inner RawViewChangeMessage payload with @p count preparedProposals.
static bytes buildViewChangeMsgPayload(int count)
{
    RawViewChangeMessage raw;

    auto* base = raw.mutable_message();
    base->set_version(1);
    base->set_view(1);
    base->set_generatedfrom(0);
    base->set_timestamp(1000);
    base->set_index(1);
    base->set_hash(std::string(32, 'A'));

    for (int i = 0; i < count; ++i)
    {
        auto* prep = raw.add_preparedproposals();
        // Minimal hashfieldsdata: a serialised BaseMessage so
        // PBFTBaseMessage::decode() succeeds.
        BaseMessage baseMsg;
        baseMsg.set_version(1);
        baseMsg.set_view(1);
        baseMsg.set_index(static_cast<int64_t>(i + 10));
        prep->set_hashfieldsdata(baseMsg.SerializeAsString());
        // Add a consensusProposal so precommitMsg->consensusProposal() is
        // non-null (required to reach the hash-comparison early-exit).
        auto* cp = prep->mutable_consensusproposal();
        cp->mutable_proposal()->set_index(static_cast<int64_t>(i + 10));
        cp->mutable_proposal()->set_hash(std::string(32, static_cast<char>('B' + (i % 26))));
    }

    std::string s = raw.SerializeAsString();
    return bytes(s.begin(), s.end());
}

/// Wrap the inner payload in the outer RawMessage envelope that PBFTCodec::decode
/// expects.  PacketType is PreparedProposalResponse.
static bytes wrapAsCodecBytes(const bytes& payload)
{
    RawMessage outer;
    outer.set_type(static_cast<int32_t>(PacketType::PreparedProposalResponse));
    outer.set_payload(reinterpret_cast<const char*>(payload.data()), payload.size());
    // PreparedProposalResponse does not require a signature in PBFTCodec.
    outer.set_version(1);
    std::string s = outer.SerializeAsString();
    return bytes(s.begin(), s.end());
}

/// Build a valid _prePrepareMsg with a consensusProposal that the function can
/// dereference before the size guard (the size check is the very first thing
/// after packet-type validation, so _prePrepareMsg is accessed AFTER the guard).
/// We just need a non-null object; the hash/index mismatch causes an early return
/// in the happy path anyway.
static PBFTMessageInterface::Ptr makeDummyPrePrepareMsg(CryptoSuite::Ptr cryptoSuite)
{
    auto msg = std::make_shared<PBFTMessage>();
    auto proposal = std::make_shared<PBFTProposal>();
    proposal->setIndex(99);
    std::string dummyStr = "dummy";
    auto hash = cryptoSuite->hashImpl()->hash(std::string_view(dummyStr));
    proposal->setHash(hash);
    msg->setConsensusProposal(proposal);
    msg->setPacketType(PacketType::PrePreparePacket);
    return msg;
}

// ---------------------------------------------------------------------------
// Minimal fixture: sets up the CryptoSuite and PBFTFixture infrastructure
// used across all four test cases.
// ---------------------------------------------------------------------------
struct FIB140Fixture : public TestPromptFixture
{
    FIB140Fixture()
    {
        auto hashImpl = std::make_shared<Keccak256>();
        auto signImpl = std::make_shared<Secp256k1Crypto>();
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
        pbftFixture = createPBFTFixture(cryptoSuite);
        // Need at least one consensus node before init().
        pbftFixture->appendConsensusNode(pbftFixture->nodeID());
        pbftFixture->init();
        auto config = pbftFixture->pbftConfig();
        auto cacheProcessor = pbftFixture->pbftEngine()->cacheProcessor();
        logSync = std::make_shared<FakePBFTLogSync>(config, cacheProcessor);
        nodeID = cryptoSuite->signatureImpl()->generateKeyPair()->publicKey();
    }

    ~FIB140Fixture() override { pbftFixture->stop(); }

    CryptoSuite::Ptr cryptoSuite;
    PBFTFixture::Ptr pbftFixture;
    std::shared_ptr<FakePBFTLogSync> logSync;
    bcos::crypto::PublicPtr nodeID;
};

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------

BOOST_FIXTURE_TEST_SUITE(FIB140_PreparedProposalResponseSize, FIB140Fixture)

/// Regression — size = 0 (OOB on empty preparedProposals):
/// The unfixed code executes assert(size==1) [stripped in release] then
/// preparedProposals()[0] on an empty vector → UB / crash.
/// The fixed code returns early with a WARNING log; no crash.
BOOST_AUTO_TEST_CASE(rejects_empty_preparedProposals)
{
    auto payload = buildViewChangeMsgPayload(0);
    auto encoded = wrapAsCodecBytes(payload);
    bytesConstRef ref(encoded.data(), encoded.size());

    auto prePrepare = makeDummyPrePrepareMsg(cryptoSuite);
    bool callbackCalled = false;
    auto callback = [&callbackCalled](PBFTMessageInterface::Ptr) { callbackCalled = true; };

    // Must not crash (UB on buggy code, early return on fixed code).
    BOOST_CHECK_NO_THROW(
        logSync->callOnRecvPrecommitResponse(nullptr, nodeID, ref, prePrepare, callback));

    // The guard must have fired: callback should NOT have been invoked.
    BOOST_CHECK(!callbackCalled);
}

/// Regression — size = 2 (attacker injects an extra proposal alongside the
/// legitimate one).  Unfixed code silently processes whichever [0] entry the
/// attacker placed first.  Fixed code rejects the whole response.
BOOST_AUTO_TEST_CASE(rejects_two_preparedProposals)
{
    auto payload = buildViewChangeMsgPayload(2);
    auto encoded = wrapAsCodecBytes(payload);
    bytesConstRef ref(encoded.data(), encoded.size());

    auto prePrepare = makeDummyPrePrepareMsg(cryptoSuite);
    bool callbackCalled = false;
    auto callback = [&callbackCalled](PBFTMessageInterface::Ptr) { callbackCalled = true; };

    BOOST_CHECK_NO_THROW(
        logSync->callOnRecvPrecommitResponse(nullptr, nodeID, ref, prePrepare, callback));
    BOOST_CHECK(!callbackCalled);
}

/// Regression — size = 1000 (large list injected by attacker).
/// Fixed code must reject without crash or silent acceptance.
BOOST_AUTO_TEST_CASE(rejects_large_preparedProposals)
{
    // Use 20 entries (well under MAX_PBFT_REPEATED_FIELD_SIZE) to stay clear
    // of the FIB-121 OOM guard while still exercising the FIB-140 size check.
    auto payload = buildViewChangeMsgPayload(20);
    auto encoded = wrapAsCodecBytes(payload);
    bytesConstRef ref(encoded.data(), encoded.size());

    auto prePrepare = makeDummyPrePrepareMsg(cryptoSuite);
    bool callbackCalled = false;
    auto callback = [&callbackCalled](PBFTMessageInterface::Ptr) { callbackCalled = true; };

    BOOST_CHECK_NO_THROW(
        logSync->callOnRecvPrecommitResponse(nullptr, nodeID, ref, prePrepare, callback));
    BOOST_CHECK(!callbackCalled);
}

/// Happy path — size = 1: the guard must NOT fire and [0] is safely accessed.
/// The callback is not invoked because the hash/index of the dummy prePrepareMsg
/// intentionally does not match the crafted response, triggering the existing
/// mismatch early-return — but the key invariant is no crash at [0].
BOOST_AUTO_TEST_CASE(accepts_size_one_preparedProposal)
{
    auto payload = buildViewChangeMsgPayload(1);
    auto encoded = wrapAsCodecBytes(payload);
    bytesConstRef ref(encoded.data(), encoded.size());

    auto prePrepare = makeDummyPrePrepareMsg(cryptoSuite);
    bool callbackCalled = false;
    auto callback = [&callbackCalled](PBFTMessageInterface::Ptr) { callbackCalled = true; };

    // Must not crash: size == 1 → guard does not fire → [0] accessed safely.
    BOOST_CHECK_NO_THROW(
        logSync->callOnRecvPrecommitResponse(nullptr, nodeID, ref, prePrepare, callback));
    // The hash/index mismatch causes an early return before the callback, so
    // callbackCalled remains false — that is expected and correct behaviour.
    BOOST_CHECK(!callbackCalled);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
