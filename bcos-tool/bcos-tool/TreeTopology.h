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
 * @brief TreeTopology implementation
 * @file TreeTopology.h
 * @author: yujiechen
 * @date 2023-03-22
 */
#pragma once

#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-utilities/Ranges.h>

#define TREE_LOG(LEVEL)     \
    BCOS_LOG(LEVEL) << LOG_BADGE("TREE") << LOG_KV("consIndex", m_consIndex) \
    << LOG_KV("nodeId", m_nodeId->shortHex())

namespace bcos
{
    namespace tool
    {

        class TreeTopology
        {
        public:
            using Ptr = std::shared_ptr<TreeTopology>;

            TreeTopology(bcos::crypto::NodeIDPtr _nodeId, std::uint32_t const _treeWidth = 3)
                    : m_nodeId(std::move(_nodeId)), m_treeWidth(_treeWidth)
            {
                m_currentConsensusNodes = std::make_shared<bcos::crypto::NodeIDs>();
            }

            virtual ~TreeTopology() = default;
            // update corresponding info when the consensus nodes changed
            virtual void updateConsensusNodeInfo(bcos::crypto::NodeIDs _consensusNodes);

            // select the nodes by tree topology
            virtual bcos::crypto::NodeIDListPtr selectNodes(bcos::crypto::NodeIDSetPtr _peers,
                                                            std::int64_t const _consIndex = 0, bool const _isTheStartNode = false);

            virtual bcos::crypto::NodeIDListPtr selectNodesByNodeID(
                    bcos::crypto::NodeIDSetPtr _peers, bcos::crypto::NodeIDPtr _nodeID,
                    bool const _isTheStartNode = false);

            virtual bcos::crypto::NodeIDListPtr selectParentByNodeID(
                    bcos::crypto::NodeIDSetPtr _peers, bcos::crypto::NodeIDPtr _nodeID);

            virtual bcos::crypto::NodeIDListPtr selectParent(bcos::crypto::NodeIDSetPtr _peers,
                                                             std::int64_t const _consIndex = 0, bool const _selectAll = false);

            virtual std::int64_t consIndex() const
            {
                if (m_consIndex == -1)
                {
                    std::srand(utcTime());
                    return std::rand() % m_nodeNum;
                }
                return m_consIndex;
            }

        protected:
            virtual void updateStartAndEndIndex();

            // select the child nodes by tree
            virtual void recursiveSelectChildNodes(bcos::crypto::NodeIDListPtr _selectedNodeList,
                                                   ssize_t const _parentIndex, bcos::crypto::NodeIDSetPtr _peers,
                                                   std::int64_t const _startIndex);
            // select the parent nodes by tree
            // _selectAll is true:
            // select all the parent(include the grandparent) for the given node
            virtual void selectParentNodes(bcos::crypto::NodeIDListPtr _selectedNodeList,
                                           bcos::crypto::NodeIDSetPtr _peers, std::int64_t const _nodeIndex,
                                           std::int64_t const _startIndex, bool const _selectAll = false);

            std::int64_t getNodeIndex(std::int64_t const _consIndex);

            virtual bool getNodeIDByIndex(bcos::crypto::NodeIDPtr& _nodeID, ssize_t const _nodeIndex) const;

            virtual ssize_t getSelectedNodeIndex(ssize_t const _selectedIndex, ssize_t const _offset);

            ssize_t getNodeIndexByNodeId(bcos::crypto::NodeIDListPtr _findSet, bcos::crypto::NodeIDPtr _nodeId);

            template<RANGES::bidirectional_range NodesType>
            requires requires
            {
                typename NodesType::value_type;
                requires requires(typename NodesType::value_type value)
                {
                    value->data();
                };
            }
            bool compareTwoNodeIDs(NodesType nodes1, NodesType nodes2)
            {
                if(RANGES::size(nodes1) != RANGES::size(nodes2))
                {
                    return false;
                }

                for(auto const& [idx, value] : nodes1 | RANGES::views::enumerate)
                {
                    if(nodes2[idx]->data() != value->data())
                    {
                        return false;
                    }
                }

                return true;
            }

            template<RANGES::bidirectional_range NodesType, RANGES::bidirectional_range OutputType>
            requires requires
            {
                typename NodesType::value_type;
                requires requires(typename NodesType::value_type value)
                {
                    value->data();
                };
                requires requires
                {
                    std::declval<OutputType>().emplace_back(std::declval<typename NodesType::value_type>()->data());
                };
            }
            void extractNodeIDsBytes(NodesType nodes, OutputType& values)
            {
                RANGES::for_each(nodes, [&values](auto& node){ values.emplace_back(node->data()); });
            }

            template<RANGES::bidirectional_range NodesType>
            requires requires
            {
                typename NodesType::value_type;
                requires requires(typename NodesType::value_type value)
                {
                    value->data();
                };
            }
            bool isNodeIDExist(NodesType::value_type node, NodesType nodes)
            {
                for(auto const& n : nodes)
                {
                    if(n->data() == node->data())
                    {
                        return true;
                    }
                }

                return false;
            }

        protected:
            mutable Mutex m_mutex;
            // the list of the current consensus nodes
            bcos::crypto::NodeIDListPtr m_currentConsensusNodes;
            std::atomic_int64_t m_nodeNum{0};

            bcos::crypto::NodeIDPtr m_nodeId;
            std::uint32_t m_treeWidth;
            std::atomic_int64_t m_consIndex{0};

            std::atomic_int64_t m_endIndex{0};
            std::atomic_int64_t m_startIndex{0};
        };

    }  // namespace sync
}  // namespace bcos
