/**
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
 * (c) 2016-2019 fisco-dev contributors.
 *
 * @brief: calc trie hash with merkle tree
 *
 * @file: TrieHash2.cpp
 * @author: darrenyin
 * @date 2019-09-24
 */

#include "TrieHash2.h"
#include "Log.h"
#include "TrieCommon.h"
#include "TrieDB.h"  // @TODO replace ASAP!
#include <tbb/parallel_for.h>
#include <mutex>


#define TRIEHASH_SESSION_LOG(LEVEL) \
    LOG(LEVEL) << "[TrieHash]"      \
               << "[line:" << __LINE__ << "]"

static const uint32_t MAX_CHILD_COUNT = 16;

namespace dev
{
h256 getHash256(const std::vector<dev::bytes>& _bytesCaches)
{
    if (_bytesCaches.empty())
    {
        return crypto::Hash(bytes());
    }
    std::vector<dev::bytes> bytesCachesTemp;
    bytesCachesTemp.insert(bytesCachesTemp.end(),
        std::make_move_iterator(const_cast<std::vector<dev::bytes>&>(_bytesCaches).begin()),
        std::make_move_iterator(const_cast<std::vector<dev::bytes>&>(_bytesCaches).end()));

    while (bytesCachesTemp.size() > 1)
    {
        std::vector<dev::bytes> higherLevelList;
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

void getMerkleProof(const std::vector<dev::bytes>& _bytesCaches,
    std::shared_ptr<std::map<std::string, std::vector<std::string>>> _parent2ChildList)
{
    if (_bytesCaches.empty())
    {
        return;
    }
    std::vector<dev::bytes> bytesCachesTemp;
    bytesCachesTemp.insert(bytesCachesTemp.end(),
        std::make_move_iterator(const_cast<std::vector<dev::bytes>&>(_bytesCaches).begin()),
        std::make_move_iterator(const_cast<std::vector<dev::bytes>&>(_bytesCaches).end()));
    std::mutex mapMutex;
    while (bytesCachesTemp.size() > 1)
    {
        std::vector<dev::bytes> higherLevelList;
        int size = (bytesCachesTemp.size() + MAX_CHILD_COUNT - 1) / MAX_CHILD_COUNT;
        higherLevelList.resize(size);
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, size), [&](const tbb::blocked_range<size_t>& _r) {
                for (uint32_t i = _r.begin(); i < _r.end(); ++i)
                {
                    bytes byteValue;
                    std::vector<dev::bytes> childList;
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
                    std::string parentNode = toHex(higherLevelList[i]);
                    for (const auto& child : childList)
                    {
                        (*_parent2ChildList)[parentNode].emplace_back(toHex(child));
                    }
                }
            });
        bytesCachesTemp = std::move(higherLevelList);
    }

    (*_parent2ChildList)[toHex(crypto::Hash(bytesCachesTemp[0]).asBytes())].push_back(
        toHex(bytesCachesTemp[0]));
}

}  // namespace dev
