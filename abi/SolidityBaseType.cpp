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
 * @file: SolidityBaseType.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "SolidityBaseType.h"
#include "SolidityExp.h"
#include <boost/regex.hpp>

namespace libabi
{
	bool  isBoolType(const std::string & strTypeName)
	{
		boost::regex reg("^bool(\\[([0-9]*)\\])*$");
		return boost::regex_match(strTypeName.c_str(), reg);
	}

	bool  isStringType(const std::string & strTypeName)
	{
		boost::regex reg("^string(\\[([0-9]*)\\])*$");
		return boost::regex_match(strTypeName, reg);
	}

	bool isAddrType(const std::string & strTypeName)
	{
		boost::regex reg("^address(\\[([0-9]*)\\])*$");
		return boost::regex_match(strTypeName, reg);
	}

	bool isBytesType(const std::string & strTypeName)
	{
		boost::regex reg("^bytes([0-9]{1,})(\\[([0-9]*)\\])*$");
		return boost::regex_match(strTypeName, reg);
	}

	bool isDBytesType(const std::string & strTypeName)
	{
		boost::regex reg("^bytes(\\[([0-9]*)\\])*$");
		return boost::regex_match(strTypeName, reg);
	}

	bool isIntType(const std::string & strTypeName)
	{
		boost::regex reg("^int([0-9]*)?(\\[([0-9]*)\\])*$");
		return boost::regex_match(strTypeName, reg);
	}

	bool isUintType(const std::string & strTypeName)
	{
		boost::regex reg("^uint([0-9]*)?(\\[([0-9]*)\\])*$");
		return boost::regex_match(strTypeName, reg);
	}

	bool isRealType(const std::string & strTypeName)
	{
		boost::regex reg("^fixed([0-9]*x[0-9]*)?(\\[([0-9]*)\\])*$");
		return boost::regex_match(strTypeName, reg);
	}

	bool isURealType(const std::string & strTypeName)
	{
		boost::regex reg("^ufixed([0-9]*x[0-9]*)?(\\[([0-9]*)\\])*$");
		return boost::regex_match(strTypeName, reg);
	}

	//根据名称获取type的类型，返回值为ENUM_SOLIDITY_TYPE之一
	solidity_type getEnumTypeByName(const std::string &strTypeName)
	{
		solidity_type enumType = ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_UNKOWN;
		if (isBoolType(strTypeName))
		{
			enumType = ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_BOOL;
		}
		else if (isStringType(strTypeName))
		{
			enumType = ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_STRING;
		}
		else if (isAddrType(strTypeName))
		{
			enumType = ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_ADDR;
		}
		else if (isBytesType(strTypeName))
		{
			enumType = ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_BYTES;
		}
		else if (isDBytesType(strTypeName))
		{
			enumType = ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_DBYTES;
		}
		else if (isIntType(strTypeName))
		{
			enumType = ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_INT;
		}
		else if (isUintType(strTypeName))
		{
			enumType = ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_UINT;
		}
		else if (isRealType(strTypeName))
		{
			enumType = ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_REAL;
		}
		else if (isURealType(strTypeName))
		{
			enumType = ENUM_SOLIDITY_TYPE::ENUM_SOLIDITY_TYPE_UREAL;
		}
		else
		{
			//unkown type ,throw exception
			ABI_EXCEPTION_THROW("unrecognized abi type, type => " + strTypeName, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
		}

		return enumType;
	}
}//namespace libabi