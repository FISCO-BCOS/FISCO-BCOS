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
 * @file: TrieHash2.h
 * @author: darrenyin
 * @date 2019-09-24
 */
#pragma once

#include <libdevcore/FixedHash.h>
#include <vector>

namespace dev
{
enum trieHashNodeType
{
    typeDefault = 0,
    typeLeafNode = 1,
    typeAllNodeHaveCommonPrefix = 2,
    typeIntermediateNode = 3,
};

bytes getTrieTree256(BytesMap const& _s,
    std::shared_ptr<std::map<std::string, std::vector<std::string>>> _parent2ChildList);
h256 getHash256(BytesMap const& _s,
    std::shared_ptr<std::map<std::string, std::vector<std::string>>> _parent2ChildList);
}  // namespace dev
