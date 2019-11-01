/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : sync status test
 * @author: jimmyshi
 * @date: 2018-10-23
 */

#include <libsync/Common.h>
#include <libsync/SyncMaster.h>
#include <libsync/SyncStatus.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <functional>
#include <set>
#include <string>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;

namespace dev
{
namespace test
{
class SyncStatusFixture : public TestOutputHelperFixture
{
public:
    SyncStatusFixture() : status(h256(111)) {}
    SyncMasterStatus status;
};

BOOST_FIXTURE_TEST_SUITE(SyncStatusTest, SyncStatusFixture)

BOOST_AUTO_TEST_CASE(PeerNewHasDeleteTest)
{
    for (unsigned id = 0; id < 10; id++)
    {
        SyncPeerInfo node{NodeID(id), id, h256(id), h256(id)};
        BOOST_CHECK(!status.hasPeer(node.nodeId));
        BOOST_CHECK(status.newSyncPeerStatus(node));
        BOOST_CHECK(!status.newSyncPeerStatus(node));
        BOOST_CHECK(status.hasPeer(node.nodeId));
    }

    NodeIDs peers = *(status.peers());
    BOOST_CHECK_EQUAL(peers.size(), 10);

    for (auto peer : peers)
    {
        status.deletePeer(peer);
        BOOST_CHECK(!status.hasPeer(peer));
    }
}

BOOST_AUTO_TEST_CASE(PeerStatusTest)
{
    unsigned id = 999;
    SyncPeerInfo node{NodeID(id), id, h256(id), h256(id)};
    status.newSyncPeerStatus(node);
    shared_ptr<SyncPeerStatus> peerStatus = status.peerStatus(node.nodeId);

    BOOST_CHECK_EQUAL(peerStatus->nodeId, node.nodeId);
    BOOST_CHECK_EQUAL(peerStatus->number, node.number);
    BOOST_CHECK_EQUAL(peerStatus->genesisHash, node.genesisHash);
    BOOST_CHECK_EQUAL(peerStatus->latestHash, node.latestHash);
}

BOOST_AUTO_TEST_CASE(ForeachPeerTest)
{
    for (unsigned id = 0; id < 10; id++)
    {
        SyncPeerInfo node{NodeID(id), id, h256(id), h256(id)};
        status.newSyncPeerStatus(node);
    }

    int peerCnt = 0;
    shared_ptr<SyncPeerStatus> peerStatus;
    cout << "foreachPeer: " << endl;
    status.foreachPeer([&](shared_ptr<SyncPeerStatus> _p) {
        peerCnt++;
        cout << _p->nodeId << endl;
        peerStatus = status.peerStatus(_p->nodeId);
        BOOST_CHECK_EQUAL(peerStatus->nodeId, _p->nodeId);
        BOOST_CHECK_EQUAL(peerStatus->number, _p->number);
        BOOST_CHECK_EQUAL(peerStatus->genesisHash, _p->genesisHash);
        BOOST_CHECK_EQUAL(peerStatus->latestHash, _p->latestHash);
        return true;
    });
    BOOST_CHECK_EQUAL(peerCnt, 10);
}

BOOST_AUTO_TEST_CASE(ForeachPeerRandomTest)
{
    for (unsigned id = 0; id < 10; id++)
    {
        SyncPeerInfo node{NodeID(id), id, h256(id), h256(id)};
        status.newSyncPeerStatus(node);
    }

    set<NodeID> peers;
    shared_ptr<SyncPeerStatus> peerStatus;
    status.foreachPeerRandom([&](shared_ptr<SyncPeerStatus> _p) {
        peers.insert(_p->nodeId);
        cout << _p->nodeId << endl;
        peerStatus = status.peerStatus(_p->nodeId);
        BOOST_CHECK_EQUAL(peerStatus->nodeId, _p->nodeId);
        BOOST_CHECK_EQUAL(peerStatus->number, _p->number);
        BOOST_CHECK_EQUAL(peerStatus->genesisHash, _p->genesisHash);
        BOOST_CHECK_EQUAL(peerStatus->latestHash, _p->latestHash);
        return true;
    });
    BOOST_CHECK_EQUAL(peers.size(), 10);
}

BOOST_AUTO_TEST_CASE(RandomSelectionTest)
{
    for (unsigned id = 0; id < 10; id++)
    {
        SyncPeerInfo node{NodeID(id), id, h256(id), h256(id)};
        status.newSyncPeerStatus(node);
    }

    NodeIDs peers;
    shared_ptr<SyncPeerStatus> peerStatus;
    peers = status.randomSelection(70, [&](shared_ptr<SyncPeerStatus> _p) {
        peerStatus = status.peerStatus(_p->nodeId);
        BOOST_CHECK_EQUAL(peerStatus->nodeId, _p->nodeId);
        BOOST_CHECK_EQUAL(peerStatus->number, _p->number);
        BOOST_CHECK_EQUAL(peerStatus->genesisHash, _p->genesisHash);
        BOOST_CHECK_EQUAL(peerStatus->latestHash, _p->latestHash);
        return true;
    });
    BOOST_CHECK_EQUAL(peers.size(), 7);

    set<NodeID> peersSet;
    for (auto peer : peers)
        peersSet.insert(peer);
    BOOST_CHECK_EQUAL(peersSet.size(), 7);
}


BOOST_AUTO_TEST_CASE(RandomSelectionSizeTest)
{
    for (unsigned id = 0; id < 10; id++)
    {
        SyncPeerInfo node{NodeID(id), id, h256(id), h256(id)};
        status.newSyncPeerStatus(node);
    }

    NodeIDs peers;
    shared_ptr<SyncPeerStatus> peerStatus;
    peers = status.randomSelectionSize(3, [&](shared_ptr<SyncPeerStatus> _p) {
        peerStatus = status.peerStatus(_p->nodeId);
        BOOST_CHECK_EQUAL(peerStatus->nodeId, _p->nodeId);
        BOOST_CHECK_EQUAL(peerStatus->number, _p->number);
        BOOST_CHECK_EQUAL(peerStatus->genesisHash, _p->genesisHash);
        BOOST_CHECK_EQUAL(peerStatus->latestHash, _p->latestHash);
        return true;
    });
    BOOST_CHECK_EQUAL(peers.size(), 3);

    set<NodeID> peersSet;
    for (auto peer : peers)
        peersSet.insert(peer);
    BOOST_CHECK_EQUAL(peersSet.size(), 3);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev