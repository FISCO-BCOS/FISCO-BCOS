/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "TrieHash_V2_2_0.h"
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

void hash256TrieTree(HexMap const& _s, HexMap::const_iterator _begin, HexMap::const_iterator _end,
    unsigned _preLen, bytes& _bytes, int32_t& nodeType, std::vector<bytes>& bytePath,
    std::map<std::string, std::vector<std::string>>& parent2ChildList)
{
    if (_begin == _end)
    {
        bytes bytesTemp;
        _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
        bytePath.push_back(bytesTemp);
    }
    else if (std::next(_begin) == _end)
    {
        bytes bytesTemp;
        bytesTemp.insert(bytesTemp.end(), _begin->first.begin(), _begin->first.end());
        bytesTemp.insert(bytesTemp.end(), _begin->second.begin(), _begin->second.end());
        _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
        bytePath.push_back(bytesTemp);
        nodeType = trieHashNodeType::typeLeafNode;
    }
    else
    {
        unsigned sharedPre = (unsigned)-1;
        unsigned c = 0;
        for (auto i = std::next(_begin); i != _end && sharedPre; ++i, ++c)
        {
            unsigned x = std::min(
                sharedPre, std::min((unsigned)_begin->first.size(), (unsigned)i->first.size()));
            unsigned shared = _preLen;
            for (; shared < x && _begin->first[shared] == i->first[shared]; ++shared)
            {
            }
            sharedPre = std::min(shared, sharedPre);
        }
        if (sharedPre > _preLen)
        {
            _bytes.insert(_bytes.end(), _begin->first.begin(), _begin->first.end());
            bytes bytesTemp;
            std::map<std::string, std::vector<std::string>> parent2ChildListTemp;
            hash256Recursive(
                _s, _begin, _end, (unsigned)sharedPre, bytesTemp, bytePath, parent2ChildListTemp);
            _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
            bytePath.push_back(bytesTemp);
            parent2ChildList.insert(parent2ChildListTemp.begin(), parent2ChildListTemp.end());
            nodeType = trieHashNodeType::typeAllNode;
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
                    bytes bytesTemp;
                    _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
                    bytePath.push_back(bytesTemp);
                }
                else
                {
                    bytes bytesTemp;
                    std::map<std::string, std::vector<std::string>> parent2ChildListTemp;
                    hash256Recursive(
                        _s, b, n, _preLen + 1, bytesTemp, bytePath, parent2ChildListTemp);
                    _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
                    parent2ChildList.insert(
                        parent2ChildListTemp.begin(), parent2ChildListTemp.end());
                    nodeType = 3;
                }
                b = n;
            }

            bytes bytesTemp;
            if (_preLen == _begin->first.size())
            {
                bytesTemp.insert(bytesTemp.end(), _begin->second.begin(), _begin->second.end());
                _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
            }
            else
            {
                _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
            }
            bytePath.push_back(bytesTemp);
        }
    }
}

void hash256Recursive(HexMap const& _s, HexMap::const_iterator _begin, HexMap::const_iterator _end,
    unsigned _preLen, bytes& _bytes, std::vector<bytes>& _bytePath,
    std::map<std::string, std::vector<std::string>>& _parent2ChildList)
{
    bytes bytesTemp;
    std::vector<bytes> bytePath;
    std::map<std::string, std::vector<std::string>> parent2ChildList;
    int32_t nodeType = 0;
    hash256TrieTree(_s, _begin, _end, _preLen, bytesTemp, nodeType, bytePath, parent2ChildList);
    TRIEHASH_SESSION_LOG(DEBUG) << " rlp size:" << bytesTemp.size()
                                << " rlpout:" << sha3(bytesTemp).hex()
                                << " tohex:" << toHex(bytesTemp) << " nodetype:" << nodeType
                                << " keypath size:" << bytePath.size();

    if (nodeType == trieHashNodeType::typeIntermediateNode)
    {
        for (auto it = bytePath.begin(); it != bytePath.end(); ++it)
        {
            _parent2ChildList[sha3(bytesTemp).hex()].push_back(toHex(*it));
        }
    }

    if (bytesTemp.size() == 0)
    {
        _bytes.insert(_bytes.end(), bytesTemp.begin(), bytesTemp.end());
    }
    else
    {
        h256 hValue = sha3(bytesTemp);
        _bytes.insert(_bytes.end(), hValue.begin(), hValue.end());
    }
    _bytePath.push_back((sha3(bytesTemp).asBytes()));
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
        hexMap[asNibbles(bytesConstRef(&i->first))] = i->second;

    /*just one element*/
    if (std::next(hexMap.begin()) == hexMap.end())
    {
        bytes bytesChild;
        bytesChild.insert(
            bytesChild.end(), hexMap.begin()->first.begin(), hexMap.begin()->first.end());
        bytesChild.insert(
            bytesChild.end(), hexMap.begin()->second.begin(), hexMap.begin()->second.end());
        dev::h256 hChild = sha3(bytesChild);
        _parent2ChildList[sha3(hChild).hex()].push_back(toHex(hChild));
        TRIEHASH_SESSION_LOG(DEBUG)
            << "child node:" << hChild.hex() << " parent node:" << sha3(hChild).hex();
        bytes bytesParent;
        bytesParent.insert(bytesParent.end(), hChild.begin(), hChild.end());
        return bytesParent;
    }
    std::vector<bytes> bytePath;
    bytes bytesTemp;
    int32_t nodeType = trieHashNodeType::typeDefault;
    hash256TrieTree(hexMap, hexMap.cbegin(), hexMap.cend(), 0, bytesTemp, nodeType, bytePath,
        _parent2ChildList);


    for (auto it = bytePath.begin(); it != bytePath.end(); ++it)
    {
        _parent2ChildList[sha3(bytesTemp).hex()].push_back(toHex(*it));
    }

    for_each(_parent2ChildList.cbegin(), _parent2ChildList.cend(),
        [](const std::pair<std::string, std::vector<std::string>>& val) -> void {
            TRIEHASH_SESSION_LOG(DEBUG) << "parent node:" << val.first;
            const auto& value = val.second;
            for_each(value.cbegin(), value.cend(), [](const std::string& childNode) -> void {
                TRIEHASH_SESSION_LOG(DEBUG) << "child node:" << childNode;
            });
        });
    TRIEHASH_SESSION_LOG(DEBUG) << "total rlpout:" << sha3(bytesTemp).hex()
                                << " tohex:" << toHex(bytesTemp) << " nodetype:" << nodeType
                                << " keypath size:" << bytePath.size();
    return bytesTemp;
}

h256 getHash256(
    BytesMap const& _s, std::map<std::string, std::vector<std::string>>& _parent2ChildList)
{
    return sha3(getTrieTree256(_s, _parent2ChildList));
}

}  // namespace dev
