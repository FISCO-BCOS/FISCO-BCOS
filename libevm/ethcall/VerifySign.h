/**
 * @file: VerifySign.h
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
class VerifySign : EthCallExecutor<std::string, std::string, std::string, std::string>
{
public:
    VerifySign()
    {
        this->registerEthcall(EthCallIdList::VERIFY_SIGN);
    }

    //hash block的hash
    //pub 公钥列表,";"分隔
    //signs 签名列表,";"分隔
    u256 ethcall(std::string _hash, std::string _pubs, std::string _signs, std::string _signsIdx);

    uint64_t gasCost(std::string _hash, std::string _pubs, std::string _signs, std::string _signsIdx);
};

}
}
