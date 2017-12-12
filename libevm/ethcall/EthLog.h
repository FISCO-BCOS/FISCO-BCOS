/**
 * @file: EthLog.h
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

class EthLog : EthCallExecutor<uint32_t, std::string>
{
public:
    EthLog()
    {
        this->registerEthcall(EthCallIdList::LOG);
    }

    u256 ethcall(uint32_t level, std::string str);

    uint64_t gasCost(uint32_t level, std::string str);
};

}
}
