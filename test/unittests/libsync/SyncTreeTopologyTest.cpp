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
 * (c) 2016-2019 fisco-dev contributors.
 */

/**
 * @brief : SyncTreeTopologyTest
 * @author: yujiechen
 * @date: 2019-9-29
 */
#include <libsync/SyncTreeTopology.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev::sync;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(SyncTreeTopologyTest, TestOutputHelperFixture)

class FakeSyncTreeTopology : public SyncTreeTopology
{
public:
    using Ptr = std::shared_ptr<FakeSyncTreeTopology>;
    FakeSyncTreeTopology(dev::h512 const& _nodeId, unsigned const& _treeWidth = 2)
      : SyncTreeTopology(_nodeId, _treeWidth)
    {}

    virtual dev::h512s const& currentConsensusNodes() { return m_currentConsensusNodes; }

    virtual dev::h512s const& nodeList() { return m_nodeList; }
    void recursiveSelectChildNodes(std::shared_ptr<dev::h512s> selectedNodeList,
        ssize_t const& parentIndex, std::shared_ptr<std::set<dev::h512>> peers)
    {
        return SyncTreeTopology::recursiveSelectChildNodes(selectedNodeList, parentIndex, peers);
    }
    // select the parent nodes by tree
    void selectParentNodes(std::shared_ptr<dev::h512s> selectedNodeList,
        std::shared_ptr<std::set<dev::h512>> peers, int64_t const& nodeIndex)
    {
        return SyncTreeTopology::selectParentNodes(selectedNodeList, peers, nodeIndex);
    }

    virtual int64_t nodeNum() { return m_nodeNum; }

    virtual dev::h512 nodeId() { return m_nodeId; }

    virtual int64_t nodeIndex() const { return m_nodeIndex; }
    virtual int64_t consIndex() const { return m_consIndex; }
    virtual int64_t endIndex() const { return m_endIndex; }
    virtual int64_t startIndex() const { return m_startIndex; }
};

class SyncTreeToplogyFixture
{
public:
    using Ptr = std::shared_ptr<SyncTreeToplogyFixture>;
    SyncTreeToplogyFixture(unsigned const& _treeWidth = 2)
    {
        dev::h512 nodeId = KeyPair::create().pub();
        // create a 2-tree rounter
        m_syncTreeRouter = std::make_shared<FakeSyncTreeTopology>(nodeId, _treeWidth);
        m_peers = std::make_shared<std::set<dev::h512>>();
    }

    void addNodeIntoConsensusList(dev::h512 const& nodeId)
    {
        m_nodeList.push_back(nodeId);
        m_consensusList.push_back(nodeId);
        m_peers->insert(nodeId);
    }

    void addNodeIntoObserverList(dev::h512 const& nodeId)
    {
        m_nodeList.push_back(nodeId);
        m_peers->insert(nodeId);
    }

    // fake ConsensusList
    void fakeConsensusList(size_t size)
    {
        for (size_t i = 0; i < size; i++)
        {
            dev::h512 nodeId = KeyPair::create().pub();
            m_consensusList.push_back(nodeId);
            m_nodeList.push_back(nodeId);
            m_peers->insert(nodeId);
        }
    }

    // fake observerList
    void fakeObserverList(size_t size)
    {
        for (size_t i = 0; i < size; i++)
        {
            dev::h512 nodeId = KeyPair::create().pub();
            m_nodeList.push_back(nodeId);
            m_peers->insert(nodeId);
        }
    }

    // clear both nodeList and consensusList
    void clearNodeList()
    {
        m_consensusList.clear();
        m_nodeList.clear();
        m_peers->clear();
    }

    void updateNodeListInfo() { m_syncTreeRouter->updateNodeListInfo(m_nodeList); }

    void updateConsensusNodeInfo() { m_syncTreeRouter->updateConsensusNodeInfo(m_consensusList); }
    std::shared_ptr<FakeSyncTreeTopology> syncTreeRouter() { return m_syncTreeRouter; }
    dev::h512s const& consensusList() { return m_consensusList; }
    dev::h512s const& nodeList() { return m_nodeList; }
    std::shared_ptr<std::set<dev::h512>> peers() { return m_peers; }

private:
    std::shared_ptr<FakeSyncTreeTopology> m_syncTreeRouter = nullptr;
    dev::h512s m_consensusList;  // faked consensus list
    dev::h512s m_nodeList;       // faked node list
    std::shared_ptr<std::set<dev::h512>> m_peers;
};

void checkSelectedNodes(SyncTreeToplogyFixture::Ptr fakeSyncTreeTopology,
    dev::h512s const& selectedNodes, std::vector<size_t> idxVec)
{
    BOOST_CHECK(selectedNodes.size() == idxVec.size());
    size_t i = 0;
    for (auto const& node : selectedNodes)
    {
        BOOST_CHECK(node == fakeSyncTreeTopology->syncTreeRouter()->nodeList()[idxVec[i]]);
        i++;
    }
}

/// case1: the node doesn't locate in the node list (free node)
BOOST_AUTO_TEST_CASE(testFreeNode)
{
    SyncTreeToplogyFixture::Ptr fakeSyncTreeTopology = std::make_shared<SyncTreeToplogyFixture>();

    fakeSyncTreeTopology->fakeConsensusList(2);

    // updateConsensusNodeInfo and check consensusList
    fakeSyncTreeTopology->updateConsensusNodeInfo();
    BOOST_CHECK(fakeSyncTreeTopology->consensusList() ==
                fakeSyncTreeTopology->syncTreeRouter()->currentConsensusNodes());
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->consIndex() == -1);
    // check node list
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->nodeList() == dev::h512s());
    // check startIndex and endIndex
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->startIndex() == 0);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->endIndex() == 0);

    // updateNodeInfo and check NodeInfo
    fakeSyncTreeTopology->updateNodeListInfo();
    BOOST_CHECK(
        fakeSyncTreeTopology->nodeList() == fakeSyncTreeTopology->syncTreeRouter()->nodeList());
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->nodeNum() ==
                (int64_t)fakeSyncTreeTopology->nodeList().size());
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->nodeIndex() == -1);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->consIndex() == -1);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->startIndex() == 0);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->endIndex() == 0);

    // selectNodes: no need to send blocks to any nodes
    std::shared_ptr<dev::h512s> selectedNodeList = std::make_shared<dev::h512s>();
    fakeSyncTreeTopology->syncTreeRouter()->recursiveSelectChildNodes(
        selectedNodeList, -1, fakeSyncTreeTopology->peers());
    BOOST_CHECK(*selectedNodeList == dev::h512s());
}

/// case2: the node locates in the observerList
BOOST_AUTO_TEST_CASE(testForTheObserverNode)
{
    SyncTreeToplogyFixture::Ptr fakeSyncTreeTopology = std::make_shared<SyncTreeToplogyFixture>();

    fakeSyncTreeTopology->fakeConsensusList(2);
    fakeSyncTreeTopology->updateConsensusNodeInfo();
    fakeSyncTreeTopology->updateNodeListInfo();

    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->consIndex() == -1);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->nodeIndex() == -1);

    // push the node into the observerList
    /// case1: nodeIndex is 2, no need to broadcast to any node
    fakeSyncTreeTopology->addNodeIntoObserverList(fakeSyncTreeTopology->syncTreeRouter()->nodeId());
    fakeSyncTreeTopology->updateNodeListInfo();
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->consIndex() == -1);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->nodeIndex() == 2);
    // check NodeList
    BOOST_CHECK(
        fakeSyncTreeTopology->nodeList() == fakeSyncTreeTopology->syncTreeRouter()->nodeList());
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->nodeNum() ==
                (int64_t)fakeSyncTreeTopology->nodeList().size());

    // select target nodes from peers for this node
    std::shared_ptr<dev::h512s> selectedNodeList = std::make_shared<dev::h512s>();
    fakeSyncTreeTopology->syncTreeRouter()->recursiveSelectChildNodes(
        selectedNodeList, 2 + 1, fakeSyncTreeTopology->peers());
    BOOST_CHECK(*selectedNodeList == dev::h512s());

    // select parent for the 2th nodes
    selectedNodeList->clear();
    fakeSyncTreeTopology->syncTreeRouter()->selectParentNodes(
        selectedNodeList, fakeSyncTreeTopology->peers(), 2);
    BOOST_CHECK(*selectedNodeList == dev::h512s());

    /// case2: nodeIndex is 0
    fakeSyncTreeTopology->clearNodeList();
    fakeSyncTreeTopology->addNodeIntoObserverList(fakeSyncTreeTopology->syncTreeRouter()->nodeId());
    fakeSyncTreeTopology->fakeObserverList(10);
    fakeSyncTreeTopology->fakeConsensusList(2);
    fakeSyncTreeTopology->updateConsensusNodeInfo();
    fakeSyncTreeTopology->updateNodeListInfo();
    BOOST_CHECK(
        fakeSyncTreeTopology->syncTreeRouter()->nodeList() == fakeSyncTreeTopology->nodeList());
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->nodeNum() ==
                (int64_t)fakeSyncTreeTopology->nodeList().size());
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->nodeIndex() == 0);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->consIndex() == -1);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->startIndex() == 0);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->endIndex() == 6);

    // check and select nodes for the 0th node
    selectedNodeList->clear();
    fakeSyncTreeTopology->syncTreeRouter()->recursiveSelectChildNodes(
        selectedNodeList, 0 + 1, fakeSyncTreeTopology->peers());
    std::vector<size_t> idxVec = {2, 3};
    checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, idxVec);

    // check and select nodes for the 1th node
    selectedNodeList->clear();
    fakeSyncTreeTopology->syncTreeRouter()->recursiveSelectChildNodes(
        selectedNodeList, 1 + 1, fakeSyncTreeTopology->peers());
    idxVec = {4, 5};
    checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, idxVec);

    // check and select nodes for the 2th node
    selectedNodeList->clear();
    fakeSyncTreeTopology->syncTreeRouter()->recursiveSelectChildNodes(
        selectedNodeList, 2 + 1, fakeSyncTreeTopology->peers());
    idxVec = {6};
    checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, idxVec);

    // check other nodes that has no child
    for (size_t i = 3; i <= 6; i++)
    {
        selectedNodeList->clear();
        fakeSyncTreeTopology->syncTreeRouter()->recursiveSelectChildNodes(
            selectedNodeList, i + 1, fakeSyncTreeTopology->peers());
        checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, {});
    }
    /// check parent
    for (size_t i = 0; i <= 1; i++)
    {
        selectedNodeList->clear();
        fakeSyncTreeTopology->syncTreeRouter()->selectParentNodes(
            selectedNodeList, fakeSyncTreeTopology->peers(), i);
        BOOST_CHECK(*selectedNodeList == dev::h512s());
    }
    // check parent for 2th node
    for (size_t i = 2; i <= 3; i++)
    {
        selectedNodeList->clear();
        fakeSyncTreeTopology->syncTreeRouter()->selectParentNodes(
            selectedNodeList, fakeSyncTreeTopology->peers(), i);
        checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, {0});
    }

    for (size_t i = 4; i <= 5; i++)
    {
        selectedNodeList->clear();
        fakeSyncTreeTopology->syncTreeRouter()->selectParentNodes(
            selectedNodeList, fakeSyncTreeTopology->peers(), i);
        checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, {1});
    }
    selectedNodeList->clear();
    fakeSyncTreeTopology->syncTreeRouter()->selectParentNodes(
        selectedNodeList, fakeSyncTreeTopology->peers(), 6);
    checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, {2});


    /// case3: nodeIndex is 13
    fakeSyncTreeTopology->clearNodeList();
    fakeSyncTreeTopology->fakeObserverList(10);
    fakeSyncTreeTopology->fakeConsensusList(2);
    fakeSyncTreeTopology->addNodeIntoObserverList(fakeSyncTreeTopology->syncTreeRouter()->nodeId());
    fakeSyncTreeTopology->updateConsensusNodeInfo();
    fakeSyncTreeTopology->updateNodeListInfo();

    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->nodeIndex() == 12);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->consIndex() == -1);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->startIndex() == 7);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->endIndex() == 12);

    // check the 7th nodes
    selectedNodeList->clear();
    fakeSyncTreeTopology->syncTreeRouter()->recursiveSelectChildNodes(
        selectedNodeList, 7 + 1, fakeSyncTreeTopology->peers());
    idxVec = {9, 10};
    checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, idxVec);
    // check parent for 9th and 10th
    for (size_t i = 9; i <= 10; i++)
    {
        selectedNodeList->clear();
        fakeSyncTreeTopology->syncTreeRouter()->selectParentNodes(
            selectedNodeList, fakeSyncTreeTopology->peers(), i);
        checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, {7});
    }

    // check the 8th nodes
    selectedNodeList->clear();
    fakeSyncTreeTopology->syncTreeRouter()->recursiveSelectChildNodes(
        selectedNodeList, 8 + 1, fakeSyncTreeTopology->peers());
    idxVec = {11, 12};
    checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, idxVec);
    for (size_t i = 11; i <= 12; i++)
    {
        selectedNodeList->clear();
        fakeSyncTreeTopology->syncTreeRouter()->selectParentNodes(
            selectedNodeList, fakeSyncTreeTopology->peers(), i);
        checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, {8});
    }
}

/// case3: the node locates in the consensus nodeList
BOOST_AUTO_TEST_CASE(testForTheConsensusNode)
{
    // init SyncTreeToplogyFixture
    SyncTreeToplogyFixture::Ptr fakeSyncTreeTopology = std::make_shared<SyncTreeToplogyFixture>(2);
    fakeSyncTreeTopology->fakeObserverList(11);
    fakeSyncTreeTopology->fakeConsensusList(1);
    fakeSyncTreeTopology->addNodeIntoConsensusList(
        fakeSyncTreeTopology->syncTreeRouter()->nodeId());
    fakeSyncTreeTopology->updateConsensusNodeInfo();
    fakeSyncTreeTopology->updateNodeListInfo();
    BOOST_CHECK(
        fakeSyncTreeTopology->syncTreeRouter()->nodeList() == fakeSyncTreeTopology->nodeList());
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->nodeNum() ==
                (int64_t)fakeSyncTreeTopology->nodeList().size());
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->nodeIndex() == 12);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->consIndex() == 1);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->startIndex() == 7);
    BOOST_CHECK(fakeSyncTreeTopology->syncTreeRouter()->endIndex() == 12);

    // get childNodes and parentNodes for this node
    std::shared_ptr<dev::h512s> selectedNodeList = std::make_shared<dev::h512s>();
    fakeSyncTreeTopology->syncTreeRouter()->recursiveSelectChildNodes(selectedNodeList,
        fakeSyncTreeTopology->syncTreeRouter()->startIndex(), fakeSyncTreeTopology->peers());
    checkSelectedNodes(fakeSyncTreeTopology, *selectedNodeList, {7, 8});

    // check parent
    selectedNodeList->clear();
    fakeSyncTreeTopology->syncTreeRouter()->selectParentNodes(selectedNodeList,
        fakeSyncTreeTopology->peers(), fakeSyncTreeTopology->syncTreeRouter()->nodeIndex());
    BOOST_CHECK(
        *selectedNodeList == fakeSyncTreeTopology->syncTreeRouter()->currentConsensusNodes());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev