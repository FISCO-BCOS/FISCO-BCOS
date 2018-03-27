/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: SolidityCoder.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <cassert>

#include "SolidityCoder.h"
#include "SolidityExp.h"
#include "ContractAbiMgr.h"

#include <libdevcore/Common.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/SHA3.h>

#define UNUSEDPARAMS(a,b,c,d)   do { (void)a;(void)b;(void)c;(void)d; } while (0);  //消除变量未使用的警告

namespace libabi
{
	SolidityCoder::SolidityCoder()
	{
		registerEncoder();
		regisgerDecoder();
	}

	void SolidityCoder::registerEncoder()
	{
		//序列化单个元素的解析器注册
		auto f_unkown_coder = [](const Json::Value &jParam, const std::string &strType) ->std::string {
			UNUSEDPARAMS(jParam, strType, 0, 0);
			ABI_EXCEPTION_THROW("unkown type encoder call, type =>  " + strType + " ,json => " + (jParam.isConvertibleTo(Json::stringValue) ? jParam.asString() : ""), EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
		};

		auto f_bool_coder = [](const Json::Value &jParam, const std::string &strType)->std::string {
			UNUSEDPARAMS(jParam, strType, 0, 0);
			std::string strResult;
			if (jParam.isConvertibleTo(Json::booleanValue))
			{
				strResult = SolidityTools::inputFormaterBool(jParam.asBool());
			}
			else
			{
				ABI_EXCEPTION_THROW("bool encoder, input json node cannot convert to bool.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
			}
			return strResult;
		};

		auto f_int_coder = [](const Json::Value &jParam, const std::string &strType)->std::string {
			UNUSEDPARAMS(jParam, strType, 0, 0);
			std::string strResult;
			if (jParam.isInt64())
			{
				strResult = SolidityTools::inputFormaterInt(jParam.asInt64());
			}
			else if (jParam.isDouble())
			{//浮点型直接转换为整形处理???
				strResult = SolidityTools::inputFormaterInt(jParam.asInt64());
			}
			else if (jParam.isString())
			{//整形时可能会出现大的整形,允许字符串
				strResult = SolidityTools::inputFormaterInt(dev::s256(jParam.asString()));
			}
			else
			{
				ABI_EXCEPTION_THROW("int encoder, input json node cannot convert to int.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
			}
			return strResult;
		};

		auto f_uint_coder = [](const Json::Value &jParam, const std::string &strType)->std::string {
			UNUSEDPARAMS(jParam, strType, 0, 0);
			std::string strResult;
			if (jParam.isUInt64())
			{
				strResult = SolidityTools::inputFormaterUint(jParam.asUInt64());
			}
			else if (jParam.isDouble())
			{
				strResult = SolidityTools::inputFormaterUint(dev::u256(jParam.asUInt64()));
			}
			else if (jParam.isString())
			{
				strResult = SolidityTools::inputFormaterUint(dev::u256(jParam.asString()));
			}
			else
			{
				ABI_EXCEPTION_THROW("int encoder, input json node cannot convert to uint.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
			}
			return strResult;
		};

		auto f_addr_coder = [](const Json::Value &jParam, const std::string &strType)->std::string {
			UNUSEDPARAMS(jParam, strType, 0, 0);
			std::string strResult;
			if (jParam.isString())
			{
				strResult = SolidityTools::inputFormaterAddr(dev::u160(jParam.asString()));
			}
			else
			{
				ABI_EXCEPTION_THROW("address encoder, input json is not string.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
			}
			return strResult;
		};

		auto f_bytes_coder = [](const Json::Value &jParam, const std::string &strType)->std::string {
			UNUSEDPARAMS(jParam, strType, 0, 0);
			std::string strResult;
			if (jParam.isString())
			{
				const std::string &str = jParam.asString();
				strResult = SolidityTools::inputFormaterBytes(str);
			}
			else
			{
				ABI_EXCEPTION_THROW("bytes encoder, input json is not string.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
			}
			return strResult;
		};

		auto f_dbytes_coder = [](const Json::Value &jParam, const std::string &strType)->std::string {
			UNUSEDPARAMS(jParam, strType, 0, 0);
			std::string strResult;
			if (jParam.isString())
			{
				const std::string &str = jParam.asString();
				if (str.empty())
				{
					//先存长度
					strResult += SolidityTools::inputFormaterUint(str.size());
				}
				else
				{
					//先存长度
					strResult += SolidityTools::inputFormaterUint(str.size());
					strResult += SolidityTools::inputFormaterDBytes(str);
				}
			}
			else
			{
				ABI_EXCEPTION_THROW("dynamic bytes encoder, input json is not string.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
			}
			return strResult;
		};

		auto f_real_coder = [](const Json::Value &jParam, const std::string &strType)->std::string {
			UNUSEDPARAMS(jParam, strType, 0, 0);
			ABI_EXCEPTION_THROW("real encoder, this type is not support now.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
			return std::string("");
		};

		auto f_ureal_coder = [](const Json::Value &jParam, const std::string &strType)->std::string {
			UNUSEDPARAMS(jParam, strType, 0, 0);
			ABI_EXCEPTION_THROW("ureal encoder, this type is not support now.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
			return std::string("");
		};

		auto f_string_coder = [](const Json::Value &jParam, const std::string &strType)->std::string {
			UNUSEDPARAMS(jParam, strType, 0, 0);
			std::string strResult;
			if (jParam.isString())
			{
				const std::string &str = jParam.asString();
				//先存长度
				strResult += SolidityTools::inputFormaterUint(str.size());
				strResult += SolidityTools::inputFormaterString(str);
			}
			else
			{
				ABI_EXCEPTION_THROW("string encoder, input json is not string.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
			}
			return strResult;
		};

		//注册
		m_map_encoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_UNKOWN] = f_unkown_coder;
		m_map_encoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_BOOL]   = f_bool_coder;
		m_map_encoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_INT]    = f_int_coder;
		m_map_encoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_UINT]   = f_uint_coder;
		m_map_encoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_ADDR]   = f_addr_coder;
		m_map_encoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_BYTES]  = f_bytes_coder;
		m_map_encoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_DBYTES] = f_dbytes_coder;
		m_map_encoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_REAL]   = f_real_coder;
		m_map_encoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_UREAL]  = f_ureal_coder;
		m_map_encoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_STRING] = f_string_coder;
	}

	void SolidityCoder::regisgerDecoder()
	{
		auto f_unkown_decoder = [](const std::string &strData, std::size_t nOffset, std::size_t nSize, const std::string &strType)->Json::Value {
			UNUSEDPARAMS(strData, nOffset, nSize, strType);
			ABI_EXCEPTION_THROW("unkown type decoder call , type is => " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
		};

		auto f_int_decoder = [](const std::string &strData, std::size_t nOffset, std::size_t nSize, const std::string &strType)->Json::Value {
			UNUSEDPARAMS(strData, nOffset, nSize, strType);
			Json::Value jValue(Json::stringValue);
			jValue = SolidityTools::outputFormaterInt(strData.substr(nOffset, nSize)).str();
			return jValue;
		};

		auto f_uint_decoder = [](const std::string &strData, std::size_t nOffset, std::size_t nSize, const std::string &strType)->Json::Value {
			UNUSEDPARAMS(strData, nOffset, nSize, strType);
			Json::Value jValue(Json::stringValue);
			jValue = SolidityTools::outputFormaterUint(strData.substr(nOffset, nSize)).str();
			return jValue;
		};

		auto f_addr_decoder = [](const std::string &strData, std::size_t nOffset, std::size_t nSize, const std::string &strType)->Json::Value {
			UNUSEDPARAMS(strData, nOffset, nSize, strType);
			Json::Value jValue(Json::stringValue);
			std::string strTemp = "0x" + SolidityTools::padAlignLeft(SolidityTools::outputFormaterAddr(strData.substr(nOffset, nSize)).str(0, std::ios_base::hex),20 );

			//std::string strTemp = "0x" + SolidityTools::outputFormaterAddr(strData.substr(nOffset, nSize)).str(0, std::ios_base::hex);
			std::for_each(strTemp.begin(), strTemp.end(), [](char &ch)->void {
				ch = tolower(ch);
			});
			jValue = strTemp;
			return jValue;
		};

		auto f_bytes_decoder = [](const std::string &strData, std::size_t nOffset, std::size_t nSize, const std::string &strType)->Json::Value {
			UNUSEDPARAMS(strData, nOffset, nSize, strType);
			Json::Value jValue(Json::stringValue);
			auto nBytesWith = SolidityTools::getBytesNWidth(strType);
			jValue = SolidityTools::outputFormaterBytes(strData.substr(nOffset, nSize > (2 * nBytesWith) ? (2 * nBytesWith) : nSize));
			return jValue;
		};

		auto f_dbytes_decoder = [](const std::string &strData, std::size_t nOffset, std::size_t nSize, const std::string &strType)->Json::Value {
			UNUSEDPARAMS(strData, nOffset, nSize, strType);
			Json::Value jValue(Json::stringValue);
			if (nSize > 0)
			{
				jValue = SolidityTools::outputFormaterDBytes(strData.substr(nOffset, nSize));
			}
			else
			{
				jValue = "0x";
			}
			return jValue;
		};

		auto f_bool_decoder = [](const std::string &strData, std::size_t nOffset, std::size_t nSize, const std::string &strType)->Json::Value {
			UNUSEDPARAMS(strData, nOffset, nSize, strType);
			Json::Value jValue(Json::booleanValue);
			jValue = SolidityTools::outputFormaterBool(strData.substr(nOffset, nSize));
			return jValue;
		};

		auto f_real_decoder = [](const std::string &strData, std::size_t nOffset, std::size_t nSize, const std::string &strType)->Json::Value {
			UNUSEDPARAMS(strData, nOffset, nSize, strType);
			ABI_EXCEPTION_THROW("real decoder, this type is not support now.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
			return Json::Value();
		};

		auto f_ureal_decoder = [](const std::string &strData, std::size_t nOffset, std::size_t nSize, const std::string &strType)->Json::Value {
			UNUSEDPARAMS(strData, nOffset, nSize, strType);
			ABI_EXCEPTION_THROW("ureal decoder, this type is not support now.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
			return Json::Value();
		};

		auto f_string_decoder = [](const std::string &strData, std::size_t nOffset, std::size_t nSize, const std::string &strType)->Json::Value {
			UNUSEDPARAMS(strData, nOffset, nSize, strType);
			Json::Value jValue(Json::stringValue);
			if (nSize > 0)
			{
				jValue = SolidityTools::outputFormaterString(strData.substr(nOffset, nSize));
			}
			else
			{
				jValue = "";
			}
			return jValue;
		};

		m_map_decoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_UNKOWN] = f_unkown_decoder;
		m_map_decoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_UINT]   = f_uint_decoder;
		m_map_decoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_INT]    = f_int_decoder;
		m_map_decoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_ADDR]   = f_addr_decoder;
		m_map_decoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_BYTES]  = f_bytes_decoder;
		m_map_decoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_DBYTES] = f_dbytes_decoder;
		m_map_decoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_BOOL]   = f_bool_decoder;
		m_map_decoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_REAL]   = f_real_decoder;
		m_map_decoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_UREAL]  = f_ureal_decoder;
		m_map_decoder[ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_STRING] = f_string_decoder;
	}

	std::string SolidityCoder::encode(const SolidityAbi::Function &f, const std::string &strParams)
	{
		Json::Value root;
		Json::Reader reader;
		if (!reader.parse(strParams, root, false))
		{
			ABI_EXCEPTION_THROW("invalid params json, params=" + strParams , EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiFormat);
		}

		if (!root.isArray())
		{
			ABI_EXCEPTION_THROW("invalid params not an array object, params=" + strParams, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiFormat);
		}

		return encode(f, root);
	}

	//序列化调用信息
	std::string SolidityCoder::encode(const SolidityAbi::Function &f, const Json::Value &jParams)
	{
		//前缀0x
		std::string strData = "0x";
		//函数签名
		strData += dev::toString(dev::sha3(f.getSignature())).substr(0, 8);
		//参数
		strData += encodeParams(f.getAllInputType(), jParams);

		LOG(TRACE) << "[SolidityCoder::encode] end, func=" << f.getSignature()
			<< " ,param=" << jParams.toStyledString()
			<< " ,data=" << strData
			;

		return strData;
	}

	Json::Value SolidityCoder::decode(const SolidityAbi::Function &f, const std::string &strData)
	{
		std::string strFunc = f.getSignature();

		std::string strResultData = strData;
		if ((strResultData.compare(0, 2, "0x") == 0) || (strResultData.compare(0, 2, "0X") == 0))
		{//去掉结果开头的0x
			strResultData.erase(0, 2);
		}

		if (strResultData.empty() && !f.getAllOutputType().empty())
		{
			ABI_EXCEPTION_THROW("decode data empty() ,func|data" + strFunc + "|" + strData , libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiCallInvokeFailed);
		}

		auto jReturn = decodeParams(f.getAllOutputType(), strResultData);

		LOG(TRACE) << "[SolidityCoder::encode] end, func=" << strFunc
			<< " ,result=" << jReturn.toStyledString();

		return jReturn;
	}

	void SolidityCoder::getCNSParams(const SolidityAbi &abi, dev::eth::CnsParams &params, const std::string &data)
	{
		std::string sig;
		std::string da;

		if (data.compare(0, 2, "0x") == 0 || data.compare(0, 2, "0X") == 0)
		{
			sig = "0x" + data.substr(2, 8);
			da = data.substr(10);
		}
		else
		{
			sig = "0x" + data.substr(0, 8);
			da = data.substr(8);
		}

		params.strContractName = abi.getContractName();
		params.strVersion = abi.getVersion();
		const auto &f = abi.getFunctionBySha3Sig(sig);
		params.strFunc = f.strName;

		params.jParams = decodeParams(f.getAllInputType(), da);

		LOG(DEBUG) << "## getCNSParams, contract= " << abi.getContractName()
			<< " ,version= " << abi.getVersion()
			<< " ,address= " << abi.getAddr()
			<< " ,func= " << f.strName
			<< " ,params= " << params.jParams.toStyledString()
			<< " ,data= " << data;

	}

	//根据接口abi信息序列化函数调用信息，返回为序列化之后的数据
	std::string SolidityCoder::encodeParams(const std::vector<std::string> &allInputTypes, const Json::Value &jParams)
	{
		if (allInputTypes.empty())
		{
			return std::string("");
		}

		//输入参数与abi参数不相等
		if (!jParams.isArray() || (jParams.size() != allInputTypes.size()))
		{
			ABI_EXCEPTION_THROW("invalid input, input json element is not the same as abi", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
		}

		std::vector<std::string> allResults;
		//先将各个参数逐个encode
		for (decltype(allInputTypes.size()) index = 0; index < allInputTypes.size(); ++index)
		{
			std::string strResult = SolidityCoder::encodeParam(allInputTypes[index], jParams[(Json::ArrayIndex)index]);
			allResults.push_back(strResult);
		}

		//计算总的偏移量
		std::size_t nTotalOffset = 0;
		for_each(allInputTypes.begin(), allInputTypes.end(), [&nTotalOffset](const std::string &strType) {
			nTotalOffset += SolidityTools::getStaticPartLen(strType);
		});

		return encodeParamsWithOffset(allInputTypes, allResults, nTotalOffset);
	}

	//根据类型序列化对应的数据类型
	std::string SolidityCoder::encodeParam(const std::string &strType, const Json::Value &jParam, bool bFirst)
	{
		solidity_type enumType = getEnumTypeByName(strType);
		if (SolidityTools::isArray(strType))
		{//检查数组是否有效
			SolidityTools::invalidAbiArray(strType);

			//对外接口中的数组不支持动态类型的数组,之前已经有检查,这里不应该出现。
			if (enumType == solidity_type::ENUM_SOLIDITY_TYPE_STRING || enumType == solidity_type::ENUM_SOLIDITY_TYPE_DBYTES)
			{
				ABI_EXCEPTION_THROW("string or dynamic bytes cannot be the type of array, type => " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
			}

			//参数类型必须是数组
			if (!jParam.isArray())
			{
				ABI_EXCEPTION_THROW("invalid input arguments, input is not an array but abi is, type => " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
			}
		}

		std::string strResult;
		if (SolidityTools::isDynamicArray(strType))
		{
			if (bFirst)
			{
				std::string nestType = SolidityTools::nestName(strType);
				//动态数组,先序列化数组长度
				strResult += SolidityTools::inputFormaterUint(jParam.size());
				for (decltype(jParam.size()) index = 0; index < jParam.size(); ++index)
				{
					strResult += encodeParam(nestType, jParam[index], false);
				}
			}
			else
			{
				ABI_EXCEPTION_THROW("invalid abi type, the type of array must be static, type => " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
			}
		}
		else if (SolidityTools::isStaticArray(strType))
		{
			std::string nestType = SolidityTools::nestName(strType);
			std::size_t size = SolidityTools::getStaticArraySize(strType);
			if (size != jParam.size())
			{
				ABI_EXCEPTION_THROW("invalid input arguments, encode static array size is not the same as abi require.", EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
			}

			for (std::size_t index = 0; index < size; ++index)
			{
				strResult += encodeParam(nestType, jParam[(Json::ArrayIndex)index], false);
			}
		}
		else
		{//直接序列化
			strResult += getEncoder(enumType)(jParam, strType);
		}

		return strResult;
	}

	std::string SolidityCoder::encodeParamsWithOffset(const std::vector<std::string> &allTypes, const std::vector<std::string> &allResult, std::size_t nOffset)
	{
		std::string strStaticPart;
		std::string strDynamicPart;

		for (std::size_t index = 0; index < allTypes.size(); ++index)
		{
			if (SolidityTools::isDynamic(allTypes[index]))
			{//动态类型先添加偏移量
				strStaticPart += SolidityTools::inputFormaterUint(dev::u256(nOffset));
				strDynamicPart += allResult[index];

				nOffset += allResult[index].size() / 2;  //注意：这里除2是因为偏移量是按字节来算的,序列化后的是十六进制的字符,两个字符算一位
			}
			else
			{
				strStaticPart += allResult[index];
			}
		}

		return strStaticPart + strDynamicPart;
	}

	//根据接口abi信息反序列化出返回的信息，返回反序列化之后的数据
	Json::Value SolidityCoder::decodeParams(const std::vector<std::string> &allOutputTypes, const std::string &strData)
	{
		if (allOutputTypes.empty())
		{
			Json::Value jNull(Json::nullValue);
			return jNull;
		}

		Json::Value jResult(Json::arrayValue);
		std::size_t nTotalOffset = 0;
		std::for_each(allOutputTypes.begin(), allOutputTypes.end(), [&nTotalOffset, &strData, &jResult, this](const std::string &strType)->void {
			jResult.append(decodeParam(strType, strData, nTotalOffset));
			nTotalOffset += (SolidityTools::getStaticPartLen(strType));
		});

		return jResult;
	}

	//检查abi数据的长度
	void SolidityCoder::checkAbiDataSize(const std::string &strData, std::size_t nOffset, std::size_t nLen, const std::string &strType)
	{
		if (strData.size() < nOffset + nLen)
		{
			LOG(WARNING) << "[SolidityCoder::checkAbiDataSize] invalid decode , type=" << strType
				<< " ,offset=" << nOffset
				<< " ,length=" << nLen
				<< " ,data=" << strData
				<< " ,size=" << strData.size()
				;

			ABI_EXCEPTION_THROW("data size too small .data size => " + std::to_string(strData.size()) + " need size => " + std::to_string(nOffset + nLen), EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiDecodeData);
		}
	}

	//
	Json::Value SolidityCoder::decodeParam(const std::string &strType, const std::string &strData, std::size_t nOffset)
	{
		solidity_type enumType = getEnumTypeByName(strType);

		if (SolidityTools::isArray(strType))
		{
			if (SolidityTools::isDynamic(strType) && !SolidityTools::isArray(strType))
			{
				ABI_EXCEPTION_THROW("dynamic cannot be the type of array, type => " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
			}
		}

		Json::Value jValue;
		if (SolidityTools::isStaticArray(strType))
		{//静态数组
			Json::Value jTemp(Json::arrayValue);
			std::size_t nArraySize = SolidityTools::getStaticArraySize(strType);
			std::string strNestType = SolidityTools::nestName(strType);
			std::size_t nStaticPartLen = SolidityTools::getStaticPartLen(strNestType);
			for (std::size_t index = 0; index < nArraySize; ++index)
			{
				jTemp.append(decodeParam(strNestType, strData, nOffset + nStaticPartLen * index));
			}
			jValue = jTemp;
		}
		else if (SolidityTools::isDynamicArray(strType))
		{//动态数组
			checkAbiDataSize(strData, 2 * nOffset, HEXABIALIGNSIZE, strType);
			//先获取偏移量
			std::size_t nTotalOffset = SolidityTools::outputFormaterUint("0x" + strData.substr(2 * nOffset, HEXABIALIGNSIZE)).convert_to<std::size_t>();
			checkAbiDataSize(strData, 2 * nTotalOffset, HEXABIALIGNSIZE, strType);
			//获取大小
			std::size_t nArraySize = SolidityTools::outputFormaterUint("0x" + strData.substr(2 * nTotalOffset, HEXABIALIGNSIZE)).convert_to<std::size_t>();

			//跳过大小的偏移量
			nTotalOffset += ABIALIGNSIZE;

			Json::Value jTemp(Json::arrayValue);
			std::string strNestType = SolidityTools::nestName(strType);
			std::size_t nStaticPartLen = SolidityTools::getStaticPartLen(strNestType);
			for (std::size_t index = 0; index < nArraySize; ++index)
			{
				jTemp.append(decodeParam(strNestType, strData, nTotalOffset + nStaticPartLen * index));
			}

			jValue = jTemp;
		}
		else if (SolidityTools::isDynamic(strType))
		{//动态类型 string or bytes
		 //先获取偏移量
			checkAbiDataSize(strData, nOffset, HEXABIALIGNSIZE, strType);
			std::size_t nTotalOffset = SolidityTools::outputFormaterUint("0x" + strData.substr(2 * nOffset, HEXABIALIGNSIZE)).convert_to<std::size_t>();
			checkAbiDataSize(strData, 2 * nTotalOffset, HEXABIALIGNSIZE, strType);
			std::size_t nSize = SolidityTools::outputFormaterUint("0x" + strData.substr(2 * nTotalOffset, HEXABIALIGNSIZE)).convert_to<std::size_t>();
			if (nSize > 0)
			{
				checkAbiDataSize(strData, 2 * nTotalOffset + HEXABIALIGNSIZE, HEXABIALIGNSIZE, strType);
			}
			jValue = getDecoder(enumType)(strData, 2 * nTotalOffset + HEXABIALIGNSIZE, 2 * nSize, strType);
		}
		else
		{//基础类型
			checkAbiDataSize(strData, nOffset, HEXABIALIGNSIZE, strType);
			jValue = getDecoder(enumType)(strData, 2 * nOffset, HEXABIALIGNSIZE, strType);
		}

		return jValue;
	}
}//namespace libabi