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
 * @brief Equivalence test for the FIB-186 (vector D) reverse-index optimisation of
 *        PeersRouterTable::removeP2PIDFromGroupNodeList. The removal was changed from an
 *        O(groups x nodes x p2pIDs) scan of the whole forward map to an O(K) targeted removal
 * driven by a reverse index (p2pID -> its (groupID, nodeID) entries), to shorten the
 * x_groupNodeList WriteLock hold time so a bulk-disconnect flood on the teardown thread cannot
 * stall queryP2pIDs on the routing hot path. This test pins that the targeted removal produces the
 *        same observable routing table as the old full scan: the removed peer disappears from every
 *        (group, node) it held, other peers are untouched, emptied node/group entries are pruned,
 *        the reverse index is cleared so a re-insert + remove stays correct, and removing an
 * unknown peer is a no-op.
 * @file PeersRouterTableReverseIndexTest.cpp
 * @date 2026-07-16
 */

#include "bcos-framework/protocol/ProtocolInfo.h"
#include "bcos-gateway/gateway/PeersRouterTable.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <set>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(PeersRouterTableReverseIndexTest, TestPromptFixture)

namespace
{
// Promote the two protected mutators so the test can drive the forward map and its reverse index in
// isolation, without the peer-status / gateway-info bookkeeping of the public updatePeerStatus.
class ExposedPeersRouterTable : public PeersRouterTable
{
public:
    ExposedPeersRouterTable() : PeersRouterTable("", nullptr, nullptr) {}
    using PeersRouterTable::batchInsertNodeList;
    using PeersRouterTable::removeP2PIDFromGroupNodeList;
};

GroupNodeInfo::Ptr makeGroupNodeInfo(std::string const& _group, std::vector<std::string> _nodes)
{
    auto info = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>();
    info->setGroupID(_group);
    // batchInsertNodeList reads protocol(i) for each node index, so append one protocol per node.
    for (std::size_t i = 0; i < _nodes.size(); ++i)
    {
        info->appendProtocol(std::make_shared<ProtocolInfo>(
            ProtocolModuleID::NodeService, ProtocolVersion::V1, ProtocolVersion::V1));
    }
    info->setNodeIDList(std::move(_nodes));
    return info;
}

std::set<P2pID> S(std::initializer_list<std::string> _l)
{
    return std::set<P2pID>(_l.begin(), _l.end());
}
}  // namespace

// The removed peer disappears from every (group, node) it held; co-located peers are untouched; a
// (group, node) whose only holder was the removed peer is pruned to empty.
BOOST_AUTO_TEST_CASE(TargetedRemovalMatchesFullScan)
{
    ExposedPeersRouterTable table;

    // p2pA holds group1{n1,n2}, group2{n3}
    table.batchInsertNodeList(
        "A", {makeGroupNodeInfo("group1", {"n1", "n2"}), makeGroupNodeInfo("group2", {"n3"})});
    // p2pB holds group1{n1}, group2{n3,n4}
    table.batchInsertNodeList(
        "B", {makeGroupNodeInfo("group1", {"n1"}), makeGroupNodeInfo("group2", {"n3", "n4"})});
    // p2pC holds group1{n2}
    table.batchInsertNodeList("C", {makeGroupNodeInfo("group1", {"n2"})});
    // p2pD is the sole holder of group3{n9} -- removing D must prune the node and the whole group.
    table.batchInsertNodeList("D", {makeGroupNodeInfo("group3", {"n9"})});

    // Pre-removal routing view.
    BOOST_CHECK(table.queryP2pIDs("group1", "n1") == S({"A", "B"}));
    BOOST_CHECK(table.queryP2pIDs("group1", "n2") == S({"A", "C"}));
    BOOST_CHECK(table.queryP2pIDs("group2", "n3") == S({"A", "B"}));
    BOOST_CHECK(table.queryP2pIDs("group2", "n4") == S({"B"}));
    BOOST_CHECK(table.queryP2pIDsByGroupID("group3") == S({"D"}));

    table.removeP2PIDFromGroupNodeList("A");

    // A is gone from every entry it held; B and C are untouched; no entry A did not hold changed.
    BOOST_CHECK(table.queryP2pIDs("group1", "n1") == S({"B"}));
    BOOST_CHECK(table.queryP2pIDs("group1", "n2") == S({"C"}));
    BOOST_CHECK(table.queryP2pIDs("group2", "n3") == S({"B"}));
    BOOST_CHECK(table.queryP2pIDs("group2", "n4") == S({"B"}));

    table.removeP2PIDFromGroupNodeList("D");

    // D was the sole holder of group3{n9}: the node set empties and the group is pruned away.
    BOOST_CHECK(table.queryP2pIDs("group3", "n9").empty());
    BOOST_CHECK(table.queryP2pIDsByGroupID("group3").empty());
}

// The reverse index is cleared when a peer is removed, so a later re-insert (the remove-then-insert
// shape of updatePeerStatus) and a second removal only affect the peer's new entries.
BOOST_AUTO_TEST_CASE(ReverseIndexClearedOnRemovalAndIdempotent)
{
    ExposedPeersRouterTable table;

    table.batchInsertNodeList("A", {makeGroupNodeInfo("group1", {"n1"})});
    table.batchInsertNodeList("B", {makeGroupNodeInfo("group1", {"n1"})});
    BOOST_CHECK(table.queryP2pIDs("group1", "n1") == S({"A", "B"}));

    // Remove A, then re-insert it under a fresh (group1, n5) only.
    table.removeP2PIDFromGroupNodeList("A");
    BOOST_CHECK(table.queryP2pIDs("group1", "n1") == S({"B"}));
    table.batchInsertNodeList("A", {makeGroupNodeInfo("group1", {"n5"})});
    BOOST_CHECK(table.queryP2pIDs("group1", "n5") == S({"A"}));

    // Second removal must clear only the new entry (proves the first removal cleared A's old
    // reverse entries -- a stale (group1, n1) reverse entry would have wrongly evicted B here).
    table.removeP2PIDFromGroupNodeList("A");
    BOOST_CHECK(table.queryP2pIDs("group1", "n5").empty());
    BOOST_CHECK(table.queryP2pIDs("group1", "n1") == S({"B"}));

    // Removing a never-seen peer, and removing A a third time, are no-ops (no crash, B intact).
    table.removeP2PIDFromGroupNodeList("A");
    table.removeP2PIDFromGroupNodeList("unknown-p2p");
    BOOST_CHECK(table.queryP2pIDs("group1", "n1") == S({"B"}));
}

// The reverse index aliases m_groupNodeList's key strings by pointer. A group/node fully erased and
// later recreated must not corrupt routing: the recreated node owns fresh key objects, and no stale
// reverse entry may reference the freed ones. (Without ASan this is a probabilistic stress of the
// lifetime invariant, not a proof; the invariant itself is argued on m_p2pID2GroupNodes.)
BOOST_AUTO_TEST_CASE(GroupErasedAndRecreatedKeepsRoutingCorrect)
{
    ExposedPeersRouterTable table;

    // P is the sole holder of group9{n1,n2}; removing P erases both nodes and the whole group.
    table.batchInsertNodeList("P", {makeGroupNodeInfo("group9", {"n1", "n2"})});
    BOOST_CHECK(table.queryP2pIDs("group9", "n1") == S({"P"}));
    table.removeP2PIDFromGroupNodeList("P");
    BOOST_CHECK(table.queryP2pIDsByGroupID("group9").empty());

    // Churn many unrelated groups so the allocator is likely to reuse the memory the erased group9
    // key strings occupied.
    for (int i = 0; i < 64; ++i)
    {
        table.batchInsertNodeList(
            "X", {makeGroupNodeInfo("churn" + std::to_string(i), {"a", "b"})});
    }

    // Recreate group9 through a different peer Q; its keys are fresh objects at (possibly) reused
    // addresses.
    table.batchInsertNodeList("Q", {makeGroupNodeInfo("group9", {"n1"})});
    BOOST_CHECK(table.queryP2pIDs("group9", "n1") == S({"Q"}));

    // Removing X keys off X's own churn entries only; Q's recreated group9 entry is untouched.
    table.removeP2PIDFromGroupNodeList("X");
    BOOST_CHECK(table.queryP2pIDs("churn0", "a").empty());
    BOOST_CHECK(table.queryP2pIDs("group9", "n1") == S({"Q"}));
    table.removeP2PIDFromGroupNodeList("Q");
    BOOST_CHECK(table.queryP2pIDsByGroupID("group9").empty());
}

BOOST_AUTO_TEST_SUITE_END()
