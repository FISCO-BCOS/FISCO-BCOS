/**
 *  Copyright (C) 2020 FISCO BCOS.
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
 * @brief: calc trie hash with merkle tree
 *
 * @file: ParallelMerkleProof.cpp
 * @author: darrenyin
 * @date 2019-09-24
 */

#include "ParallelMerkleProof.h"
#include "libdevcrypto/CryptoInterface.h"
#include <tbb/parallel_for.h>
#include <mutex>


#define TRIEHASH_SESSION_LOG(LEVEL) \
    LOG(LEVEL) << "[TrieHash]"      \
               << "[line:" << __LINE__ << "]"

static const uint32_t MAX_CHILD_COUNT = 16;

namespace bcos
{
h256 getMerkleProofRoot(const std::vector<bcos::bytes>& _bytesCaches)
{
    if (_bytesCaches.empty())
    {
        return crypto::Hash(bytes());
    }
    std::vector<bcos::bytes> bytesCachesTemp;
    bytesCachesTemp.insert(bytesCachesTemp.end(),
        std::make_move_iterator(const_cast<std::vector<bcos::bytes>&>(_bytesCaches).begin()),
        std::make_move_iterator(const_cast<std::vector<bcos::bytes>&>(_bytesCaches).end()));

    while (bytesCachesTemp.size() > 1)
    {
        std::vector<bcos::bytes> higherLevelList;
        int size = (bytesCachesTemp.size() + MAX_CHILD_COUNT - 1) / MAX_CHILD_COUNT;
        higherLevelList.resize(size);
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, size), [&](const tbb::blocked_range<size_t>& _r) {
                for (uint32_t i = _r.begin(); i < _r.end(); ++i)
                {
                    bytes byteValue;
                    for (uint32_t j = 0; j < MAX_CHILD_COUNT; j++)
                    {
                        uint32_t index = i * MAX_CHILD_COUNT + j;
                        if (index < bytesCachesTemp.size())
                        {
                            byteValue.insert(byteValue.end(), bytesCachesTemp[index].begin(),
                                bytesCachesTemp[index].end());
                        }
                    }
                    higherLevelList[i] = crypto::Hash(byteValue).asBytes();
                }
            });
        bytesCachesTemp = std::move(higherLevelList);
    }
    return crypto::Hash(bytesCachesTemp[0]);
}

void getMerkleProof(const std::vector<bcos::bytes>& _bytesCaches,
    std::shared_ptr<std::map<std::string, std::vector<std::string>>> _parent2ChildList)
{
    if (_bytesCaches.empty())
    {
        return;
    }
    std::vector<bcos::bytes> bytesCachesTemp;
    bytesCachesTemp.insert(bytesCachesTemp.end(),
        std::make_move_iterator(const_cast<std::vector<bcos::bytes>&>(_bytesCaches).begin()),
        std::make_move_iterator(const_cast<std::vector<bcos::bytes>&>(_bytesCaches).end()));
    std::mutex mapMutex;
    while (bytesCachesTemp.size() > 1)
    {
        std::vector<bcos::bytes> higherLevelList;
        int size = (bytesCachesTemp.size() + MAX_CHILD_COUNT - 1) / MAX_CHILD_COUNT;
        higherLevelList.resize(size);
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, size), [&](const tbb::blocked_range<size_t>& _r) {
                for (uint32_t i = _r.begin(); i < _r.end(); ++i)
                {
                    bytes byteValue;
                    std::vector<bcos::bytes> childList;
                    for (uint32_t j = 0; j < MAX_CHILD_COUNT; j++)
                    {
                        uint32_t index = i * MAX_CHILD_COUNT + j;
                        if (index < bytesCachesTemp.size())
                        {
                            byteValue.insert(byteValue.end(), bytesCachesTemp[index].begin(),
                                bytesCachesTemp[index].end());
                            childList.push_back(bytesCachesTemp[index]);
                        }
                    }
                    higherLevelList[i] = crypto::Hash(byteValue).asBytes();
                    std::lock_guard<std::mutex> l(mapMutex);
                    std::string parentNode = *toHexString(higherLevelList[i]);
                    for (const auto& child : childList)
                    {
                        (*_parent2ChildList)[parentNode].emplace_back(*toHexString(child));
                    }
                }
            });
        bytesCachesTemp = std::move(higherLevelList);
    }

    (*_parent2ChildList)[*toHexString(crypto::Hash(bytesCachesTemp[0]).asBytes())].push_back(
        *toHexString(bytesCachesTemp[0]));
}

}  // namespace bcos
