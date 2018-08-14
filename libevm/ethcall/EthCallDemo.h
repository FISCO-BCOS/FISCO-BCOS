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
 * @file: EthCallDemo.h
 * @author: fisco-dev, jimmyshi
 * 
 * @date: 2017
 */

#pragma once
#include <string>
#include <vector>

#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>

#include <libevm/ethcall/EthCallEntry.h>

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace eth
{

/*
*   ethcall program example
*   In solidity, call ethcall like:
*       int a;
*       int b;
*       bytes memory bstr;
*       string memory str;
*       uint32 callId = 0x66666;
*       uint r;
*       assembly{
*           r := ethcall(callId, a, b, bstr, str, 0, 0, 0, 0, 0)
*       }
*   So the corresponding EthCallExecutor can be write as follow: (EthCallDemo)
*/

class EthCallDemo : EthCallExecutor<int, int, vector_ref<byte>, string> //Declare the type in the order correspond to ethcall(callId, type0, type1...) in solidity
{
    /*
    *   Program steps:
    *   1. Include EthCallEntry.h in this file.
    *
    *   2. EthCallEntry.cpp include this header file.
    *
    *   3. Declare a CallId in EthCallList of EthCallEntry.h
    *           eg: EthCallIdList::DEMO = 0x66666
    *
    *   4. Add a line like below in EthCallContainer of EthCallEntry.cpp to initialize EthCallDemo.
    *           eg: static EthCallDemo ethCallDemo; 
    *   
    *   5. Follow this Demo, to derive EthCallExecutor and overwrite this two functions: ethcall() and gasCost().
    */    
public:
    EthCallDemo()
    {
        //Register EthCallDemo into ethCallTable using CallId we decare before in EthCallList
        //LOG(DEBUG) << "ethcall bind EthCallDemo--------------------";
        this->registerEthcall(EthCallIdList::DEMO); //EthCallList::DEMO = 0x66666 is EthCallDemo's CallId
    }

    u256 ethcall(int a, int b, vector_ref<byte>bstr, string str) override 
    {
        LOG(TRACE) << "ethcall " <<  a + b << " " << bstr.toString() << str;

        //Using vector_ref instead of vector, can modify the solidity array directly.
        //The usage of vector_ref is similar with vector, ref to libdevcore/vector_ref.h
        bstr[0] = '#';
        bstr[1] = '#';
        bstr[2] = '#';
        bstr[3] = '#';
        bstr[4] = '#';
        bstr[5] = '#';

        return 0; //This return number is the r number of ethcall (r := ethcall(...) )
    }

    uint64_t gasCost(int a, int b, vector_ref<byte>bstr, string str) override 
    {
        return sizeof(a) + sizeof(b) + bstr.size() + str.length(); //Somtimes, gas cost is corresponding to the length of parameter
    }
};

///EthCallDemo example end///


}
}
