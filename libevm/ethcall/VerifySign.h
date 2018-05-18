/*
	This file is part of FISCO BCOS.

	FISCO BCOS is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FISCO BCOS is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FISCO BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file: VerifySign.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include <string>
#include <vector>

#include <libdevcore/easylog.h>
#include <libdevcore/Common.h>

#include <libevm/ethcall/EthCallEntry.h>

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
    //pub public key list, separate by ";"
    //signs signature list, separate by ";"
    u256 ethcall(std::string _hash, std::string _pubs, std::string _signs, std::string _signsIdx);

    uint64_t gasCost(std::string _hash, std::string _pubs, std::string _signs, std::string _signsIdx);
};

}
}
