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
 * (c) 2016-2020 fisco-dev contributors.
 */

/**
 * @brief : RotatingPBFTEngineTest
 * @author: yujiechen
 * @date: 2020-01-21
 */

#include "RotatingPBFTEngineTest.h"

using namespace dev::test;

std::set<dev::h512> getExpectedSelectedNodes(
    RotatingPBFTEngineFixture::Ptr _fixture, int64_t const& _round, int64_t const& _epochSize)
{
    std::set<dev::h512> expectedSelectedNodes;
    auto currentSealerList = _fixture->rotatingPBFT()->sealerList();
    for (auto i = _round; i < (_round + _epochSize); i++)
    {
        expectedSelectedNodes.insert(currentSealerList[i % currentSealerList.size()]);
    }
    return expectedSelectedNodes;
}

void checkResetConfig(RotatingPBFTEngineFixture::Ptr _fixture, int64_t const& _round,
    int64_t const& _rotatingInterval, int64_t const& _epochSize, int64_t const& _sealersNum)
{
    int64_t startBlockNumber = _round * _rotatingInterval;
    auto expectedSelectedNodes = getExpectedSelectedNodes(_fixture, _round, _epochSize);
    // block number range: [startBlockNumber, startBlockNumber + _rotatingInterval - 1](the
    // {_round}th rotating round)
    int64_t epochSize = _epochSize;
    if (_epochSize > _sealersNum)
    {
        epochSize = _epochSize;
    }
    for (auto i = startBlockNumber; i < (startBlockNumber + _rotatingInterval); i++)
    {
        // update the blockNumber
        _fixture->rotatingPBFT()->fakedBlockChain()->setBlockNumber(i + 1);
        _fixture->rotatingPBFT()->resetConfigWrapper();
        BOOST_CHECK(_fixture->rotatingPBFT()->sealersNum() == _sealersNum);
        BOOST_CHECK(_fixture->rotatingPBFT()->startNodeIdx() == _round % _sealersNum);
        BOOST_CHECK(_fixture->rotatingPBFT()->rotatingRound() == _round);
        auto selectedConsensusNodes = _fixture->rotatingPBFT()->chosedConsensusNodes();
        BOOST_CHECK(selectedConsensusNodes.size() == (size_t)epochSize);
        BOOST_CHECK(selectedConsensusNodes == expectedSelectedNodes);
        _fixture->rotatingPBFT()->setSealerListUpdated(false);
    }
}

// UT for RotatingPBFTEngine
BOOST_FIXTURE_TEST_SUITE(RotatingPBFTEngineTest, TestOutputHelperFixture)

// case1: sealers are not updated
BOOST_AUTO_TEST_CASE(testConstantSealers)
{
    // case1: eight sealers with two groups(every group with four members), rotating interval is 10
    auto fixture = std::make_shared<RotatingPBFTEngineFixture>(8);
    fixture->rotatingPBFT()->setSealersNum(8);
    fixture->rotatingPBFT()->resetConfigWrapper();
    int64_t rotatingInterval = 10;
    for (auto epochSize = 4; epochSize < 10; epochSize++)
    {
        // the node set up: update m_sealerListUpdated
        fixture->rotatingPBFT()->clearStartIdx();
        fixture->rotatingPBFT()->setSealerListUpdated(true);

        fixture->rotatingPBFT()->setEpochSealerNum(epochSize);
        fixture->rotatingPBFT()->setEpochBlockNum(rotatingInterval);
        BOOST_CHECK(fixture->rotatingPBFT()->rotatingInterval() == rotatingInterval);
        auto tmpEpochSize = epochSize;
        if (epochSize <= 8)
        {
            BOOST_CHECK(fixture->rotatingPBFT()->epochSize() == epochSize);
        }
        else
        {
            tmpEpochSize = 8;
            BOOST_CHECK(fixture->rotatingPBFT()->epochSize() == 8);
        }

        auto currentSealerList = fixture->rotatingPBFT()->sealerList();
        auto sealersNum = currentSealerList.size();
        // check [10, 29] round
        for (auto round = 10; round < 30; round++)
        {
            checkResetConfig(fixture, round, rotatingInterval, tmpEpochSize, sealersNum);
        }
        // check falut-tolerance
        BOOST_CHECK(fixture->rotatingPBFT()->f() == (tmpEpochSize - 1) / 3);
        // check minValidNodes
        BOOST_CHECK(fixture->rotatingPBFT()->minValidNodes() ==
                    (tmpEpochSize - fixture->rotatingPBFT()->f()));
    }
}

// test for broadcasting rawPrepare by tree
BOOST_AUTO_TEST_CASE(testRawPrepareTreeBroadcast)
{
    // create RotatingPBFTEngineFixture for the leader
    auto leaderRPBFT = std::make_shared<RotatingPBFTEngineFixture>(20);
    auto followRPBFT = std::make_shared<RotatingPBFTEngineFixture>(20);

    leaderRPBFT->fakePBFTSuite()->consensus()->setLocatedInConsensusNodes(true);
    followRPBFT->fakePBFTSuite()->consensus()->setLocatedInConsensusNodes(true);
    // create RotatingPBFTEngineFixture for the follow
    std::shared_ptr<BlockFactory> blockFactory = std::make_shared<BlockFactory>();
    std::shared_ptr<PrepareReq> prepareReq = std::make_shared<PrepareReq>();

    // fake prepare message
    fakePrepareMsg(leaderRPBFT->fakePBFTSuite(), followRPBFT->fakePBFTSuite(), prepareReq,
        blockFactory, false);

    std::shared_ptr<FakeService> leaderService = std::dynamic_pointer_cast<FakeService>(
        leaderRPBFT->fakePBFTSuite()->consensus()->mutableService());
    /// case 1: tree-topology has been disabled, broadcast rawprepare request to all the sealers
    // the leader construct and broadcast the prepareReq
    leaderRPBFT->fakePBFTSuite()->consensus()->wrapperConstructPrepareReq(prepareReq->pBlock);
    leaderRPBFT->fakePBFTSuite()->consensus()->reqCache()->addRawPrepare(prepareReq);
    checkPrepareReqEqual(
        leaderRPBFT->fakePBFTSuite()->consensus()->reqCache()->rawPrepareCachePtr(), prepareReq);

    std::shared_ptr<P2PMessage> receivedP2pMsg = nullptr;
    // wait broadcast enqueue finished
    while (!receivedP2pMsg)
    {
        receivedP2pMsg = leaderService->getAsyncSendMessageByNodeID(
            followRPBFT->fakePBFTSuite()->consensus()->keyPair().pub());
        sleep(1);
    }
    for (auto const& node : leaderRPBFT->fakePBFTSuite()->consensus()->sealerList())
    {
        compareAndClearAsyncSendTime(*(leaderRPBFT->fakePBFTSuite()), node, 1);
    }

    // the follower receive rawPrepare message from the leader, no forward
    // fake session for the leader
    std::shared_ptr<FakeSession> leaderSession =
        std::make_shared<FakeSession>(leaderRPBFT->rotatingPBFT()->keyPair().pub());

    NetworkException networkException;
    followRPBFT->fakePBFTSuite()->consensus()->wrapperOnRecvPBFTMessage(
        networkException, leaderSession, receivedP2pMsg);
    for (auto const& node : followRPBFT->fakePBFTSuite()->consensus()->sealerList())
    {
        compareAndClearAsyncSendTime(*(followRPBFT->fakePBFTSuite()), node, 0);
    }

    followRPBFT->fakePBFTSuite()->consensus()->reqCache()->clearAll();

    leaderRPBFT->fakePBFTSuite()->consensus()->clearAllMsgCache();
    followRPBFT->fakePBFTSuite()->consensus()->clearAllMsgCache();
    // enable tree-topology, only broadcast rawprepare request to the children
    leaderRPBFT->fakePBFTSuite()->consensus()->createTreeTopology(3);
    followRPBFT->fakePBFTSuite()->consensus()->createTreeTopology(3);
    auto sealerList = leaderRPBFT->fakePBFTSuite()->consensus()->sealerList();
    // the leader only broadcast rawprepare request to the selected child nodes
    std::shared_ptr<std::set<dev::h512>> sealerSet =
        std::make_shared<std::set<dev::h512>>(sealerList.begin(), sealerList.end());
    leaderRPBFT->fakePBFTSuite()->consensus()->setChosedConsensusNodes(sealerSet);
    followRPBFT->fakePBFTSuite()->consensus()->setChosedConsensusNodes(sealerSet);

    leaderRPBFT->fakePBFTSuite()->consensus()->treeRouter()->updateConsensusNodeInfo(sealerList);
    followRPBFT->fakePBFTSuite()->consensus()->treeRouter()->updateConsensusNodeInfo(sealerList);

    leaderRPBFT->fakePBFTSuite()->consensus()->wrapperConstructPrepareReq(prepareReq->pBlock);
    auto selectedNodes = leaderRPBFT->fakePBFTSuite()->consensus()->treeRouter()->selectNodes(
        sealerSet, leaderRPBFT->fakePBFTSuite()->consensus()->nodeIdx());
    BOOST_CHECK(selectedNodes->size() == 3);
    receivedP2pMsg = nullptr;
    while (!receivedP2pMsg)
    {
        receivedP2pMsg = leaderService->getAsyncSendMessageByNodeID((*selectedNodes)[0]);
        sleep(1);
    }
    for (auto const& node : *selectedNodes)
    {
        compareAndClearAsyncSendTime(*(leaderRPBFT->fakePBFTSuite()), node, 1);
    }

    // check the follow forward and handle the message
    followRPBFT->fakePBFTSuite()->consensus()->wrapperOnRecvPBFTMessage(
        networkException, leaderSession, receivedP2pMsg);

    selectedNodes = followRPBFT->fakePBFTSuite()->consensus()->treeRouter()->selectNodes(
        sealerSet, prepareReq->idx);
    // should forward the message
    std::shared_ptr<FakeService> followService = std::dynamic_pointer_cast<FakeService>(
        followRPBFT->fakePBFTSuite()->consensus()->mutableService());
    if (selectedNodes->size() == 0)
    {
        for (auto const& node : sealerList)
        {
            compareAndClearAsyncSendTime(*(followRPBFT->fakePBFTSuite()), node, 0);
        }
    }
    else
    {
        receivedP2pMsg = nullptr;
        while (!receivedP2pMsg)
        {
            receivedP2pMsg = followService->getAsyncSendMessageByNodeID((*selectedNodes)[0]);
            sleep(1);
        }

        for (auto const& node : *selectedNodes)
        {
            compareAndClearAsyncSendTime(*(followRPBFT->fakePBFTSuite()), node, 1);
        }
    }

    followRPBFT->fakePBFTSuite()->consensus()->reqCache()->clearAll();
    // the leader sendRawPrepareStatusRandomly
    leaderRPBFT->fakePBFTSuite()->consensus()->setNodeIdx(0);
    leaderRPBFT->fakePBFTSuite()->consensus()->setKeyPair(
        leaderRPBFT->fakePBFTSuite()->m_keyPair[0]);

    leaderRPBFT->fakePBFTSuite()->consensus()->wrapperSendRawPrepareStatusRandomly(prepareReq);
    auto chosedConsensusNodeSize =
        leaderRPBFT->fakePBFTSuite()->consensus()->chosedConsensusNodes().size();
    size_t selectedSize =
        (leaderRPBFT->fakePBFTSuite()->consensus()->prepareStatusBroadcastPercent() *
                chosedConsensusNodeSize +
            99) /
        100;
    size_t sendedSize = 0;
    std::vector<std::pair<dev::h512, IDXTYPE>> chosedNodes;
    ssize_t index = 0;
    for (auto const& node : sealerList)
    {
        if (leaderService->getAsyncSendSizeByNodeID(node) == 1)
        {
            chosedNodes.push_back(std::make_pair(node, index));
            receivedP2pMsg = leaderService->getAsyncSendMessageByNodeID(node);
            compareAndClearAsyncSendTime(*(leaderRPBFT->fakePBFTSuite()), node, 1);
            sendedSize++;
        }
        index++;
    }
    BOOST_CHECK(sendedSize == selectedSize);

    // the chosed node receive rawPrepareStatus and request the rawPrepareReq
    // make sure the followRPBFT is not the child of the leaderRPBFT
    do
    {
        std::srand(utcTime());
        index = std::rand() % chosedNodes.size();
    } while (index >= 0 && index <= 3);

    followRPBFT->fakePBFTSuite()->consensus()->setKeyPair(
        followRPBFT->fakePBFTSuite()->m_keyPair[chosedNodes[index].second]);
    followRPBFT->fakePBFTSuite()->consensus()->setNodeIdx(chosedNodes[index].second);

    std::shared_ptr<FakeSession> leaderSession2 =
        std::make_shared<FakeSession>(leaderRPBFT->fakePBFTSuite()->consensus()->keyPair().pub());

    // onReceiveRawPrepareStatus
    followRPBFT->fakePBFTSuite()->consensus()->rpbftReqCache()->requestedPrepareQueue()->clear();
    leaderRPBFT->fakePBFTSuite()->consensus()->rpbftReqCache()->requestedPrepareQueue()->clear();
    // case1: connect with all the node, but the parent node doesn't send rawPrepare to the node,
    // request again after 100ms
    followRPBFT->fakePBFTSuite()->consensus()->wrapperOnReceiveRawPrepareStatus(
        leaderSession2, receivedP2pMsg);
    // wait for request
    std::shared_ptr<P2PMessage> receivedP2pMsg2 = nullptr;
    while (!receivedP2pMsg2)
    {
        receivedP2pMsg2 = followService->getAsyncSendMessageByNodeID(
            leaderRPBFT->fakePBFTSuite()->consensus()->keyPair().pub());
        sleep(1);
    }
    compareAndClearAsyncSendTime(*(followRPBFT->fakePBFTSuite()),
        leaderRPBFT->fakePBFTSuite()->consensus()->keyPair().pub(), 1);

    // case2: only connect with the leaderRPBFT
    followRPBFT->fakePBFTSuite()->consensus()->rpbftReqCache()->requestedPrepareQueue()->clear();
    leaderRPBFT->fakePBFTSuite()->consensus()->rpbftReqCache()->requestedPrepareQueue()->clear();
    followService->clearSessionInfo();
    appendSessionInfo(*(followRPBFT->fakePBFTSuite()),
        leaderRPBFT->fakePBFTSuite()->consensus()->keyPair().pub());
    followRPBFT->fakePBFTSuite()->consensus()->wrapperHandleP2PMessage(
        networkException, leaderSession2, receivedP2pMsg);

    receivedP2pMsg = nullptr;
    while (!receivedP2pMsg)
    {
        receivedP2pMsg = followService->getAsyncSendMessageByNodeID(
            leaderRPBFT->fakePBFTSuite()->consensus()->keyPair().pub());
        sleep(1);
    }
    compareAndClearAsyncSendTime(*(followRPBFT->fakePBFTSuite()),
        leaderRPBFT->fakePBFTSuite()->consensus()->keyPair().pub(), 1);

    // receive the rawPrepareRequest and response the rawPrepareReq
    // onReceiveRawPrepareRequest
    std::shared_ptr<FakeSession> followSession =
        std::make_shared<FakeSession>(followRPBFT->fakePBFTSuite()->consensus()->keyPair().pub());
    leaderRPBFT->fakePBFTSuite()->consensus()->wrapperHandleP2PMessage(
        networkException, followSession, receivedP2pMsg);
    receivedP2pMsg = nullptr;
    while (!receivedP2pMsg)
    {
        receivedP2pMsg = leaderService->getAsyncSendMessageByNodeID(
            followRPBFT->fakePBFTSuite()->consensus()->keyPair().pub());
        sleep(1);
    }

    // onReceiveRawPrepareResponse
    followRPBFT->fakePBFTSuite()->consensus()->wrapperOnReceiveRawPrepareResponse(
        leaderSession2, receivedP2pMsg);
    checkPrepareReqEqual(
        followRPBFT->fakePBFTSuite()->consensus()->reqCache()->rawPrepareCachePtr(), prepareReq);
}
BOOST_AUTO_TEST_SUITE_END()
