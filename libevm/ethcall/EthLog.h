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
 * @file: EthLog.h
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
