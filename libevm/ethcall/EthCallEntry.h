/**
 * @file: EthCallEntry.h
 * @author: fisco-dev
 * 
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
#include <libethcore/Common.h>
#include <boost/multiprecision/cpp_int.hpp>
#include "../VM.h"
#include "index_sequence.h"

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace eth
{

using callid_t = uint32_t;
enum EthCallIdList:callid_t
{
    LOG         = 0x10,         ///< 打log
    PAILLIER    = 0x20,         ///< 同态加密
    DEMO        = 0x66666,      ///< demo
    TEST        = 0x66667,       ///< test
    TRIE_PROOF        = 0x66668,       ///< trie的交易证明
    VERIFY_SIGN        = 0x66669       ///< block的验证签名
};

/*
 *  EthCall 参数翻译器
 */
class EthCallParamParser
{
    /*
     *  将solidity的EthCall调用参数转换成c++参数类型（目前只支持memory类型）
     *  基本类型:
     *      sol类型   ->    c++
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
     *  数组:（支持引用）
     *      sol类型    ->     一般类型        引用类型
     *      string          string          vector_ref<char>
     *      bytes           bytes           vector_ref<byte>
     *      //T[]             vector<T>       vector_ref<T>
     *
     *  //TODO： 支持更多类型
     *
    */

private:
    void parseArrayHeader(u256* sp, char* &data_addr, size_t &size);


public:
    inline void parse(u256* sp);
    template<typename T, typename ... Ts>
    void parse(u256* sp, T& p, Ts& ... ps);


    //基本类型注册，仅仅只是简单进行static_cast
    #define T_RegTypeNormal(_Type)                              \
    void parseUnit(u256* sp, _Type& p);


    //---------注册基本类型------------
    T_RegTypeNormal(bool);
    T_RegTypeNormal(char);

    //uint
    T_RegTypeNormal(uint8_t);       T_RegTypeNormal(uint16_t);
    T_RegTypeNormal(uint32_t);      T_RegTypeNormal(uint64_t);
    //T_RegTypeNormal(uint128_t);     T_RegTypeNormal(uint256_t);

    //int
    T_RegTypeNormal(int8_t);        T_RegTypeNormal(int16_t);
    T_RegTypeNormal(int32_t);       T_RegTypeNormal(int64_t);
    //T_RegTypeNormal(int128_t);      T_RegTypeNormal(int256_t);

    //---------string类型转换------------
    //--换成一般类型std::string
    void parseUnit(u256* sp, std::string& p);

    //--转换为引用类型:vector_ref<char>
    void parseUnit(u256* sp, vector_ref<char>& p);

    //---------bytes类型转换------------
    //--转换为一般类型:bytes
    void parseUnit(u256* sp, bytes& p);

    //--转换为引用类型:vector_ref<byte>
    void parseUnit(u256* sp, vector_ref<byte>& p);

    //--------------- parse 成 tuple 类型--------------------
    template<typename ... Ts> //TpType: like tuple<int, string>
    void parseToTuple(u256* sp, std::tuple<Ts...>& tp);

    template<typename TpType, std::size_t ... I>
    void parseToTupleImpl(u256* sp, TpType& tp, index_sequence<I...>);

    void setTargetVm(VM *vm);

private:
    VM *vm;
};

//ethcall总入口
u256 ethcallEntry(VM *vm, u256* sp);

//因为EthCallExecutor的T是不同的类型，所以写一个EthCallExecutor的父类，提供统一类型的run接口
class EthCallExecutorBase
{
public:
    virtual u256 run(VM *vm, u256* sp) = 0;
    void updateCostGas(VM *vm, uint64_t gas);

};

//ethCallTable:各个EthCallExecutor的映射表
using ethCallTable_t = unordered_map<callid_t, EthCallExecutorBase*>;
extern ethCallTable_t ethCallTable;

/*
*   EthCallExecutor
*/
template<typename ... T>
class EthCallExecutor : EthCallExecutorBase
{
public:
    void registerEthcall(enum EthCallIdList callId)
    {
        assert(ethCallTable.end() == ethCallTable.find(callId));

        ethCallTable[callId] = (EthCallExecutorBase*)this;
    }

    u256 run(VM *vm, u256* sp) override
    {
        EthCallParamParser parser;
        parser.setTargetVm(vm);
        std::tuple<T...> tp;//通过tuple的方式实现可变长的参数，能够对参数parse并向func传参
        parser.parseToTuple(sp, tp);
        return runImpl(vm, tp, make_index_sequence<sizeof...(T)>());
    }

    template<typename TpType, std::size_t ... I>
    u256 runImpl(VM *vm, TpType& tp, index_sequence<I...>)
    {
        //自动把tuple中的参数展开，传入ethcall中
        this->updateCostGas(vm, gasCost(std::get<I>(tp)...));
        return ethcall(std::get<I>(tp)...);
    }

    /*
    *   编程接口：ethcall、 gasCost
    *   注意：覆盖函数时，若编译报错，请加上override关键字
    */
    virtual u256 ethcall(T...args) = 0;
    virtual uint64_t gasCost(T ...args) = 0;

};

DEV_SIMPLE_EXCEPTION(EthCallNotFound);

}
}

