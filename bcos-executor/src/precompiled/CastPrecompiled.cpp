#include "CastPrecompiled.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework/protocol/Exceptions.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

constexpr const char* const CAST_STR_S256 = "stringToS256(string)" ;
constexpr const char* const CAST_STR_U256 = "stringToU256(string)" ;
constexpr const char* const CAST_STR_ADDR = "stringToAddr(string)" ;
constexpr const char* const CAST_STR_BT32 = "stringToBytes32(string)" ;

constexpr const char* const CAST_S256_STR = "s256ToString(int256)" ;
constexpr const char* const CAST_U256_STR = "u256ToString(uint256)";
constexpr const char* const CAST_ADDR_STR = "addrToString(address)";
constexpr const char* const CAST_BT32_STR = "bytes32ToString(bytes32)";

static std::string setInt(CodecWrapper& codec, bytesConstRef _data)
{
    s256 num;
    codec.decode(_data, num);
    return boost::lexical_cast<std::string>(num);
}

static std::string setUInt(CodecWrapper& codec, bytesConstRef _data)
{
    u256 num;
    codec.decode(_data, num);
    return boost::lexical_cast<std::string>(num);
}

CastPrecompiled::CastPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{    
    name2Selector[CAST_STR_S256] = getFuncSelector(CAST_STR_S256, _hashImpl);
    name2Selector[CAST_STR_U256] = getFuncSelector(CAST_STR_U256, _hashImpl);
    name2Selector[CAST_STR_ADDR] = getFuncSelector(CAST_STR_ADDR, _hashImpl);
    name2Selector[CAST_STR_BT32] = getFuncSelector(CAST_STR_BT32, _hashImpl);

    name2Selector[CAST_S256_STR] = getFuncSelector(CAST_S256_STR, _hashImpl);
    name2Selector[CAST_U256_STR] = getFuncSelector(CAST_U256_STR, _hashImpl);
    name2Selector[CAST_ADDR_STR] = getFuncSelector(CAST_ADDR_STR, _hashImpl);
    name2Selector[CAST_BT32_STR] = getFuncSelector(CAST_BT32_STR, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> CastPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    uint32_t func = getParamFunc(_callParameters->input());
    bytesConstRef data = _callParameters->params();
    if (func == name2Selector[CAST_STR_S256])
    {
        // stringToS256(string)    
        std::string src;
        codec.decode(data, src);
        s256 num = boost::lexical_cast<s256>(src);
        gasPricer->appendOperation(InterfaceOpcode::GetInt);
        _callParameters->setExecResult(codec.encode(num));
    }
    else if (func == name2Selector[CAST_STR_U256])
    {        
        // stringToU256(string) 
        std::string src;
        codec.decode(data, src);
        u256 num = boost::lexical_cast<u256>(src);
        gasPricer->appendOperation(InterfaceOpcode::GetInt);
        _callParameters->setExecResult(codec.encode(num));
    }
    else if (func == name2Selector[CAST_STR_ADDR])
    {
        // stringToAddr(string)
        std::string src;
        codec.decode(data, src);
        Address ret = Address(src);
        gasPricer->appendOperation(InterfaceOpcode::GetAddr);
        _callParameters->setExecResult(codec.encode(ret));
    }
    else if (func == name2Selector[CAST_STR_BT32])
    {        
        // stringToBt32(string)
        std::string src;
        codec.decode(data, src); 
        string32 s32 = bcos::codec::toString32(src);
        gasPricer->appendOperation(InterfaceOpcode::GetByte32);
        _callParameters->setExecResult(codec.encode(s32));
    }
    else if (func == name2Selector[CAST_S256_STR])
    {        
        // s256ToString(int256)
        std::string value(setInt(codec, data));
        gasPricer->appendOperation(InterfaceOpcode::GetString);
        _callParameters->setExecResult(codec.encode(value));

    }
    else if (func == name2Selector[CAST_U256_STR])
    {
        // u256ToString(uint256)
        std::string value(setUInt(codec, data));
        gasPricer->appendOperation(InterfaceOpcode::GetString);
        _callParameters->setExecResult(codec.encode(value));        
    }
    else if (func == name2Selector[CAST_ADDR_STR])
    {        
        // addrToString(address)
        Address src;
        codec.decode(data, src);        
        gasPricer->appendOperation(InterfaceOpcode::GetString);
        _callParameters->setExecResult(codec.encode(src.hex()));
    }
    else if (func == name2Selector[CAST_BT32_STR])
    {
        // bytes32ToString(bytes32)
        string32 src;
        codec.decode(data, src); 
        std::string ret;
        ret.resize(32);
        for(size_t i = 0; i < src.size(); ++i) {
            ret[i] = src[i];
        }
        gasPricer->appendOperation(InterfaceOpcode::GetString);
        _callParameters->setExecResult(codec.encode(ret));        
    }
    else
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("CastPrecompiled")
                              << LOG_DESC("call undefined function!");
        BOOST_THROW_EXCEPTION(PrecompiledError("CastPrecompiled call undefined function!"));
    }
    return _callParameters;
}