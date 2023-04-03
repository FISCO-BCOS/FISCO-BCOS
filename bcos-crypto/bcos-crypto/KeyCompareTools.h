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

#include "bcos-crypto/bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-utilities/bcos-utilities/FixedBytes.h"
#include "bcos-utilities/bcos-utilities/Ranges.h"

namespace bcos
{
    namespace crypto
    {
        class KeyCompareTools
        {
        public:
            template<RANGES::bidirectional_range NodesType>
            requires requires
            {
                typename NodesType::value_type;
                requires requires(typename NodesType::value_type value)
                {
                    value->data();
                };
            }
            static bool compareTwoNodeIDs(NodesType nodes1, NodesType nodes2)
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
            static void extractNodeIDsBytes(NodesType nodes, OutputType& values)
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
            static bool isNodeIDExist(NodesType::value_type node, NodesType nodes)
            {
                std::cout << "node: " << std::endl;
                std::cout << node->shortHex() << std::endl;

                std::cout << "nodes: " << std::endl;
                for(auto const& n : nodes)
                {
                    std::cout << n->shortHex() << std::endl;
                }

                for(auto const& n : nodes)
                {
                    if(n->data() == node->data())
                    {
                        return true;
                    }
                }

                return false;
            }
        };
    }
}
