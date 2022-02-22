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
 * @file MerkleProofUtility.cpp
 * @author: kyonRay
 * @date 2021-05-24
 */

#include "MerkleProofUtility.h"

using namespace bcos;
using namespace bcos::ledger;

namespace bcos
{
namespace ledger
{
void MerkleProofUtility::makeMerkleProof(const crypto::HashType& _txHash,
    const std::shared_ptr<Parent2ChildListMap>& parent2ChildList,
    const std::shared_ptr<Child2ParentMap>& child2Parent, const std::shared_ptr<MerkleProof>& merkleProof)
{
    std::string merkleNode = _txHash.hex();
    // get child=>parent info
    auto itChild2Parent = child2Parent->find(merkleNode);
    while (itChild2Parent != child2Parent->end())
    {
        // find parent=>childrenList info
        auto itParent2ChildList = parent2ChildList->find(itChild2Parent->second);
        if (itParent2ChildList == parent2ChildList->end())
        {
            break;
        }
        // get index from itParent2ChildList->second by merkleNode
        auto itChildList = std::find(
            itParent2ChildList->second.begin(), itParent2ChildList->second.end(), merkleNode);
        if (itChildList == itParent2ChildList->second.end())
        {
            break;
        }
        // leftPath = [childrenList.begin, index)
        std::vector<std::string> leftPath{};
        // rightPath = (index, childrenList.end]
        std::vector<std::string> rightPath{};
        leftPath.insert(leftPath.end(), itParent2ChildList->second.begin(), itChildList);
        rightPath.insert(rightPath.end(), std::next(itChildList), itParent2ChildList->second.end());

        auto singleTree = std::make_pair(std::move(leftPath), std::move(rightPath));
        merkleProof->emplace_back(singleTree);

        // node=parent
        merkleNode = itChild2Parent->second;
        itChild2Parent = child2Parent->find(merkleNode);
    }
}

std::shared_ptr<Child2ParentMap> MerkleProofUtility::getChild2Parent(
    const std::shared_ptr<Parent2ChildListMap>& _parent2Child)
{
    auto child2Parent = std::make_shared<Child2ParentMap>();

    // trans parent2ChildList into child2Parent concurrently
    tbb::parallel_for_each(
        _parent2Child->begin(), _parent2Child->end(), [&](auto const& _childListIterator) {
            tbb::parallel_for(tbb::blocked_range<size_t>(0, _childListIterator.second.size()),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t i = range.begin(); i < range.end(); i++)
                    {
                        std::string child = _childListIterator.second[i];
                        if (!child.empty())
                        {
                            child2Parent->insert({child, _childListIterator.first});
                        }
                    }
                });
        });

    return child2Parent;
}

}  // namespace ledger
}  // namespace bcos
