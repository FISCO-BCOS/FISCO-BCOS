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
 * @file: SolidityAbi.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <iostream>
#include <cassert>
#include <algorithm>
#include "SolidityTools.h"
#include "SolidityAbi.h"
#include "SolidityExp.h"
#include "SolidityBaseType.h"
#include <libdevcore/easylog.h>
#include <libdevcore/Exceptions.h>
#include <libethcore/Exceptions.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/SHA3.h>

using namespace dev;
using namespace dev::eth;

namespace libabi
{
	SolidityAbi::SolidityAbi(const std::string &strContractName, const std::string &strVer, const std::string &strAddr, const std::string & strAbi, dev::u256 uBlockNumber, dev::u256 uTimeStamp)
	{
		reset(strContractName, strVer, strAddr, strAbi, uBlockNumber, uTimeStamp);
	}
	
	SolidityAbi::SolidityAbi(const dev::bytes &_rlp)
	{
		RLP rlp(_rlp);
		int field = 0;
		try
		{
			if (!rlp.isList())
				BOOST_THROW_EXCEPTION(InvalidTransactionFormat() << errinfo_comment("[SolidityAbi::SolidityAbi] transaction RLP must be a list"));

			if (rlp.itemCount() != 6)
			{
				LOG(WARNING) << "[SolidityAbi::SolidityAbi] invalid fields in the tracsaction RLP ,size=" << rlp.itemCount();

				BOOST_THROW_EXCEPTION(InvalidTransactionFormat() << errinfo_comment("invalid fields in the transaction RLP"));
			}

			//_s << m_strContractName << m_strVersion << m_strContractAddr << m_strAbi << m_uBlockNumber << m_uTimeStamp;

			int index = 0;

			std::string strContractName = rlp[field = (index++)].toString();
			std::string strVersion      = rlp[field = (index++)].toString();
			std::string strContractAddr = rlp[field = (index++)].toString();
			std::string strAbi          = rlp[field = (index++)].toString();
			dev::u256   uBlockNumber    = rlp[field = (index++)].toInt<dev::u256>();
			dev::u256   uTimeStamp      = rlp[field = (index++)].toInt<dev::u256>();

			reset(strContractName, strVersion, strContractAddr, strAbi, uBlockNumber, uTimeStamp);
		}
		catch (Exception& _e)
		{
			_e << dev::eth::errinfo_name("invalid name entire abi format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
			throw;
		}
	}

	void SolidityAbi::streamRLP(dev::RLPStream& _s) const
	{
		_s.appendList(6);
		_s << m_strContractName << m_strVersion << m_strContractAddr << m_strAbi << m_uBlockNumber << m_uTimeStamp;
	}

	void SolidityAbi::reset(const std::string &strContractName, const std::string &strVer, const std::string &strAddr, const std::string & strAbi, dev::u256 uBlockNumber, dev::u256 uTimeStamp)
	{
		//clear all before infomation
		clear();

		Json::Value root;
		Json::Reader reader;
		if (!reader.parse(strAbi, root, false))
		{
			LOG(WARNING) << "[SolidityAbi::reset] invalid abi json format, json parser failed"
				<< " ,name=" << strContractName
				<< " ,version=" << strVer
				<< " ,addr=" << strAddr
				<< " ,abi=" << strAbi;

			ABI_EXCEPTION_THROW("invalid abi json,not invalid json format, contract|version|address=" + strContractName + "|" + strVer + "|" + strAddr, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiFormat);

			return;
		}

		if (!root.isArray())
		{
			LOG(WARNING) << "[SolidityAbi::reset] invalid abi json format, not an array object"
				<< " ,name=" << strContractName
				<< " ,version=" << strVer
				<< " ,addr=" << strAddr
				<< " ,abi=" << strAbi;

			ABI_EXCEPTION_THROW("invalid abi json,not array object, contract|version|address=" + strContractName + "|" + strVer + "|" + strAddr, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiFormat);

			return;
		}


		for (std::size_t index = 0; index < root.size(); ++index)
		{
			Json::Value jValue = root[(Json::ArrayIndex)index];
			if (jValue.isMember("type") && jValue["type"].isString())
			{
				std::string strType = jValue["type"].asString();
				//different
				if ("constructor" == strType)
				{
					Constructor cf;
					parserConstruct(jValue, cf);
					addOFunction(cf);
				}
				else if ("function" == strType)
				{
					Function f;
					parserFunction(jValue, f);
					f.setSha3Signature("0x" + dev::toString(dev::sha3(f.getSignature())).substr(0, 8));
					addOFunction(f);

					LOG(DEBUG) << "add one function ,name=" << f.strName
						<< " ,isconst=" << f.bConstant()
						;
				}
				else if ("event" == strType)
				{
					Event e;
					parserEvent(jValue, e);
					addOEvent(e);
				}
			}
			else
			{
				LOG(WARNING) << "[SolidityAbi::reset] invalid abi, have not \"type\" node"
					<< " ,name=" << strContractName
					<< " ,version=" << strVer
					<< " ,addr=" << strAddr
					<< " ,abi=" << strAbi;

				ABI_EXCEPTION_THROW("invalid abi ,have not \"type\" node, contract|version|address=" + strContractName + "|" + strVer + "|" + strAddr, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiFormat);
			}
		}

		m_strContractName = strContractName;
		m_strVersion      = strVer;
		m_strContractAddr = strAddr;
		m_strAbi          = strAbi;
		m_uBlockNumber    = uBlockNumber;
		m_uTimeStamp      = uTimeStamp;

		LOG(DEBUG) << "[SolidityAbi::reset] name=" << strContractName
			<< " ,version" << strVer
			<< " ,name=" << strContractName
			<< " ,addr=" << strAddr
			<< " ,blocknumber=" << uBlockNumber.str()
			<< " ,timestamp=" << uTimeStamp.str()
			<< " ,func=" << m_allFunc.size()
			<< " ,event=" << m_allEvent.size();
	}

	//清理所有的信息
	void SolidityAbi::clear()
	{
		m_strContractName.clear();
		m_strVersion.clear();
		m_strContractAddr.clear();
		m_strAbi.clear();
		m_allFunc.clear();
		m_allEvent.clear();
	}

	//从json abi中解析函数 事件  构造函数信息
	void SolidityAbi::parserFunction(const Json::Value & jValue, Function & func)
	{
		if (jValue.isMember("constant") && jValue["constant"].isBool())
		{
			func.isConstant = jValue["constant"].asBool();
		}

		if (jValue.isMember("name") && jValue["name"].isString())
		{
			func.strName = jValue["name"].asString();
		}

		if (jValue.isMember("payable") && jValue["payable"].isBool())
		{
			func.isPayable = jValue["payable"].asBool();
		}

		if (jValue.isMember("type") && jValue["type"].isString())
		{
			func.strType = jValue["type"].asString();
		}

		if (jValue.isMember("inputs") && jValue["inputs"].isArray())
		{
			parserFunctionInput(jValue["inputs"], func.allInputs);
		}

		if (jValue.isMember("outputs") && jValue["outputs"].isArray())
		{
			parserFunctionOutput(jValue["outputs"], func.allOutputs);
		}
	}

	void SolidityAbi::parserEvent(const Json::Value & jValue, Event & event)
	{
		if (jValue.isMember("anonymous") && jValue["anonymous"].isBool())
		{
			event.isAnonymous = jValue["anonymous"].asBool();
		}

		if (jValue.isMember("name") && jValue["name"].isString())
		{
			event.strName = jValue["name"].asString();
		}

		if (jValue.isMember("type") && jValue["type"].isString())
		{
			event.strType = jValue["type"].asString();
		}

		if (jValue.isMember("inputs") && jValue["inputs"].isArray())
		{
			parserEventInput(jValue["inputs"], event.allInputs);
		}
	}

	void SolidityAbi::parserConstruct(const Json::Value & jValue, Function & func)
	{
		parserFunction(jValue, func);
	}

	void SolidityAbi::parserEventInput(const Json::Value & jValue, std::vector<Event::Input>& inputs)
	{
		assert(jValue.isArray());
		for (std::size_t index = 0; index < jValue.size(); ++index)
		{
			Event::Input i;

			const Json::Value &jNode = jValue[(Json::ArrayIndex)index];

			if (jNode.isMember("indexed") && jNode["indexed"].isBool())
			{
				i.isIndexed = jNode["indexed"].asBool();
			}

			if (jNode.isMember("name") && jNode["name"].isString())
			{
				i.strName = jNode["name"].asString();
			}

			if (jNode.isMember("type") && jNode["type"].isString())
			{
				i.strType = jNode["type"].asString();
			}

			inputs.push_back(i);
		}
	}

	void SolidityAbi::parserFunctionInput(const Json::Value & jValue, std::vector<Function::Input> & inputs)
	{
		for (std::size_t index = 0; index < jValue.size(); ++index)
		{
			Function::Input i;

			const Json::Value &jNode = jValue[(Json::ArrayIndex)index];
			if (jNode.isMember("name") && jNode["name"].isString())
			{
				i.strName = jNode["name"].asString();
			}

			if (jNode.isMember("type") && jNode["type"].isString())
			{
				i.strType = jNode["type"].asString();

				//检查下类型是否是正常的。
				invalidAbiInputOrOutput(i.strType);
			}

			inputs.push_back(i);
		}
	}

	void SolidityAbi::parserFunctionOutput(const Json::Value & jValue, std::vector<Function::Input> & outputs)
	{
		return parserFunctionInput(jValue, outputs);
	}

	//检查是否是一个正常的abi的输入或者是输入类型
	void SolidityAbi::invalidAbiInputOrOutput(const std::string & strType)
	{
		auto enumType = getEnumTypeByName(strType);

		SolidityTools::invalidAbiArray(strType);

		switch (enumType)
		{
		case ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_BOOL:
		case ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_INT:
		case ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_UINT:
		case ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_ADDR:
		case ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_REAL:
		case ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_UREAL:
		case ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_BYTES:
		{//check if type is valid array type
			break;
		}
		case ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_DBYTES:
		case ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_STRING:
		{//string and bytes already dynamic type and not allow be the member of array
			if (SolidityTools::isArray(strType))
			{
				ABI_EXCEPTION_THROW("string or bytes not allow to be member of array, type=" + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
			}
			break;
		}
		default:
		{//unkown type
			ABI_EXCEPTION_THROW("unrecognized abi type, type=" + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
			break;
		}
		}

		return;
	}

	const SolidityAbi::Function &SolidityAbi::getFunctionByFuncName(const std::string &strFuncName) const
	{
		auto iter = std::find_if(m_allFunc.begin(), m_allFunc.end(), [&strFuncName](const Function &f)->bool {
			return strFuncName == f.strName;
		});

		if (iter == m_allFunc.end())
		{
			//不存在，直接抛异常
			ABI_EXCEPTION_THROW("abi function not find, contract|version|strFunc|addr=" + m_strContractName + "|" + m_strVersion + "|" + strFuncName + "|" + m_strContractAddr, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeFunctionNotExist);
		}

		return *iter;
	}

	const SolidityAbi::Function& SolidityAbi::getFunctionBySha3Sig(const std::string &strSha3Sig) const
	{
		auto iter = std::find_if(m_allFunc.begin(), m_allFunc.end(), [&strSha3Sig](const Function &f)->bool {
			return strSha3Sig == f.getSha3Signature();
		});

		if (iter == m_allFunc.end())
		{
			//不存在，直接抛异常
			ABI_EXCEPTION_THROW("abi function not find, contract|version|strSha3Sig|addr=" + m_strContractName + "|" + m_strVersion + "|" + strSha3Sig + "|" + m_strContractAddr, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeFunctionNotExist);
		}

		return *iter;
	}

	//从函数的完整签名构建一个函数对象
	SolidityAbi::Function SolidityAbi::constructFromFuncSig(const std::string &strFuncSig) const
	{
		Function f;
		auto i0 = strFuncSig.find("(");
		auto i1 = strFuncSig.find(")");
		if ((i0 != std::string::npos) && (i1 != std::string::npos) && (i1 > i0))
		{
			//get(int,string,int,int[])
			std::string strFuncName = strFuncSig.substr(0, i0);
			std::string strParams = strFuncSig.substr(i0 + 1, i1 - i0 -1);
			auto allParams = libabi::SolidityTools::splitString(strParams, ",");
			f.strName = el::base::utils::Str::trim(strFuncName);
			for (auto &s: allParams)
			{
				Function::Input i;
				i.strType = el::base::utils::Str::trim(s);
				f.allInputs.push_back(i);
			}

			f.setSha3Signature("0x" + dev::toString(dev::sha3(f.getSignature())).substr(0, 8));
		}

		return f;
	}

	const SolidityAbi::Function& SolidityAbi::getFunction(const std::string &strFunc) const
	{
		//判断传入的是否是函数签名
		auto isFuncSignature = [](const std::string &name) -> bool {
			auto i0 = name.find("(");
			auto i1 = name.find(")");
			return ((i0 != std::string::npos) && (i1 != std::string::npos) && (i1 > i0));
		}(strFunc);
		
		if (isFuncSignature)
		{
			auto f = constructFromFuncSig(strFunc);
			return getFunctionBySha3Sig(f.getSha3Signature());
		}
		else
		{
			return getFunctionByFuncName(strFunc);
		}
	}

	std::string SolidityAbi::getFunctionSignature(const std::string & strName)
	{
		auto iter = std::find_if(m_allFunc.begin(), m_allFunc.end(), [&strName](const Function &f)->bool {
			return strName == f.strName;
		});

		if (iter == m_allFunc.end())
		{
			//不存在，直接抛异常
			ABI_EXCEPTION_THROW("abi function not find, contract|version|func|addr=" + m_strContractName + "|" + m_strVersion + "|" + strName + "|" + m_strContractAddr, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeFunctionNotExist);
		}

		return iter->getSignature();
	}

	std::vector<std::string> SolidityAbi::getFunctionInputTypes(const std::string & strName)
	{
		auto iter = std::find_if(m_allFunc.begin(), m_allFunc.end(), [&strName](const Function &f)->bool {
			return strName == f.strName;
		});

		if (iter == m_allFunc.end())
		{
			//不存在，直接抛异常
			ABI_EXCEPTION_THROW("abi input function not exist, contract|version|name=" + m_strContractName + "|" + m_strVersion + "|" + strName, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeFunctionNotExist);
		}

		return iter->getAllInputType();
	}

	std::vector<std::string> SolidityAbi::getFunctionOutputTypes(const std::string & strName)
	{
		auto iter = std::find_if(m_allFunc.begin(), m_allFunc.end(), [&strName](const Function &f)->bool {
			return strName == f.strName;
		});

		if (iter == m_allFunc.end())
		{
			//不存在，直接抛异常
			ABI_EXCEPTION_THROW("abi output function not exist, contract|version|name=" + m_strContractName + "|" + m_strVersion + "|" +strName, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeFunctionNotExist);
		}

		return iter->getAllOutputType();;
	}
}//namespace libabi