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
 * @brief Unit tests for the ConsensusNode
 * @file ConsensusNodeTest.cpp
 */
#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/consensus/ConsensusNodeInterface.h"
#include <bcos-crypto/signature/key/KeyImpl.h>
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
BOOST_FIXTURE_TEST_SUITE(ConsensusNodeTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testConsensusNode)
{
    // test operator
    std::string node1 = "123";
    uint64_t weight = 1;
    auto nodeId = std::make_shared<KeyImpl>(bytes(node1.begin(), node1.end()));
    auto consensusNode1 = std::make_shared<ConsensusNode>(nodeId, weight);

    std::string node2 = "1234";
    auto nodeId2 = std::make_shared<KeyImpl>(bytes(node2.begin(), node2.end()));
    auto consensusNode2 = std::make_shared<ConsensusNode>(nodeId2, weight);

    auto nodeId3 = std::make_shared<KeyImpl>(bytes(node1.begin(), node1.end()));
    auto consensusNode3 = std::make_shared<ConsensusNode>(nodeId3, weight);

    // test set
    std::set<ConsensusNodeInterface::Ptr, ConsensusNodeComparator> consensusNodeList;
    consensusNodeList.insert(consensusNode1);
    BOOST_CHECK(consensusNodeList.count(consensusNode1));
    BOOST_CHECK(consensusNodeList.size() == 1);

    consensusNodeList.insert(consensusNode2);
    BOOST_CHECK(consensusNodeList.count(consensusNode2));
    BOOST_CHECK(consensusNodeList.size() == 2);

    consensusNodeList.insert(consensusNode3);
    BOOST_CHECK(consensusNodeList.count(consensusNode3));
    BOOST_CHECK(consensusNodeList.size() == 2);

    // check map
    std::map<NodeIDPtr, ConsensusNodeInterface::Ptr, KeyCompare> nodeId2ConsensusNode;
    nodeId2ConsensusNode.insert(std::make_pair(consensusNode1->nodeID(), consensusNode1));
    BOOST_CHECK(nodeId2ConsensusNode.count(consensusNode1->nodeID()));
    BOOST_CHECK(!nodeId2ConsensusNode.count(consensusNode2->nodeID()));
    BOOST_CHECK(nodeId2ConsensusNode.size() == 1);

    nodeId2ConsensusNode.insert(std::make_pair(consensusNode2->nodeID(), consensusNode2));
    BOOST_CHECK(nodeId2ConsensusNode.count(consensusNode2->nodeID()));
    BOOST_CHECK(nodeId2ConsensusNode.count(consensusNode3->nodeID()));
    BOOST_CHECK(nodeId2ConsensusNode.size() == 2);
    nodeId2ConsensusNode.insert(std::make_pair(consensusNode3->nodeID(), consensusNode3));
    BOOST_CHECK(nodeId2ConsensusNode.size() == 2);

    // test NodeIDSet
    NodeIDSet nodeIdSet;
    nodeIdSet.insert(nodeId);
    BOOST_CHECK(nodeIdSet.count(nodeId));
    BOOST_CHECK(nodeIdSet.size() == 1);

    nodeIdSet.insert(nodeId2);
    BOOST_CHECK(nodeIdSet.count(nodeId2));
    BOOST_CHECK(nodeIdSet.size() == 2);

    nodeIdSet.insert(nodeId3);
    BOOST_CHECK(nodeIdSet.count(nodeId3));
    BOOST_CHECK(nodeIdSet.size() == 2);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos