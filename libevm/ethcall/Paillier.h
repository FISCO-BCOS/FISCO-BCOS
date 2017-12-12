/**
 * @file: Paillier.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include "EthCallEntry.h"
#include <libdevcore/easylog.h>
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
class Paillier : EthCallExecutor<vector_ref<char>, std::string, std::string>
{
public:
    Paillier()
    {
        this->registerEthcall(EthCallIdList::PAILLIER);
    }

    u256 ethcall(vector_ref<char> result, std::string d1, std::string d2);

    uint64_t gasCost(vector_ref<char> result, std::string d1, std::string d2);
};

}
}