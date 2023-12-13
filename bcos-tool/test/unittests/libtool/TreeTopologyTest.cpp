/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file TreeTopologyTest.cpp
 * @author: kyonGuo
 * @date 2023/8/2
 */

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <bcos-tool/TreeTopology.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::crypto;

namespace bcos::test
{
class TreeTopologyTestFixture : public TestPromptFixture
{
public:
    TreeTopologyTestFixture()
      : localKey(std::make_shared<KeyImpl>(
            h256("0000000000000000000000000000000000000000000000000000000000000000").asBytes())),
        treeTopology(localKey)
    {
        for (int i = 0; i < 10; ++i)
        {
            std::stringstream nodeStr;
            nodeStr << std::setw(64) << std::setfill('0') << i;
            auto node = std::make_shared<KeyImpl>(h256(nodeStr.str()).asBytes());
            consensusList.push_back(node);
            peers->insert(node);
        }
    };
    std::uint32_t treeWidth = 3;
    NodeIDPtr localKey;
    TreeTopology treeTopology;
    NodeIDs consensusList = {};
    NodeIDSetPtr peers = std::make_shared<NodeIDSet>();
};
BOOST_FIXTURE_TEST_SUITE(TreeTopologyTest, TreeTopologyTestFixture)
BOOST_AUTO_TEST_CASE(UpdateTreeTopologyTest)
{
    treeTopology.updateConsensusNodeInfo(consensusList);
    BOOST_CHECK(treeTopology.consIndex() == 0);

    const size_t consensusInsertSize = 8;
    for (size_t i = 0; i < consensusInsertSize; ++i)
    {
        std::stringstream nodeStr;
        nodeStr << std::setw(64) << std::setfill('0') << (i + 10000);
        auto node = std::make_shared<KeyImpl>(h256(nodeStr.str()).asBytes());
        consensusList.insert(consensusList.cbegin(), node);
        peers->insert(node);
    }

    treeTopology.updateConsensusNodeInfo(consensusList);
    BOOST_CHECK(treeTopology.consIndex() == consensusInsertSize);

    // select correct
    auto selectedNodes = treeTopology.selectNodes(peers, treeTopology.consIndex());
    BOOST_CHECK(selectedNodes->size() == treeWidth);
    for (size_t i = 0; i < treeWidth; ++i)
    {
        BOOST_CHECK(selectedNodes->contains(consensusList.at(consensusInsertSize + i + 1)));
    }
}

BOOST_AUTO_TEST_CASE(SelectTest)
{
    // observer
    consensusList.erase(consensusList.begin());
    treeTopology.updateConsensusNodeInfo(consensusList);

    auto selectedNodes = treeTopology.selectNodes(peers, treeTopology.consIndex());
    BOOST_CHECK(selectedNodes->empty());

    selectedNodes = treeTopology.selectNodes(peers, treeTopology.consIndex(), true);
    BOOST_CHECK(selectedNodes->size() == 1);

    selectedNodes = treeTopology.selectParent(peers, treeTopology.consIndex(), true);
    BOOST_CHECK(selectedNodes->empty());

    consensusList.insert(consensusList.begin(), localKey);
    treeTopology.updateConsensusNodeInfo(consensusList);

    const size_t consensusInsertSize = 8;
    for (size_t i = 0; i < consensusInsertSize; ++i)
    {
        std::stringstream nodeStr;
        nodeStr << std::setw(64) << std::setfill('0') << (i + 10000);
        auto node = std::make_shared<KeyImpl>(h256(nodeStr.str()).asBytes());
        peers->insert(node);
    }

    for (const auto& nodeId : (*peers))
    {
        auto nodeIdSet = treeTopology.selectParentByNodeID(peers, nodeId);
        if (nodeId->data() == localKey->data() ||
            std::find_if(consensusList.cbegin(), consensusList.cend(), [&nodeId](auto&& node) {
                return node->data() == nodeId->data();
            }) == consensusList.cend())
        {
            BOOST_CHECK(nodeIdSet->empty());
        }
        else
        {
            BOOST_CHECK(nodeIdSet->size() == 1);
        }
    }
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test