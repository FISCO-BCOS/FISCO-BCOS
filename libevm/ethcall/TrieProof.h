/**
 * @file: TrieProof.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include "EthCallEntry.h"
#include <libdevcore/Common.h>
#include <string>
#include <vector>

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace eth
{
class TrieProof : EthCallExecutor<std::string,std::string, std::string, std::string>
{
public:
    TrieProof()
    {
        this->registerEthcall(EthCallIdList::TRIE_PROOF);
    }

    //_root trie的root hash
    //_proofs 证明列表，hex encode后用";"拼接的字符串,
    //_targetValue 交易的数据 hex encode
    u256 ethcall(std::string _root, std::string _proofs, std::string _key, std::string _targetValue);

    uint64_t gasCost(std::string _root, std::string _proofs, std::string _key, std::string _targetValue);
};

}
}
