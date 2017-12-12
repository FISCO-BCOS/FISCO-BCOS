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
 * @file: SolidityBaseType.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#ifndef __SOLIDITYBASETYPE_H__
#define __SOLIDITYBASETYPE_H__
#include <string>

namespace libabi
{
	//solidity里面的基础数据定义类型
	typedef enum class ENUM_SOLIDITY_TYPE
	{
		ENUM_SOLIDITY_TYPE_UNKOWN,  // unkown type
		ENUM_SOLIDITY_TYPE_BOOL,    // bool
		ENUM_SOLIDITY_TYPE_INT,     // int8  ~ int256
		ENUM_SOLIDITY_TYPE_UINT,    // uint8 ~ uint256
		ENUM_SOLIDITY_TYPE_ADDR,    // address
		ENUM_SOLIDITY_TYPE_BYTES,   // bytes
		ENUM_SOLIDITY_TYPE_DBYTES,  // dynamic bytes
		ENUM_SOLIDITY_TYPE_REAL,    // real
		ENUM_SOLIDITY_TYPE_UREAL,   // ureal
		ENUM_SOLIDITY_TYPE_STRING,  // string
	}solidity_type;

	//根据名称获取type的类型，返回值为ENUM_SOLIDITY_TYPE之一
	solidity_type getEnumTypeByName(const std::string &strTypeName);
	//类型判断
	bool isBoolType(const std::string &strTypeName);
	bool isStringType(const std::string &strTypeName);
	bool isAddrType(const std::string &strTypeName);
	bool isBytesType(const std::string &strTypeName);
	bool isDBytesType(const std::string &strTypeName);
	bool isIntType(const std::string &strTypeName);
	bool isUintType(const std::string &strTypeName);
	bool isRealType(const std::string &strTypeName);
	bool isURealType(const std::string &strTypeName);
}//namespace libabi

#endif//__SOLIDITYBASETYPE_H__
