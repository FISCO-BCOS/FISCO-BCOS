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
 * @file: EthCallEntry.cpp
 * @author: fisco-dev, jimmyshi
 * 
 * @date: 2017
 */
#include <string>
#include <vector>

#include <libdevcore/easylog.h>
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>

//for compile control:
//1. off: compile without group sig and ring sig
//2. on: compile with group sig and ring sig
#ifdef ETH_GROUPSIG
#include <libevm/ethcall/EthcallGroupSig.h>
#include <libevm/ethcall/EthcallRingSig.h>
#endif

#ifdef ETH_ZKG_VERIFY
#include <libevm/ethcall/VerifyZkg.h>
#endif

#include <libevm/ethcall/EthCallEntry.h>
#include <libevm/ethcall/EthCallDemo.h>
#include <libevm/ethcall/EthLog.h>
#include <libevm/ethcall/Paillier.h>
#include <libevm/ethcall/TrieProof.h>
#include <libevm/ethcall/VerifySign.h>

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace eth
{

class EthCallContainer
{
public:
    EthLog ethLog;
    Paillier ethPaillier;
    EthCallDemo ethCallDemo;
    TrieProof trieProof;
    VerifySign verifySign;
#ifdef ETH_GROUPSIG
    EthcallGroupSig group_sig_ethcall;
    EthcallRingSig ring_sig_ethcall;
#endif

#ifdef ETH_ZKG_VERIFY
    VerifyZkg verifyZkg;
#endif
};

/*
 *   EthCallParamParser
 */
void EthCallParamParser::parseArrayHeader(u256* sp, char* &data_addr, size_t &size)
{
    /*
     *      m_sp points to an offset of the parameter, m_mem + offset locates the array's start address
     *      Array start address： m_mem + *m_sp
     *                      |
     *      Array memory    [       size      ][element0, element1, element2... ]
     *          size:       ( h256 = 32 bytes )(  T  )(  T  )(  T  )(  T  )(  T  )
    */

    if(NULL == vm)
    {
        LOG(ERROR) << "EthCallParamParser.vm == NULL. Set target vm before hand.";
        return;
    }
    vm->updateNeedMen(*sp, sizeof(h256));
    char* size_addr = vm->getDataAddr(*sp);
    data_addr = size_addr + sizeof(h256);
    size = size_t((u256)*(h256 const*)size_addr);
}


//---------string type conversion------------
//--convert to std::string
void EthCallParamParser::parseUnit(u256* sp, std::string& p)
{
    char* data_addr;
    size_t size;
    parseArrayHeader(sp, data_addr, size);
    vm->updateNeedMen(*sp, size * sizeof(char) + sizeof(h256));
    p = string(data_addr, size);
}

//--convert to reference type vector_ref<char>
void EthCallParamParser::parseUnit(u256* sp, vector_ref<char>& p)
{
    char* data_addr;
    size_t size;
    parseArrayHeader(sp, data_addr, size);
    vm->updateNeedMen(*sp, size * sizeof(char) + sizeof(h256));
    p.retarget((char*)data_addr, size);
}

//---------bytes type conversion------------
//--convert to bytes
void EthCallParamParser::parseUnit(u256* sp, bytes& p)
{
    char* data_addr;
    size_t size;
    parseArrayHeader(sp, data_addr, size);
    vm->updateNeedMen(*sp, size * sizeof(byte) + sizeof(h256));
    p = bytes((byte*)data_addr, (byte*)(data_addr + size));
}

//--convert to reference type vector_ref<byte>
void EthCallParamParser::parseUnit(u256* sp, vector_ref<byte>& p)
{
    char* data_addr;
    size_t size;
    parseArrayHeader(sp, data_addr, size);
    vm->updateNeedMen(*sp, size * sizeof(byte) + sizeof(h256));
    p.retarget((byte*)data_addr, size);
}


void EthCallParamParser::setTargetVm(VM *vm)
{
    this->vm = vm;
}

/*
 *   ethcall entry
 */
//ethCallTable: the map of EthCallExecutor
ethCallTable_t ethCallTable;
EthCallContainer ethCallContainer = EthCallContainer();

u256 ethcallEntry(VM *vm, u256* sp, ExtVMFace* ext = NULL)
{
    EthCallParamParser parser;
    parser.setTargetVm(vm);

    callid_t callId;
    parser.parse(sp, callId);
    --sp;

    ethCallTable_t::iterator it = ethCallTable.find(callId);
    if(ethCallTable.end() == it)
    {
        LOG(ERROR) << "EthCall id " << callId << " not found";
        //BOOST_THROW_EXCEPTION(EthCallNotFound());//Throw exception if callId is not found
	    BOOST_THROW_EXCEPTION(EthCallIdNotFound());
    }

    return it->second->run(vm, sp, ext);
}


void EthCallExecutorBase::updateCostGas(VM *vm, uint64_t gas)
{
    vm->updateCostGas(gas);
}
}
}
