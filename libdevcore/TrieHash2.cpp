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


#define TRIEHASH_SESSION_LOG(LEVEL) \
    LOG(LEVEL) << "[TrieHash]"      \
               << "[line:" << __LINE__ << "]"

namespace dev
{
void hash256Recursive(HexMap const& _s, HexMap::const_iterator _begin, HexMap::const_iterator _end,
    unsigned _preLen, bytes& _bytes, std::vector<bytes>& _bytePath,
    std::map<std::string, std::vector<std::string>>& _parent2ChildList);
void hash256Recursive(HexMap const& _s, HexMap::const_iterator _begin, HexMap::const_iterator _end,
    unsigned _preLen, bytes& _bytes);

bytes h256ForOneNode(
    HexMap const& _s, std::map<std::string, std::vector<std::string>>& _parent2ChildList);

bytes h256ForOneNode(HexMap const& _s);

unsigned getSharedPre(HexMap::const_iterator _begin, HexMap::const_iterator _end, unsigned _preLen)
{
    unsigned sharedPre = (unsigned)-1;
    for (auto i = std::next(_begin); i != _end && sharedPre; ++i)
    {
        unsigned x = std::min(
            sharedPre, std::min((unsigned)_begin->first.size(), (unsigned)i->first.size()));
        unsigned shared = _preLen;
        for (; shared < x && _begin->first[shared] == i->first[shared]; ++shared)
        {
        }
        sharedPre = std::min(shared, sharedPre);
    }
    return sharedPre;
}

void hash256TrieTree(HexMap const& _s, HexMap::const_iterator _begin, HexMap::const_iterator _end,
    unsigned _preLen, bytes& _bytes)
{
    if (_begin == _end)
    {
    }
    else if (std::next(_begin) == _end)
    {
        bytes bytesTemp;
        bytesTemp.insert(bytesTemp.end(), _begin->second.begin(), _begin->second.end());
        _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
    }
    else
    {
        unsigned sharedPre = getSharedPre(_begin, _end, _preLen);
        if (sharedPre > _preLen)
        {
            bytes bytesTemp;
            hash256Recursive(_s, _begin, _end, (unsigned)sharedPre, bytesTemp);
            _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
        }
        else
        {
            auto b = _begin;
            if (_preLen == b->first.size())
                ++b;
            for (auto i = 0; i < 16; ++i)
            {
                auto n = b;
                for (; n != _end && n->first[_preLen] == i; ++n)
                {
                }
                if (b == n)
                {
                }
                else
                {
                    bytes bytesTemp;
                    hash256Recursive(_s, b, n, _preLen + 1, bytesTemp);
                    _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
                }
                b = n;
            }
            if (_preLen == _begin->first.size())
            {
                _bytes.insert(_bytes.end(), _begin->second.begin(), _begin->second.end());
            }
        }
    }
}
void hash256TrieTree(HexMap const& _s, HexMap::const_iterator _begin, HexMap::const_iterator _end,
    unsigned _preLen, bytes& _bytes, int32_t& _nodeType, std::vector<bytes>& _bytePath,
    std::map<std::string, std::vector<std::string>>& _parent2ChildList)
{
    if (_begin == _end)
    {
        _bytePath.push_back(bytes());
    }
    else if (std::next(_begin) == _end)
    {
        bytes bytesTemp;
        bytesTemp.insert(bytesTemp.end(), _begin->second.begin(), _begin->second.end());
        _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
        _bytePath.emplace_back(std::move(bytesTemp));
        _nodeType = trieHashNodeType::typeLeafNode;
    }
    else
    {
        unsigned sharedPre = getSharedPre(_begin, _end, _preLen);
        if (sharedPre > _preLen)
        {
            bytes bytesTemp;
            hash256Recursive(
                _s, _begin, _end, (unsigned)sharedPre, bytesTemp, _bytePath, _parent2ChildList);
            _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
            _nodeType = trieHashNodeType::typeAllNodeHaveCommonPrefix;
        }
        else
        {
            auto b = _begin;
            if (_preLen == b->first.size())
                ++b;
            for (auto i = 0; i < 16; ++i)
            {
                auto n = b;
                for (; n != _end && n->first[_preLen] == i; ++n)
                {
                }
                if (b == n)
                {
                    _bytePath.push_back(bytes());
                }
                else
                {
                    bytes bytesTemp;
                    hash256Recursive(
                        _s, b, n, _preLen + 1, bytesTemp, _bytePath, _parent2ChildList);
                    _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
                    _nodeType = trieHashNodeType::typeIntermediateNode;
                }
                b = n;
            }
            bytes bytesTemp;
            if (_preLen == _begin->first.size())
            {
                bytesTemp.insert(bytesTemp.end(), _begin->second.begin(), _begin->second.end());
                _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
            }
            _bytePath.emplace_back(std::move(bytesTemp));
        }
    }
}
void hash256Recursive(HexMap const& _s, HexMap::const_iterator _begin, HexMap::const_iterator _end,
    unsigned _preLen, bytes& _bytes)
{
    bytes bytesTemp;
    hash256TrieTree(_s, _begin, _end, _preLen, bytesTemp);
    h256 hValue = sha3(bytesTemp);
    _bytes.insert(_bytes.end(), hValue.begin(), hValue.end());
}
void hash256Recursive(HexMap const& _s, HexMap::const_iterator _begin, HexMap::const_iterator _end,
    unsigned _preLen, bytes& _bytes, std::vector<bytes>& _bytePath,
    std::map<std::string, std::vector<std::string>>& _parent2ChildList)
{
    bytes bytesTemp;
    std::vector<bytes> bytePath;
    int32_t nodeType = trieHashNodeType::typeDefault;
    hash256TrieTree(_s, _begin, _end, _preLen, bytesTemp, nodeType, bytePath, _parent2ChildList);

    if (nodeType == trieHashNodeType::typeIntermediateNode ||
        nodeType == trieHashNodeType::typeAllNodeHaveCommonPrefix)
    {
        std::string parentNode = sha3(bytesTemp).hex();
        for (const auto& it : bytePath)
        {
            _parent2ChildList[parentNode].emplace_back(toHex(it));
        }
    }
    h256 hValue = sha3(bytesTemp);
    _bytes.insert(_bytes.end(), hValue.begin(), hValue.end());
    _bytePath.push_back(hValue.asBytes());
}
bytes getTrieTree256(
    BytesMap const& _s, std::map<std::string, std::vector<std::string>>& _parent2ChildList)
{
    if (_s.empty())
    {
        return bytes();
    }
    HexMap hexMap;
    for (auto i = _s.rbegin(); i != _s.rend(); ++i)
    {
        bytes bytesData = i->first;
        bytesData.insert(bytesData.end(), i->second.begin(), i->second.end());
        hexMap[asNibbles(bytesConstRef(&i->first))] = bytesData;
    }

    /*just one element*/
    if (std::next(hexMap.begin()) == hexMap.end())
    {
        return h256ForOneNode(hexMap, _parent2ChildList);
    }
    std::vector<bytes> bytePath;
    bytes bytesTemp;
    int32_t nodeType = trieHashNodeType::typeDefault;
    hash256TrieTree(hexMap, hexMap.cbegin(), hexMap.cend(), 0, bytesTemp, nodeType, bytePath,
        _parent2ChildList);
    std::string rootNode = sha3(bytesTemp).hex();
    for (const auto& it : bytePath)
    {
        _parent2ChildList[rootNode].emplace_back(toHex(it));
    }
    return bytesTemp;
}
bytes getTrieTree256(BytesMap const& _s)
{
    if (_s.empty())
    {
        return bytes();
    }
    HexMap hexMap;
    for (auto i = _s.rbegin(); i != _s.rend(); ++i)
    {
        bytes bytesData = i->first;
        bytesData.insert(bytesData.end(), i->second.begin(), i->second.end());
        hexMap[asNibbles(bytesConstRef(&i->first))] = bytesData;
    }

    /*just one element*/
    if (std::next(hexMap.begin()) == hexMap.end())
    {
        return h256ForOneNode(hexMap);
    }
    bytes bytesTemp;
    hash256TrieTree(hexMap, hexMap.cbegin(), hexMap.cend(), 0, bytesTemp);
    return bytesTemp;
}
bytes h256ForOneNode(
    HexMap const& _hexMap, std::map<std::string, std::vector<std::string>>& _parent2ChildList)
{
    bytes bytesChild;
    bytesChild.insert(
        bytesChild.end(), _hexMap.begin()->second.begin(), _hexMap.begin()->second.end());
    dev::h256 parentNode = sha3(bytesChild);
    _parent2ChildList[sha3(parentNode).hex()].push_back(toHex(parentNode));
    return parentNode.asBytes();
}
bytes h256ForOneNode(HexMap const& _hexMap)
{
    bytes bytesChild;
    bytesChild.insert(
        bytesChild.end(), _hexMap.begin()->second.begin(), _hexMap.begin()->second.end());
    return sha3(bytesChild).asBytes();
}
h256 getHash256(
    BytesMap const& _s, std::map<std::string, std::vector<std::string>>& _parent2ChildList)
{
    return sha3(getTrieTree256(_s, _parent2ChildList));
}
h256 getHash256(BytesMap const& _s)
{
    return sha3(getTrieTree256(_s));
}
}  // namespace dev
