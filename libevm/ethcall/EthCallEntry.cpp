/**
 * @file: EthCallEntry.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "EthCallEntry.h"
#include "EthLog.h"
#include "EthCallDemo.h"
#include "Paillier.h"
#include "TrieProof.h"
#include "VerifySign.h"
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

class EthCallContainer
{
public:
    //在此处实例化EthCall的各种功能
    EthLog ethLog;
    Paillier ethPaillier;
    EthCallDemo ethCallDemo;
    TrieProof trieProof;
    VerifySign verifySign;
};

/*
 *   EthCallParamParser
 */
void EthCallParamParser::parseArrayHeader(u256* sp, char* &data_addr, size_t &size)
{
    /*
     *      m_sp指向一个偏移量（offset), m_mem + offset为数组结构位置
     *      数组地址： m_mem + *m_sp
     *                      |
     *      数组结构：       [       size      ][元素0, 元素1, 元素2... ]
     *          大小：       ( h256 = 32 bytes )(  T  )(  T  )(  T  )(  T  )(  T  )
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


inline void EthCallParamParser::parse(u256* sp)
{
    sp++;//Just to avoid: Unused variable warning
    //LOG(TRACE) << "ethcall parse end(sp: " << sp << ")";
    return;
}

template<typename T, typename ... Ts>
void EthCallParamParser::parse(u256* sp, T& p, Ts& ... ps)
{
    parseUnit(sp, p);
    parse(--sp, ps...);
}


//基本类型注册，仅仅只是简单进行static_cast
#define RegTypeNormal(_Type)                                \
void EthCallParamParser::parseUnit(u256* sp, _Type& p)      \
{                                                           \
    p =  static_cast<_Type>(*sp);                           \
}                                                           \


//---------注册基本类型------------
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


//---------string类型转换------------
//--转换成一般类型std::string
void EthCallParamParser::parseUnit(u256* sp, std::string& p)
{
    char* data_addr;
    size_t size;
    parseArrayHeader(sp, data_addr, size);
    vm->updateNeedMen(*sp, size * sizeof(char) + sizeof(h256));
    p = string(data_addr, size);
}

//--转换为引用类型:vector_ref<char>
void EthCallParamParser::parseUnit(u256* sp, vector_ref<char>& p)
{
    char* data_addr;
    size_t size;
    parseArrayHeader(sp, data_addr, size);
    vm->updateNeedMen(*sp, size * sizeof(char) + sizeof(h256));
    p.retarget((char*)data_addr, size);
}

//---------bytes类型转换------------
//--转换为一般类型:bytes
void EthCallParamParser::parseUnit(u256* sp, bytes& p)
{
    char* data_addr;
    size_t size;
    parseArrayHeader(sp, data_addr, size);
    vm->updateNeedMen(*sp, size * sizeof(byte) + sizeof(h256));
    p = bytes((byte*)data_addr, (byte*)(data_addr + size));
}

//--转换为引用类型:vector_ref<byte>
void EthCallParamParser::parseUnit(u256* sp, vector_ref<byte>& p)
{
    char* data_addr;
    size_t size;
    parseArrayHeader(sp, data_addr, size);
    vm->updateNeedMen(*sp, size * sizeof(byte) + sizeof(h256));
    p.retarget((byte*)data_addr, size);
}

//--------------- parse 成 tuple 类型--------------------
template<typename ... Ts> //TpType: like tuple<int, string>
void EthCallParamParser::parseToTuple(u256* sp, std::tuple<Ts...>& tp) //parse param into tuple
{
    parseToTupleImpl(sp, tp, make_index_sequence<sizeof...(Ts)>());
}

template<typename TpType, std::size_t ... I>
void EthCallParamParser::parseToTupleImpl(u256* sp, TpType& tp, index_sequence<I...>)
{
    parse(sp, std::get<I>(tp)...);
}

void EthCallParamParser::setTargetVm(VM *vm)
{
    this->vm = vm;
}




/*
 *   ethcall总入口
 */
//ethCallTable:各个EthCallExecutor的映射表
ethCallTable_t ethCallTable;
EthCallContainer ethCallContainer = EthCallContainer();

u256 ethcallEntry(VM *vm, u256* sp)
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
        BOOST_THROW_EXCEPTION(EthCallNotFound());//Throw exception if callId is not found
    }

    return it->second->run(vm, sp);
}


void EthCallExecutorBase::updateCostGas(VM *vm, uint64_t gas)
{
    vm->updateCostGas(gas);
}


}
}
