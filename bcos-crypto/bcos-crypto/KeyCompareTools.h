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
 * @brief tool of key compare
 * @file KeyCompare.h
 * @date 2023.03.27
 * @author chuwen
 */

#pragma once

#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-utilities/FixedBytes.h"
#include "bcos-utilities/Ranges.h"

namespace bcos::crypto
{
template <class NodeType>
concept Node = requires(NodeType node)
{
    node->data();
};
template <class NodesType>
concept Nodes = requires(NodesType nodesType)
{
    requires RANGES::bidirectional_range<NodesType>;
    requires Node<RANGES::range_value_t<NodesType>>;
};
class KeyCompareTools
{
public:
    static bool compareTwoNodeIDs(Nodes auto const& nodes1, Nodes auto const& nodes2)
    {
        if (RANGES::size(nodes1) != RANGES::size(nodes2))
        {
            return false;
        }

        for (auto const& [idx, value] : nodes1 | RANGES::views::enumerate)
        {
            if (nodes2[idx]->data() != value->data())
            {
                return false;
            }
        }
        return true;
    }

    static bool isNodeIDExist(Node auto const& node, Nodes auto const& nodes)
    {
        return RANGES::find_if(RANGES::begin(nodes), RANGES::end(nodes),
                   [&node](auto&& n) { return n->data() == node->data(); }) != RANGES::end(nodes);
    }
};
}  // namespace bcos::crypto
