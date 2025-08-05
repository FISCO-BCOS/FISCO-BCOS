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
#include "bcos-crypto/bcos-crypto/signature/key/KeyImpl.h"
#include "bcos-crypto/bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
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
    auto consensusNode1 = ConsensusNode{nodeId, Type::consensus_sealer, weight, 0, 0};

    std::string node2 = "1234";
    auto nodeId2 = std::make_shared<KeyImpl>(bytes(node2.begin(), node2.end()));
    auto consensusNode2 = ConsensusNode{nodeId2, Type::consensus_sealer, weight, 0, 0};

    auto nodeId3 = std::make_shared<KeyImpl>(bytes(node1.begin(), node1.end()));
    auto consensusNode3 = ConsensusNode{nodeId3, Type::consensus_sealer, weight, 0, 0};

    // test set
    std::set<ConsensusNode> consensusNodeList;
    consensusNodeList.insert(consensusNode1);
    BOOST_TEST(consensusNodeList.count(consensusNode1));
    BOOST_TEST(consensusNodeList.size() == 1);

    consensusNodeList.insert(consensusNode2);
    BOOST_TEST(consensusNodeList.count(consensusNode2));
    BOOST_TEST(consensusNodeList.size() == 2);

    consensusNodeList.insert(consensusNode3);
    BOOST_TEST(consensusNodeList.count(consensusNode3));
    BOOST_TEST(consensusNodeList.size() == 2);

    // check map
    std::map<NodeIDPtr, ConsensusNode, KeyCompare> nodeId2ConsensusNode;
    nodeId2ConsensusNode.insert(std::make_pair(consensusNode1.nodeID, consensusNode1));
    BOOST_TEST(nodeId2ConsensusNode.count(consensusNode1.nodeID));
    BOOST_TEST(!nodeId2ConsensusNode.count(consensusNode2.nodeID));
    BOOST_TEST(nodeId2ConsensusNode.size() == 1);

    nodeId2ConsensusNode.insert(std::make_pair(consensusNode2.nodeID, consensusNode2));
    BOOST_TEST(nodeId2ConsensusNode.count(consensusNode2.nodeID));
    BOOST_TEST(nodeId2ConsensusNode.count(consensusNode3.nodeID));
    BOOST_TEST(nodeId2ConsensusNode.size() == 2);
    nodeId2ConsensusNode.insert(std::make_pair(consensusNode3.nodeID, consensusNode3));
    BOOST_TEST(nodeId2ConsensusNode.size() == 2);

    // test NodeIDSet
    NodeIDSet nodeIdSet;
    nodeIdSet.insert(nodeId);
    BOOST_TEST(nodeIdSet.count(nodeId));
    BOOST_TEST(nodeIdSet.size() == 1);

    nodeIdSet.insert(nodeId2);
    BOOST_TEST(nodeIdSet.count(nodeId2));
    BOOST_TEST(nodeIdSet.size() == 2);

    nodeIdSet.insert(nodeId3);
    BOOST_TEST(nodeIdSet.count(nodeId3));
    BOOST_TEST(nodeIdSet.size() == 2);
}

BOOST_AUTO_TEST_CASE(test_stringToModuleID)
{
    BOOST_TEST(bcos::protocol::ModuleID::Raft == protocol::stringToModuleID("raft").value());
    BOOST_TEST(bcos::protocol::ModuleID::Raft == protocol::stringToModuleID("Raft").value());
    BOOST_TEST(bcos::protocol::ModuleID::Raft == protocol::stringToModuleID("RAFT").value());

    BOOST_TEST(bcos::protocol::ModuleID::PBFT == protocol::stringToModuleID("pbft").value());
    BOOST_TEST(bcos::protocol::ModuleID::PBFT == protocol::stringToModuleID("Pbft").value());
    BOOST_TEST(bcos::protocol::ModuleID::PBFT == protocol::stringToModuleID("PBFT").value());

    BOOST_TEST(bcos::protocol::ModuleID::AMOP == protocol::stringToModuleID("amop").value());
    BOOST_TEST(bcos::protocol::ModuleID::AMOP == protocol::stringToModuleID("Amop").value());
    BOOST_TEST(bcos::protocol::ModuleID::AMOP == protocol::stringToModuleID("AMOP").value());

    BOOST_TEST(
        bcos::protocol::ModuleID::BlockSync == protocol::stringToModuleID("block_sync").value());
    BOOST_TEST(
        bcos::protocol::ModuleID::BlockSync == protocol::stringToModuleID("Block_sync").value());
    BOOST_TEST(
        bcos::protocol::ModuleID::BlockSync == protocol::stringToModuleID("BLOCK_SYNC").value());

    BOOST_TEST(
        bcos::protocol::ModuleID::TxsSync == protocol::stringToModuleID("txs_sync").value());
    BOOST_TEST(
        bcos::protocol::ModuleID::TxsSync == protocol::stringToModuleID("Txs_sync").value());
    BOOST_TEST(
        bcos::protocol::ModuleID::TxsSync == protocol::stringToModuleID("TXS_SYNC").value());

    BOOST_TEST(bcos::protocol::ModuleID::ConsTxsSync ==
                protocol::stringToModuleID("cons_txs_sync").value());
    BOOST_TEST(bcos::protocol::ModuleID::ConsTxsSync ==
                protocol::stringToModuleID("cons_Txs_sync").value());
    BOOST_TEST(bcos::protocol::ModuleID::ConsTxsSync ==
                protocol::stringToModuleID("CONS_TXS_SYNC").value());


    BOOST_TEST(!protocol::stringToModuleID("aa").has_value());
}


BOOST_AUTO_TEST_CASE(test_moduleIDToString)
{
    BOOST_TEST("raft" == protocol::moduleIDToString(protocol::ModuleID::Raft));
    BOOST_TEST("pbft" == protocol::moduleIDToString(protocol::ModuleID::PBFT));
    BOOST_TEST("amop" == protocol::moduleIDToString(protocol::ModuleID::AMOP));
    BOOST_TEST("block_sync" == protocol::moduleIDToString(protocol::ModuleID::BlockSync));
    BOOST_TEST("txs_sync" == protocol::moduleIDToString(protocol::ModuleID::TxsSync));
    BOOST_TEST(
        "light_node" == protocol::moduleIDToString(protocol::ModuleID::LIGHTNODE_GET_BLOCK));
    BOOST_TEST("cons_txs_sync" == protocol::moduleIDToString(protocol::ModuleID::ConsTxsSync));
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos