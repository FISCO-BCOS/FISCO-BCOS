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
 * @file NavTest.cpp
 * @brief governanceFetch over scripted committee calls (count, newest N, wanted ids, stop),
 *        proposalTypeName, and the proposal detail key map (v / x / d open the confirmation
 *        layer of the vote / revoke preset, Esc closes it, no key → notice)
 * @file GovernanceTest.cpp
 */
#include "bcos-ops/tui/Governance.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/CommitteeCmd.h"
#include "bcos-ops/tui/SendForm.h"
#include "bcos-ops/tx/Abi.h"
#include "bcos-ops/tx/PresetCall.h"
#include "fixtures/FakeRpc.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <atomic>

namespace bcos::ops::test
{
namespace
{
std::string const c_govManager = "0x0000000000000000000000000000000000010001";
std::string const c_govCommittee = "0xc0ffee0000000000000000000000000000000001";
std::string const c_govProposalMgr = "0x5c2d3b4a1f0e9d8c7b6a59483726150493827161";
std::string const c_govGovernor = "0x3f12000000000000000000000000000000009a00";

std::string govOutput(std::string const& _types, std::string const& _jsonValues)
{
    auto data = Abi(false).encodeMethodBySignature("o(" + _types + ")", _jsonValues);
    return "0x" + toHex(bytes(data.begin() + 4, data.end()));
}

std::string govSelector(std::string const& _signature)
{
    return bcos::crypto::Keccak256().hash(bytesConstRef(_signature)).hex().substr(0, 8);
}

Json::Value govCallResult(std::string const& _output)
{
    Json::Value value;
    value["status"] = 0;
    value["output"] = _output;
    return value;
}

/// a committee with two governors and three proposals (status voting, type sysConfig)
FakeRpc govRpc()
{
    FakeRpc rpc;
    rpc.setCall(govSelector("_committee()"),
        govCallResult(govOutput("address", "[\"" + c_govCommittee + "\"]")));
    rpc.setCall(govSelector("_proposalMgr()"),
        govCallResult(govOutput("address", "[\"" + c_govProposalMgr + "\"]")));
    rpc.setCall(govSelector("getCommitteeInfo()"),
        govCallResult(govOutput("uint8,uint8,address[],uint32[]",
            "[100,100,[\"" + c_govGovernor + "\",\"" + c_govCommittee + "\"],[1,2]]")));
    rpc.setCall(govSelector("_proposalCount()"), govCallResult(govOutput("uint256", "[3]")));
    rpc.setCall(
        govSelector("getProposalStatus(uint256)"), govCallResult(govOutput("uint8", "[1]")));
    rpc.setCall(govSelector("getProposalInfo(uint256)"),
        govCallResult(govOutput("address,address,uint8,uint256,uint8,address[],address[]",
            "[\"" + c_govManager + "\",\"" + c_govGovernor + "\",41,1128,1,[\"" + c_govGovernor +
                "\"],[]]")));
    return rpc;
}

Connection govConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "attach";
    connection.group = "group0";
    return connection;
}

/// how many getProposalInfo calls the fake saw
size_t govInfoCalls(FakeRpc const& _rpc)
{
    size_t count = 0;
    for (auto const& [method, params] : _rpc.calls())
    {
        if (method == "call" &&
            params[3].asString().starts_with(govSelector("getProposalInfo(uint256)")))
        {
            ++count;
        }
    }
    return count;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(GovernanceTest)

BOOST_AUTO_TEST_CASE(typeNamesFollowCommitteeManager)
{
    BOOST_CHECK_EQUAL(proposalTypeName(11), "governor");
    BOOST_CHECK_EQUAL(proposalTypeName(41), "sysConfig");
    BOOST_CHECK_EQUAL(proposalTypeName(51), "consensusWeight");
    BOOST_CHECK_EQUAL(proposalTypeName(52), "rmNode");
    BOOST_CHECK_EQUAL(proposalTypeName(7), "type7");
}

BOOST_AUTO_TEST_CASE(fetchReadsTheCommitteeAndTheNewestRows)
{
    auto rpc = govRpc();
    auto connection = govConnection(rpc);
    CommitteeReader reader(connection);
    tui::GovernanceState state;
    tui::GovernanceWants wants{.rows = 2, .ids = {}};
    tui::governanceFetch(state, reader, wants, nullptr);
    BOOST_CHECK(state.error.empty());
    BOOST_CHECK(state.loaded);
    BOOST_CHECK_EQUAL(state.committee, c_govCommittee);
    BOOST_CHECK_EQUAL(state.proposalManager, c_govProposalMgr);
    BOOST_CHECK_EQUAL(state.participatesRate, "100");
    BOOST_CHECK_EQUAL(state.winRate, "100");
    BOOST_REQUIRE_EQUAL(state.governors.size(), 2U);
    BOOST_CHECK_EQUAL(state.governors[0].address, c_govGovernor);
    BOOST_CHECK_EQUAL(state.governors[1].weight, "2");
    BOOST_CHECK_EQUAL(state.proposalCount, 3);
    // the newest two of three, newest first
    BOOST_REQUIRE_EQUAL(state.proposals.size(), 2U);
    BOOST_CHECK_EQUAL(state.proposals[0].id, 3);
    BOOST_CHECK_EQUAL(state.proposals[1].id, 2);
    BOOST_CHECK_EQUAL(state.proposals[0].typeName, "sysConfig");
    BOOST_CHECK_EQUAL(state.proposals[0].statusName, "voting");
    BOOST_CHECK_EQUAL(state.proposals[0].proposer, c_govGovernor);
    BOOST_CHECK_EQUAL(state.proposals[0].agree, 1U);
    BOOST_CHECK_EQUAL(state.proposals[0].against, 0U);
    BOOST_CHECK_EQUAL(state.proposals[0].expiresAtBlock, "1128");
    BOOST_CHECK_EQUAL(govInfoCalls(rpc), 2U);
}

BOOST_AUTO_TEST_CASE(wantedIdsOutsideTheListAreFetchedAndListedOnesReused)
{
    auto rpc = govRpc();
    auto connection = govConnection(rpc);
    CommitteeReader reader(connection);
    tui::GovernanceState state;
    tui::GovernanceWants wants{.rows = 1, .ids = {3, 1, 9}};  // 3 listed, 1 fetched, 9 absent
    tui::governanceFetch(state, reader, wants, nullptr);
    BOOST_CHECK(state.error.empty());
    BOOST_REQUIRE_EQUAL(state.proposals.size(), 1U);
    BOOST_CHECK_EQUAL(state.details.count(3), 1U);
    BOOST_CHECK_EQUAL(state.details.count(1), 1U);
    BOOST_CHECK_EQUAL(state.details.count(9), 0U);
    BOOST_CHECK_EQUAL(govInfoCalls(rpc), 2U);  // id 3 from the list, id 1 fetched
}

// three RPCs per proposal per 2 s round: only rows that can still change are fetched again
BOOST_AUTO_TEST_CASE(settledRowsAreReusedVotingRowsRefetched)
{
    auto rpc = govRpc();
    auto connection = govConnection(rpc);
    CommitteeReader reader(connection);
    tui::GovernanceState state;
    tui::GovernanceWants wants{.rows = 3, .ids = {1}};
    tui::governanceFetch(state, reader, wants, nullptr);
    BOOST_CHECK_EQUAL(govInfoCalls(rpc), 3U);
    tui::governanceFetch(state, reader, wants, nullptr);  // all voting: fetched again
    BOOST_CHECK_EQUAL(govInfoCalls(rpc), 6U);
    rpc.setCall(
        govSelector("getProposalStatus(uint256)"), govCallResult(govOutput("uint8", "[3]")));
    tui::governanceFetch(state, reader, wants, nullptr);  // now denied
    BOOST_CHECK_EQUAL(govInfoCalls(rpc), 9U);
    BOOST_CHECK_EQUAL(state.proposals[0].statusName, "denied");
    tui::governanceFetch(state, reader, wants, nullptr);  // settled: nothing asked
    BOOST_CHECK_EQUAL(govInfoCalls(rpc), 9U);
    BOOST_REQUIRE_EQUAL(state.proposals.size(), 3U);
    BOOST_CHECK_EQUAL(state.proposals[2].statusName, "denied");
    BOOST_CHECK_EQUAL(state.details.count(1), 1U);
}

// a denied row whose getProposalInfo failed has no type or votes: it is fetched again, not kept;
// a settled detail row outside the list is reused like a listed one
BOOST_AUTO_TEST_CASE(incompleteSettledRowsAreRefetched)
{
    auto rpc = govRpc();
    rpc.setCall(
        govSelector("getProposalStatus(uint256)"), govCallResult(govOutput("uint8", "[3]")));
    Json::Value reverted;
    reverted["status"] = 16;
    reverted["output"] = "0x";
    rpc.setCall(govSelector("getProposalInfo(uint256)"), reverted);  // info fails
    auto connection = govConnection(rpc);
    CommitteeReader reader(connection);
    tui::GovernanceState state;
    tui::GovernanceWants wants{.rows = 1, .ids = {1}};  // id 3 listed, id 1 as a detail
    tui::governanceFetch(state, reader, wants, nullptr);
    BOOST_CHECK_EQUAL(govInfoCalls(rpc), 2U);
    tui::governanceFetch(state, reader, wants, nullptr);  // incomplete: asked again
    BOOST_CHECK_EQUAL(govInfoCalls(rpc), 4U);
    rpc.setCall(govSelector("getProposalInfo(uint256)"),
        govCallResult(govOutput("address,address,uint8,uint256,uint8,address[],address[]",
            "[\"" + c_govManager + "\",\"" + c_govGovernor + "\",41,1128,3,[],[\"" + c_govGovernor +
                "\"]]")));
    tui::governanceFetch(state, reader, wants, nullptr);  // complete now
    BOOST_CHECK_EQUAL(govInfoCalls(rpc), 6U);
    tui::governanceFetch(state, reader, wants, nullptr);  // settled list row and detail reused
    BOOST_CHECK_EQUAL(govInfoCalls(rpc), 6U);
    BOOST_CHECK_EQUAL(state.details.count(1), 1U);
    BOOST_CHECK_EQUAL(state.details.at(1).statusName, "denied");
    BOOST_CHECK_EQUAL(state.proposals.at(0).typeName, "sysConfig");
}

BOOST_AUTO_TEST_CASE(stopEndsTheFetchAndKeepsThePreviousRows)
{
    auto rpc = govRpc();
    auto connection = govConnection(rpc);
    CommitteeReader reader(connection);
    tui::GovernanceState state;
    tui::governanceFetch(state, reader, tui::GovernanceWants{.rows = 3, .ids = {}}, nullptr);
    BOOST_REQUIRE_EQUAL(state.proposals.size(), 3U);
    std::atomic<bool> stop{true};
    auto before = rpc.calls().size();
    tui::governanceFetch(state, reader, tui::GovernanceWants{.rows = 3, .ids = {}}, &stop);
    BOOST_CHECK_EQUAL(rpc.calls().size(), before);  // nothing asked once stopped
    BOOST_CHECK(!state.error.empty());
    BOOST_CHECK_EQUAL(state.proposals.size(), 3U);  // the rows stay
}

BOOST_AUTO_TEST_CASE(aFailingCommitteeCallBecomesTheError)
{
    auto rpc = govRpc();
    rpc.setCall(govSelector("_committee()"), govCallResult("0x"));  // not an auth chain shape
    auto connection = govConnection(rpc);
    CommitteeReader reader(connection);
    tui::GovernanceState state;
    tui::governanceFetch(state, reader, tui::GovernanceWants{}, nullptr);
    BOOST_CHECK(!state.loaded);
    BOOST_CHECK(!state.error.empty());
}

BOOST_AUTO_TEST_CASE(voteKeysOpenTheConfirmationLayerOfTheCommitteePreset)
{
    using ftxui::Event;
    auto form = tui::makeSendForm(false);
    tui::SendContext context;
    context.hasKey = false;
    // no key: a notice, no layer
    BOOST_CHECK(
        tui::govFormHandle(form, context, 3, Event::Character('v')) == tui::SendAction::redraw);
    BOOST_CHECK(!form.confirm);
    BOOST_CHECK(form.notice.find("load a key first") != std::string::npos);
    context.hasKey = true;
    BOOST_CHECK(
        tui::govFormHandle(form, context, 3, Event::Character('v')) == tui::SendAction::redraw);
    BOOST_REQUIRE(form.confirm);
    BOOST_REQUIRE(form.pending.preset != nullptr);
    BOOST_CHECK_EQUAL(std::string(form.pending.preset->signature), "voteProposal(uint256,bool)");
    BOOST_REQUIRE_EQUAL(form.pending.params.size(), 2U);
    BOOST_CHECK_EQUAL(form.pending.params[0], "3");
    BOOST_CHECK_EQUAL(form.pending.params[1], "true");
    BOOST_CHECK(!form.pending.openTxDetail);
    std::string rows;
    for (auto const& [label, value] : form.confirmRows)
    {
        rows += label + "=" + value + ";";
    }
    BOOST_CHECK(rows.find("mode=direct") != std::string::npos);
    BOOST_CHECK(rows.find("to=0x0000000000000000000000000000000000010001") != std::string::npos);
    BOOST_CHECK(rows.find("voteProposal(uint256,bool)") != std::string::npos);
    // Esc closes the layer without submitting
    BOOST_CHECK(tui::govFormHandle(form, context, 3, Event::Escape) == tui::SendAction::redraw);
    BOOST_CHECK(!form.confirm);
    // x votes against, d revokes; Enter in the layer submits
    tui::govFormHandle(form, context, 3, Event::Character('x'));
    BOOST_CHECK_EQUAL(form.pending.params[1], "false");
    tui::govFormHandle(form, context, 3, Event::Escape);
    tui::govFormHandle(form, context, 7, Event::Character('d'));
    BOOST_REQUIRE(form.confirm);
    BOOST_CHECK_EQUAL(std::string(form.pending.preset->signature), "revokeProposal(uint256)");
    BOOST_CHECK_EQUAL(form.pending.params[0], "7");
    BOOST_CHECK(tui::govFormHandle(form, context, 7, Event::Return) == tui::SendAction::submit);
    BOOST_CHECK(!form.confirm);
    // a proposal that is no longer voting: a notice, no layer
    context.proposalStatus = 2;
    BOOST_CHECK(
        tui::govFormHandle(form, context, 7, Event::Character('v')) == tui::SendAction::redraw);
    BOOST_CHECK(!form.confirm);
    BOOST_CHECK(form.notice.find("not voting") != std::string::npos);
    context.proposalStatus = 1;
    // other keys belong to the page map
    BOOST_CHECK(
        tui::govFormHandle(form, context, 7, Event::Character('q')) == tui::SendAction::toNav);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
