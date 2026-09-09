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
 * @file EthGetProofHistoricalTest.cpp
 * @brief eth_getProof at a PAST block, over the real stack (pathdb spec §10.2).
 *
 *        A path-addressed node store keeps one version per position, so the node rows alone
 *        can only prove the tip. Each position's version at the requested block comes from the
 *        trie-node reverse history the commit path writes, and the proof is then produced by
 *        the unchanged walk — every node still verified against the hash its parent records,
 *        which is what makes an old version trustworthy rather than merely plausible.
 *
 *        The chain is real: FullChainFixture's Ledger genesis, real BaselineScheduler blocks on
 *        real RocksDB, so the history rows under test are the ones a commit actually wrote.
 *        The verdict is the independent verifier (mpt::verifyProof), against the header root
 *        the chain committed at that height — the same oracle EthGetProofIntegrationTest uses
 *        for the tip.
 */
#include "transaction-scheduler/tests/FullChainFixture.h"
// Shared test-asset helper (see bcos-ledger genesis tests): the SystemConfig predeploy alloc
// must carry the feature_flags Entry slot Ledger verifies.
#include "../../../../bcos-ledger/test/unittests/ledger/GenesisFeatureFlagsHelper.h"
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/history/HistoryDepths.h>
#include <bcos-ledger/mpt/history/HistoryRead.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <string>
#include <vector>

namespace
{
using namespace bcos;
using namespace bcos::test::fullchain;
namespace mpt = bcos::ledger::mpt;

using EgpNodeStorage =
    storage2::memory_storage::MemoryStorage<mpt::PathKey, bytes, storage2::memory_storage::ORDERED>;

/// Snapshot every committed node row from the fixture's RocksDB backend — the CURRENT version
/// of each position, which is what the history's "unchanged since B" answer falls back to.
void egpLoadNodes(FullChainFixture& fixture, EgpNodeStorage& nodes)
{
    task::syncWait([&]() -> task::Task<void> {
        auto iterator = co_await storage2::range(fixture.m_multiLayerStorage.latestBackend());
        while (auto keyValue = co_await iterator.next())
        {
            auto&& [key, value] = *keyValue;
            if (auto position = mpt::parsePathNodeStateKey(key))
            {
                auto const* entry = std::get_if<storage::Entry>(std::addressof(value));
                BOOST_REQUIRE(entry != nullptr);
                auto raw = entry->get();
                co_await storage2::writeOne(
                    nodes, std::move(*position), bytes(raw.begin(), raw.end()));
            }
        }
    }());
}

/// EthEndpoint over the fixture's real ledger, with both MPT handles wired the way
/// AirNodeInitializer wires them: the node reader over the current node rows, the history
/// reader over the committed backend (the only plane that can seek).
struct EgpEndpointHarness
{
    rpc::NodeService::Ptr m_nodeService;
    std::unique_ptr<rpc::EthEndpoint> m_endpoint;

    EgpEndpointHarness(
        FullChainFixture& fixture, EgpNodeStorage& nodes, protocol::BlockNumber proofDepth)
    {
        m_nodeService = std::make_shared<rpc::NodeService>(fixture.m_ledger, nullptr,
            fixture.m_txpool, nullptr, nullptr, fixture.m_blockFactory, nullptr);
        m_nodeService->setMPTNodeReader(std::make_shared<rpc::NodeService::MPTNodeReader>(nodes));
        m_nodeService->setMPTHistoryReader(
            mpt::history::makeHistoryReader(fixture.m_multiLayerStorage.latestBackend()));
        m_nodeService->setMPTHistoryDepths({.state = proofDepth, .proof = proofDepth});
        m_endpoint = std::make_unique<rpc::EthEndpoint>(m_nodeService, nullptr, false);
    }

    /// Direct endpoint call with EIP-1186 params; JsonRpcException escapes to the caller.
    Json::Value getProof(std::string const& addressHex, std::string tag)
    {
        return getProofWithSlots(addressHex, {}, std::move(tag));
    }

    Json::Value getProofWithSlots(
        std::string const& addressHex, std::vector<std::string> const& slots, std::string tag)
    {
        Json::Value params(Json::arrayValue);
        params.append(addressHex);
        Json::Value slotsJson(Json::arrayValue);
        for (auto const& slot : slots)
        {
            slotsJson.append(slot);
        }
        params.append(std::move(slotsJson));
        params.append(std::move(tag));
        Json::Value response;
        task::syncWait(m_endpoint->getProof(params, response));
        return response["result"];
    }
};

mpt::EIP1186Proof egpProofFromJson(Json::Value const& result)
{
    mpt::EIP1186Proof out;
    out.address = Address(result["address"].asString(), Address::FromHex);
    out.balance = fromBigQuantity(result["balance"].asString());
    out.nonce = fromBigQuantity(result["nonce"].asString());
    out.codeHash = h256(result["codeHash"].asString(), h256::FromHex);
    out.storageHash = h256(result["storageHash"].asString(), h256::FromHex);
    for (auto const& node : result["accountProof"])
    {
        out.accountProof.push_back(fromHexWithPrefix(node.asString()));
    }
    return out;
}

/// Blocks 1-2 run in the XOR era (activation at 2), so the MPT blocks are 3, 4 and 5. The
/// probed account's balance changes at every MPT block, which is what makes a wrong version
/// visible in the proof's own balance field as well as in the verifier's verdict.
struct EgpChain
{
    Address account{FullChainFixture::makeAddress(0xAC)};
    protocol::BlockHeader::Ptr header3;
    protocol::BlockHeader::Ptr header4;
    protocol::BlockHeader::Ptr header5;

    void run(FullChainFixture& fixture)
    {
        fixture.buildGenesis(FullChainFixture::baseGenesis());
        fixture.enableFeatureFromBlock("feature_mpt_state_root", 2);
        // Production injects these from nodeConfig [storage]; the scheduler defaults to 0, so a
        // fixture that wants history has to ask for it.
        fixture.m_baselineScheduler.setHistoryDepths({.state = 128, .proof = 128});

        auto const filler = FullChainFixture::makeAddress(0xF1);
        fixture.planBlock(1, {FullChainFixture::balanceRow(account, "1000")});  // XOR era
        fixture.planBlock(2, {FullChainFixture::balanceRow(filler, "1")});      // activation
        fixture.planBlock(3, {FullChainFixture::balanceRow(account, "3000"),
                                 FullChainFixture::nonceRow(account, "3")});  // first MPT
        fixture.planBlock(4, {FullChainFixture::balanceRow(account, "4000")});
        fixture.planBlock(5, {FullChainFixture::balanceRow(account, "5000")});
        for (protocol::BlockNumber number = 1; number <= 5; ++number)
        {
            fixture.runBlock(number);
        }
        header3 = fixture.headerOnChain(3);
        header4 = fixture.headerOnChain(4);
        header5 = fixture.headerOnChain(5);
        BOOST_REQUIRE(header3->stateRoot() != header5->stateRoot());
    }
};

BOOST_AUTO_TEST_SUITE(EthGetProofHistoricalSuite)

/// The whole contract in one case: the same account proves at the TIP and at an older height,
/// each proof verifying against ITS OWN committed header root and against no other — so a proof
/// silently built from today's nodes would be caught by the verifier, not just by a field value.
BOOST_AUTO_TEST_CASE(ProofAtPastBlockAndAtTipBothVerify)
{
    FullChainFixture fixture{"egp_historical"};
    EgpChain chain;
    chain.run(fixture);

    EgpNodeStorage nodes;
    egpLoadNodes(fixture, nodes);
    EgpEndpointHarness harness{fixture, nodes, /*proofDepth*/ 128};

    // (i) The tip: unchanged behaviour, the node rows answer directly.
    auto const tipProof = egpProofFromJson(harness.getProof(chain.account.hexPrefixed(), "latest"));
    BOOST_CHECK_EQUAL(tipProof.balance, u256(5000));
    BOOST_CHECK(mpt::verifyProof(chain.header5->stateRoot(), tipProof).accountValid);
    BOOST_CHECK(!mpt::verifyProof(chain.header3->stateRoot(), tipProof).accountValid);

    // (ii) Block 3, two blocks back: every position on the walk resolved to its block-3
    // version through the trie-node history.
    auto const pastProof = egpProofFromJson(harness.getProof(chain.account.hexPrefixed(), "0x3"));
    BOOST_CHECK_EQUAL(pastProof.balance, u256(3000));
    BOOST_CHECK_EQUAL(pastProof.nonce, u256(3));
    BOOST_CHECK_MESSAGE(mpt::verifyProof(chain.header3->stateRoot(), pastProof).accountValid,
        "the block-3 proof must verify against block 3's committed stateRoot");
    // The discriminating half: it must NOT verify against any other height's root, which is
    // what a proof accidentally assembled from current nodes would do.
    BOOST_CHECK(!mpt::verifyProof(chain.header5->stateRoot(), pastProof).accountValid);
    BOOST_CHECK(!mpt::verifyProof(chain.header4->stateRoot(), pastProof).accountValid);

    // (iii) Block 4 as well, so the case covers more than one historical distance.
    auto const proof4 = egpProofFromJson(harness.getProof(chain.account.hexPrefixed(), "0x4"));
    BOOST_CHECK_EQUAL(proof4.balance, u256(4000));
    BOOST_CHECK(mpt::verifyProof(chain.header4->stateRoot(), proof4).accountValid);
}

/// Out of window: the guard runs BEFORE the seek, so an expired height is refused rather than
/// answered from whatever the seek happens to find (spec B.3, G5). -32004 is the endpoint's
/// only out-of-window code, and it must not be reached by an in-window height.
BOOST_AUTO_TEST_CASE(ProofBeyondTheRetainedWindowIsMinus32004)
{
    FullChainFixture fixture{"egp_out_of_window"};
    EgpChain chain;
    chain.run(fixture);

    EgpNodeStorage nodes;
    egpLoadNodes(fixture, nodes);
    // depth 1 at tip 5 retains block 5 alone: block 4 is the first block outside.
    EgpEndpointHarness harness{fixture, nodes, /*proofDepth*/ 1};

    // Positive anchor: the tip is unaffected by the depth.
    BOOST_CHECK_NO_THROW(harness.getProof(chain.account.hexPrefixed(), "latest"));

    try
    {
        harness.getProof(chain.account.hexPrefixed(), "0x3");
        BOOST_FAIL("eth_getProof outside the retained window must throw");
    }
    catch (rpc::JsonRpcException const& e)
    {
        BOOST_CHECK_EQUAL(e.code(), -32004);
        BOOST_CHECK_MESSAGE(std::string(e.msg()).find("history window") != std::string::npos,
            "the message must name the window, got: " << e.msg());
    }

    // Widening the window brings the same height back — the refusal is the parameter's doing,
    // not a broken chain.
    EgpEndpointHarness wide{fixture, nodes, /*proofDepth*/ 128};
    auto const proof = egpProofFromJson(wide.getProof(chain.account.hexPrefixed(), "0x3"));
    BOOST_CHECK(mpt::verifyProof(chain.header3->stateRoot(), proof).accountValid);
}

/// A node that never recorded the era is a different failure from an expired one, and it must
/// not be silent: with no trie history at all every position would resolve to "unchanged since
/// B", i.e. today's trie proved against an old header's root.
BOOST_AUTO_TEST_CASE(ProofWithoutRecordedHistoryIsMinus32004)
{
    FullChainFixture fixture{"egp_no_history"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());
    fixture.enableFeatureFromBlock("feature_mpt_state_root", 2);
    // Depths left at the scheduler's default (0): the blocks below write no history.
    auto const account = FullChainFixture::makeAddress(0xAC);
    auto const filler = FullChainFixture::makeAddress(0xF1);
    fixture.planBlock(1, {FullChainFixture::balanceRow(account, "1000")});
    fixture.planBlock(2, {FullChainFixture::balanceRow(filler, "1")});
    fixture.planBlock(3, {FullChainFixture::balanceRow(account, "3000")});
    fixture.planBlock(4, {FullChainFixture::balanceRow(account, "4000")});
    for (protocol::BlockNumber number = 1; number <= 4; ++number)
    {
        fixture.runBlock(number);
    }

    EgpNodeStorage nodes;
    egpLoadNodes(fixture, nodes);
    EgpEndpointHarness harness{fixture, nodes, /*proofDepth*/ 128};

    BOOST_CHECK_NO_THROW(harness.getProof(account.hexPrefixed(), "latest"));
    try
    {
        harness.getProof(account.hexPrefixed(), "0x3");
        BOOST_FAIL("eth_getProof at an un-recorded height must throw, not prove today's trie");
    }
    catch (rpc::JsonRpcException const& e)
    {
        BOOST_CHECK_EQUAL(e.code(), -32004);
        BOOST_CHECK_MESSAGE(
            std::string(e.msg()).find("No trie-node history recorded") != std::string::npos,
            "the message must say the history was never recorded, got: " << e.msg());
    }
}

/// A cold slot's flat value must be read AT THE REQUESTED BLOCK, like the Merkle half beside it.
///
/// Under scenario A the storage tries are incomplete, so a slot written before MPT activation
/// has no leaf: generateProof marks it inMPT=false and the endpoint answers with the flat value
/// and an empty proof instead of a lying value-0 exclusion proof. That flat half used to come
/// from `ledger::getStorageAt(..., blockNumber)`, which takes the block number and DISCARDS it
/// (`Ledger.cpp`: `std::ignore = _blockNumber`) — so the response carried a Merkle half anchored
/// at B next to a flat half holding TODAY's value, with nothing to mark the difference.
///
/// The chain below makes the two differ: slot A is written in the XOR era (never entering the
/// trie), the account joins the trie at block 3, and block 4 overwrites the slot. At block 3 the
/// slot is cold and held V1; at the tip it holds V2.
BOOST_AUTO_TEST_CASE(ColdSlotFlatValueIsReadAtTheRequestedBlock)
{
    FullChainFixture fixture{"egp_cold_slot"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());
    fixture.enableFeatureFromBlock("feature_mpt_state_root", 2);
    fixture.m_baselineScheduler.setHistoryDepths({.state = 128, .proof = 128});

    auto const account = FullChainFixture::makeAddress(0xAC);
    auto const filler = FullChainFixture::makeAddress(0xF1);
    h256 const slot{1U};
    h256 const valueAtThree{42U};
    h256 const valueAtTip{0x63U};

    // XOR era: the slot is written before activation, so it never enters a storage trie.
    fixture.planBlock(1, {FullChainFixture::balanceRow(account, "1000"),
                             FullChainFixture::slotRow(account, slot, valueAtThree)});
    fixture.planBlock(2, {FullChainFixture::balanceRow(filler, "1")});  // activation, still XOR
    // First MPT block: the account joins the trie (balance touch), the cold slot does not.
    fixture.planBlock(3, {FullChainFixture::balanceRow(account, "3000")});
    // Block 4 overwrites the slot, recording V1 as its pre-image — and puts it IN the trie from
    // here on, which is why block 3 is the height where "cold" and "changed later" coincide.
    fixture.planBlock(4, {FullChainFixture::slotRow(account, slot, valueAtTip)});
    fixture.planBlock(5, {FullChainFixture::balanceRow(account, "5000")});
    for (protocol::BlockNumber number = 1; number <= 5; ++number)
    {
        fixture.runBlock(number);
    }

    EgpNodeStorage nodes;
    egpLoadNodes(fixture, nodes);
    EgpEndpointHarness harness{fixture, nodes, /*proofDepth*/ 128};

    auto const slotHex = slot.hexPrefixed();
    auto const atThree = harness.getProofWithSlots(account.hexPrefixed(), {slotHex}, "0x3");
    BOOST_REQUIRE_EQUAL(atThree["storageProof"].size(), 1U);
    auto const& coldEntry = atThree["storageProof"][0U];
    BOOST_REQUIRE_MESSAGE(!coldEntry["inMPT"].asBool(),
        "the pre-activation slot must be cold at block 3, or this case is not testing the "
        "flat-half path");
    BOOST_CHECK_MESSAGE(coldEntry["value"].asString() == toQuantity(42),
        "the cold slot's flat half must be block 3's value, got " << coldEntry["value"].asString());
    BOOST_CHECK_EQUAL(coldEntry["proof"].size(), 0U);

    // The tip is where the other value lives — so the block-3 answer above is not simply the
    // only value the chain ever held.
    auto const atTip = harness.getProofWithSlots(account.hexPrefixed(), {slotHex}, "latest");
    BOOST_REQUIRE_EQUAL(atTip["storageProof"].size(), 1U);
    BOOST_CHECK_EQUAL(atTip["storageProof"][0U]["value"].asString(), toQuantity(0x63));
}

/// The same path, refused rather than answered when the node cannot serve that height: half a
/// proof from the latest state is worse than none.
BOOST_AUTO_TEST_CASE(ColdSlotOutsideTheStateWindowIsRefused)
{
    FullChainFixture fixture{"egp_cold_slot_window"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());
    fixture.enableFeatureFromBlock("feature_mpt_state_root", 2);
    // The TRIE history is wide enough to serve the walk; the STATE history is not, so the
    // refusal below is specifically about the flat half.
    fixture.m_baselineScheduler.setHistoryDepths({.state = 1, .proof = 128});

    auto const account = FullChainFixture::makeAddress(0xAC);
    auto const filler = FullChainFixture::makeAddress(0xF1);
    h256 const slot{1U};
    fixture.planBlock(1, {FullChainFixture::balanceRow(account, "1000"),
                             FullChainFixture::slotRow(account, slot, h256{42U})});
    fixture.planBlock(2, {FullChainFixture::balanceRow(filler, "1")});
    fixture.planBlock(3, {FullChainFixture::balanceRow(account, "3000")});
    fixture.planBlock(4, {FullChainFixture::slotRow(account, slot, h256{0x63U})});
    fixture.planBlock(5, {FullChainFixture::balanceRow(account, "5000")});
    for (protocol::BlockNumber number = 1; number <= 5; ++number)
    {
        fixture.runBlock(number);
    }

    EgpNodeStorage nodes;
    egpLoadNodes(fixture, nodes);
    EgpEndpointHarness harness{fixture, nodes, /*proofDepth*/ 128};
    harness.m_nodeService->setMPTHistoryDepths({.state = 1, .proof = 128});

    try
    {
        harness.getProofWithSlots(account.hexPrefixed(), {slot.hexPrefixed()}, "0x3");
        BOOST_FAIL("a cold slot outside the state-history window must not be answered");
    }
    catch (rpc::JsonRpcException const& e)
    {
        BOOST_CHECK_EQUAL(e.code(), -32004);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
