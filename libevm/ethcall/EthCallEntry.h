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
 * @file: EthCallEntry.h
 * @author: fisco-dev, jimmyshi
 * @date: 2017
 */

#pragma once
//#define ETHCALL_DEBUG

#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <unordered_map>
#include <tuple>
#include <boost/multiprecision/cpp_int.hpp>

#include <libethcore/Common.h>

#include <libevm/VM.h>
#include <libevm/ethcall/index_sequence.h>

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace eth
{

using callid_t = uint32_t;
enum EthCallIdList : callid_t
{
    LOG = 0x10,           ///< Print solidity log in nodes log dir
    PAILLIER = 0x20,      ///< Paillier computing
    DEMO = 0x66666,       ///< demo
    TEST = 0x66667,       ///< test
    TRIE_PROOF = 0x66668, ///< trie transaction proof
    VERIFY_SIGN = 0x66669 ///< verify block signature
#ifdef ETH_GROUPSIG
    ,
    GROUP_SIG = 0x66670, //group sig s
    RING_SIG = 0x66671   //ring sig
#endif
#ifdef ETH_ZKG_VERIFY
    ,
    VERIFY_ZKG = 0x5A4B47 ///< zero knowledge proof verify
#endif
};

/*
 *  EthCall parameter parser
 */
class EthCallParamParser
{
    /*
     *  Parse solidity parameter of ethcall to c++ type(Support memory type only)
     *  Normal types:
     *      solidity  ->    c++
     *      bool            bool
     *      int             根据需要int8~64_t
     *      int8~64         int8~64_t
     *      uint            根据需要int8~64_t
     *      uint8~64        uint8~64_t
     *      byte            byte/char
     *      //int128~256
     *      //uint128~256
     *      //Fixed Point Numbers
     *
     *  Array:(support reference type)
     *      solidity    ->  normal type ->  reference type
     *      string          string          vector_ref<char>
     *      bytes           bytes           vector_ref<byte>
     *      //T[]             vector<T>       vector_ref<T>
     *
     *  //TODO： support more types
     *
    */

  private:
    void parseArrayHeader(u256* sp, char* &data_addr, size_t &size);
  public:
    inline void parse(u256* sp)
    {
        sp++;//Just to avoid: Unused variable warning
        //LOG(TRACE) << "ethcall parse end(sp: " << sp << ")";
        return;
    }

    template<typename T, typename ... Ts>
    void parse(u256* sp, T& p, Ts& ... ps)
    {
        parseUnit(sp, p);
        parse(--sp, ps...);
    }


    #define RegTypeNormal(_Type)                                \
    void parseUnit(u256* sp, _Type& p)                          \
    {                                                           \
        p =  static_cast<_Type>(*sp);                           \
    }     

    //---------Register normal type------------
    RegTypeNormal(bool);
    RegTypeNormal(char);

    //uint
    RegTypeNormal(uint8_t);     RegTypeNormal(uint16_t);
    RegTypeNormal(uint32_t);    RegTypeNormal(uint64_t);
    //RegTypeNormal(uint128_t);   RegTypeNormal(uint256_t);

    //int
    RegTypeNormal(int8_t);      RegTypeNormal(int16_t);
    RegTypeNormal(int32_t);     RegTypeNormal(int64_t);
    //RegTypeNormal(int128_t);    RegTypeNormal(int256_t);

    //---------string type conversion------------
    //--convert to std::string
    void parseUnit(u256* sp, std::string& p);

    //--convert to reference type vector_ref<char>
    void parseUnit(u256* sp, vector_ref<char>& p);

    //---------bytes type conversion------------
    //--convert to bytes
    void parseUnit(u256* sp, bytes& p);

    //--convert to reference type vector_ref<byte>
    void parseUnit(u256* sp, vector_ref<byte>& p);

    //--------------- parse to tuple--------------------
    template<typename ... Ts> //TpType: like tuple<int, string>
    void parseToTuple(u256* sp, std::tuple<Ts...>& tp) //parse param into tuple
    {
        parseToTupleImpl(sp, tp, make_index_sequence<sizeof...(Ts)>());
    }

    template<typename TpType, std::size_t ... I>
    void parseToTupleImpl(u256* sp, TpType& tp, index_sequence<I...>)
    {
        parse(sp, std::get<I>(tp)...);
    }

    void setTargetVm(VM *vm);

  private:
    VM *vm;
};

//ethcall entry
u256 ethcallEntry(VM *vm, u256 *sp, ExtVMFace *ext);

/*
 * Because EthCallExecutor is a tempate of different types T, 
 * we need an EthCallExecutorBase containing an uniform call function: run().
 */
class EthCallExecutorBase
{
  public:
    virtual u256 run(VM *vm, u256 *sp, ExtVMFace *ext = NULL) = 0;
    void updateCostGas(VM *vm, uint64_t gas);
};

//ethCallTable: The map of EthCallExecutor
using ethCallTable_t = unordered_map<callid_t, EthCallExecutorBase *>;
extern ethCallTable_t ethCallTable;

/*
*   EthCallExecutor
*/
template <typename... T>
class EthCallExecutor : public EthCallExecutorBase
{
  public:
    void registerEthcall(enum EthCallIdList callId)
    {
        assert(ethCallTable.end() == ethCallTable.find(callId));

        ethCallTable[callId] = (EthCallExecutorBase *)this;
    }

    u256 run(VM *vm, u256 *sp, ExtVMFace *ext = NULL) override
    {
        EthCallParamParser parser;
        parser.setTargetVm(vm);
        std::tuple<T...> tp; //Use tuple to parse variable number of parameters
        parser.parseToTuple(sp, tp);
        return runImpl(vm, ext, tp, make_index_sequence<sizeof...(T)>());
    }

    template <typename TpType, std::size_t... I>
    u256 runImpl(VM *vm, ExtVMFace *ext, TpType &tp, index_sequence<I...>)
    {
        //expand parameters of tuple and call ethcall()
        this->updateCostGas(vm, gasCostEnv(ext, std::get<I>(tp)...));
        return ethcallEnv(ext, std::get<I>(tp)...);
    }

    /*
    *   Funtion to overwrite：ethcall(), gasCost()
    *   Notice: If compile error happens, add the keyword: override
    */
    virtual u256 ethcallEnv(ExtVMFace *env, T... args)
    {
        return ethcall(args...);
    }

    virtual u256 ethcall(T... args)
    {
        LOG(WARNING) << "Call a null ethcall";
        return 0;
    }

    virtual uint64_t gasCostEnv(ExtVMFace *env, T... args)
    {
        return gasCost(args...);
    }

    virtual uint64_t gasCost(T... args)
    {
        LOG(WARNING) << "Call a null ethcall gasCost";
        return 0;
    }
};

DEV_SIMPLE_EXCEPTION(EthCallNotFound);
}
}
