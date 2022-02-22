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
 * @file MerkleProofUtility.h
 * @author: kyonRay
 * @date 2021-04-29
 */

#pragma once

#include "Common.h"
#include <bcos-codec/scale/Scale.h>
#include <bcos-framework/interfaces/ledger/LedgerTypeDef.h>
#include <bcos-protocol/ParallelMerkleProof.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_for_each.h>

#include <utility>

namespace bcos::ledger
{
class MerkleProofUtility
{
public:
    template <typename T>
    void getMerkleProof(const crypto::HashType& _txHash, T _ts, crypto::CryptoSuite::Ptr _crypto,
        const std::shared_ptr<MerkleProof>& merkleProof)
    {
        auto parent2Child = getParent2ChildList(_crypto, _ts);
        auto child2Parent = getChild2Parent(parent2Child);
        makeMerkleProof(_txHash, std::move(parent2Child), std::move(child2Parent), merkleProof);
    }

    template <typename T>
    std::shared_ptr<Parent2ChildListMap> getParent2ChildList(
        crypto::CryptoSuite::Ptr _crypto, T _ts)
    {
        auto merklePath = std::make_shared<Parent2ChildListMap>();
        tbb::concurrent_vector<bytes> tsVector;
        tsVector.resize(_ts.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, _ts.size()),
            [_ts = std::move(_ts), &tsVector](const tbb::blocked_range<size_t>& _r) {
                for (uint32_t i = _r.begin(); i < _r.end(); ++i)
                {
                    crypto::HashType hash = ((_ts)[i])->hash();
                    tsVector[i] = hash.asBytes();
                }
            });
        auto tsList = std::vector<bytes>(tsVector.begin(), tsVector.end());
        protocol::calculateMerkleProof(std::move(_crypto), tsList, merklePath);
        return merklePath;
    }

    void makeMerkleProof(const crypto::HashType& _txHash,
        const std::shared_ptr<Parent2ChildListMap>& parent2ChildList,
        const std::shared_ptr<Child2ParentMap>& child2Parent,
        const std::shared_ptr<MerkleProof>& merkleProof);

    std::shared_ptr<Child2ParentMap> getChild2Parent(
        const std::shared_ptr<Parent2ChildListMap>& _parent2Child);
};


}  // namespace bcos::ledger