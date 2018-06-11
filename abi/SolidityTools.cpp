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
 * @file: SolidityTools.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <stdlib.h>
#include <assert.h>
#include <iostream>

#include "SolidityTools.h"

#include "SolidityExp.h"
#include "SolidityBaseType.h"

#include <boost/regex.hpp>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>

namespace libabi
{
	const std::string SolidityTools::CNS_SPLIT_STRING = "@";

	std::vector<std::string> SolidityTools::splitString(const std::string &str, const std::string &splitString)
	{
		std::vector<std::string> splitVecString;
		boost::split(splitVecString, str, boost::is_any_of(splitString));
		return splitVecString;
	}

	std::string SolidityTools::nestName(const std::string & strType)
	{
		auto r = nestTypes(strType);
		return r.empty() ? strType : (strType.substr(0, strType.size() - r[r.size() - 1].size()));
	}

	std::vector<std::string> SolidityTools::nestTypes(const std::string & strTypeName)
	{
		std::vector<std::string> result;
		boost::regex regex("\\[[0-9]*\\]");
		boost::sregex_token_iterator iter(strTypeName.begin(), strTypeName.end(), regex, 0);
		boost::sregex_token_iterator end;

		for (; iter != end; ++iter)
		{
			result.push_back(*iter);
		}

		return result;
	}

	//是否是动态类型
	bool SolidityTools::isDynamic(const std::string &strType)
	{
		return isDynamicArray(strType) || isDBytesType(strType) || isStringType(strType);
	}

	bool SolidityTools::isArray(const std::string & strType)
	{
		auto r = nestTypes(strType);
		return !r.empty();
	}

	bool SolidityTools::isDynamicArray(const std::string &strType)
	{
		auto r = nestTypes(strType);
		if (!r.empty())
		{
			boost::regex reg("\\[[0-9]{1,}\\]");
			return !boost::regex_match(r[r.size() - 1], reg);
		}

		return false;
	}

	bool SolidityTools::isStaticArray(const std::string &strType)
	{
		auto r = nestTypes(strType);
		if (!r.empty())
		{
			boost::regex reg("\\[[0-9]{1,}\\]");
			return boost::regex_match(r[r.size() - 1], reg);
		}

		return false;
	}

	//获取静态数组的维度大小
	std::size_t SolidityTools::getStaticArraySize(const std::string &strType)
	{
		if (!SolidityTools::isStaticArray(strType))
		{
			return 1;
		}

		auto types = nestTypes(strType);
		if (!types.empty())
		{
			std::size_t size = strtoul(types[types.size() - 1].c_str() + 1, NULL, 10);
			if (size > 0)
			{
				return size;
			}
		}

		ABI_EXCEPTION_THROW("static array type get dimension failed => type is " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);

		return 1;
	}

	//获取类型静态部分的长度,动态类型返回32
	std::size_t SolidityTools::getStaticPartLen(const std::string &strType)
	{
		if (SolidityTools::isStaticArray(strType))
		{
			std::vector<std::string> types = SolidityTools::nestTypes(strType);
			if (types.empty())
			{
				ABI_EXCEPTION_THROW("static array type get dimension failed => type is " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
			}

			std::size_t size = ABIALIGNSIZE;
			for (const auto &t : types)
			{
				std::size_t i = SolidityTools::getStaticArraySize(t);
				if (i == 0)
				{
					ABI_EXCEPTION_THROW("static array type get size failed => type is " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
				}
				size *= i;
			}

			return size;
		}

		return ABIALIGNSIZE;
	}

	//eg int8 => 8   int256 => 256
	std::size_t SolidityTools::getIntNWidth(const std::string &strType)
	{
		assert(isIntType(strType));
		const static std::string &strIntFlag = "int";
		auto r = strtoul(strType.c_str() + strIntFlag.size(), NULL, 10);
		if (r <= 0)
		{
			ABI_EXCEPTION_THROW("get intN width failed, type => " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
		}

		return r;
	}

	//eg uint8 => 8  uint256 => 256
	std::size_t SolidityTools::getUintNWidth(const std::string &strType)
	{
		assert(isUintType(strType));
		const static std::string &strUIntFlag = "uint";
		auto r = strtoul(strType.c_str() + strUIntFlag.size(), NULL, 10);
		if (r <= 0)
		{
			ABI_EXCEPTION_THROW("get uintN width failed, type => " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
		}

		return r;
	}

	//eg bytes32 => 32
	std::size_t SolidityTools::getBytesNWidth(const std::string &strType)
	{
		assert(isBytesType(strType));
		const static std::string &strBytesNFlag = "bytes";
		auto r = strtoul(strType.c_str() + strBytesNFlag.size(), NULL, 10);
		if (r <= 0 || r > 32)
		{
			ABI_EXCEPTION_THROW("get bytesN width failed, type => " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
		}

		return r;
	}

	//检查是否是正常的aib的数组类型，abi中数组可以是多维数组,不过只能在最后一维是动态类型,即 int a[2][3][]是合法的  int a[][2] int a[2][][4]都是非法的
	void SolidityTools::invalidAbiArray(const std::string &strType)
	{
		auto r = SolidityTools::nestTypes(strType);
		for (std::size_t index = 0; index < r.size(); ++index)
		{
			auto nSize = strtoull(r[index].c_str() + 1, NULL, 10);
			if (index != r.size() - 1 && nSize <= 0)
			{
				//数组索引异常
				ABI_EXCEPTION_THROW("array index valid,type => " + strType, EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidAbiType);
			}
		}
	}

	std::string SolidityTools::padLeft(const std::string &strSource, std::string::size_type nStrSize, char defaultChar)
	{
		if (strSource.size() >= nStrSize)
		{
			return strSource;
		}

		return std::string(nStrSize - strSource.size(), defaultChar) + strSource;
	}

	std::string SolidityTools::padRight(const std::string &strSource, std::string::size_type nStrSize, char defaultChar)
	{
		if (strSource.size() >= nStrSize)
		{
			return strSource;
		}

		return strSource + std::string(nStrSize - strSource.size(), defaultChar);
	}

	std::string SolidityTools::padAlignLeft(const std::string &strSource, std::string::size_type nAignSize, char defaultChar)
	{
		assert(nAignSize);
		std::size_t nMod = strSource.size() % nAignSize;
		if (!nMod)
		{
			return strSource;
		}

		return padLeft(strSource, (strSource.size() + nAignSize - 1) / nAignSize * nAignSize, defaultChar);
	}

	std::string SolidityTools::padAlignRight(const std::string &strSource, std::string::size_type nAignSize, char defaultChar)
	{
		assert(nAignSize);
		std::size_t nMod = strSource.size() % nAignSize;
		if (!nMod)
		{
			return strSource;
		}

		return padRight(strSource, (strSource.size() + nAignSize - 1) / nAignSize * nAignSize, defaultChar);
	}

	//序列化地址
	std::string SolidityTools::inputFormaterAddr(const dev::u160 u)
	{
		return inputFormaterUint(u);
	}

	dev::u160 SolidityTools::outputFormaterAddr(const std::string &s)
	{
		if (!(s.compare(0, 2, "0x") == 0 || s.compare(0, 2, "0X") == 0))
		{
			dev::u160 u("0x" + s);
			return u;
		}

		dev::u160 u(s);
		return u;
	}

	//序列化dynamic bytes数据
	std::string SolidityTools::inputFormaterDBytes(const std::string &s, bool lowcase)
	{
		std::string res;
		if (lowcase)
		{
			boost::algorithm::hex_lower(s.begin(), s.end(), back_inserter(res));
		}
		else
		{
			boost::algorithm::hex(s.begin(), s.end(), back_inserter(res));
		}

		res = padAlignRight(res, HEXABIALIGNSIZE);

		return res;
	}

	std::string SolidityTools::outputFormaterDBytes(const std::string &s)
	{
		std::string result;
		boost::algorithm::unhex(s.begin(), s.end(), std::back_inserter(result));
		return result;

		return "0x" + s;
	}

	//序列化string
	std::string SolidityTools::inputFormaterString(const std::string &s, bool lowcase)
	{
		return inputFormaterDBytes(s, lowcase);
	}

	std::string SolidityTools::outputFormaterString(const std::string &s)
	{
		/*return outputFormaterDBytes(s);*/
		std::string result;
		boost::algorithm::unhex(s.begin(), s.end(), std::back_inserter(result));
		return result;
	}

	std::string SolidityTools::inputFormaterBytes(const std::string & s, bool lowcase)
	{
		return inputFormaterDBytes(s.substr(0, ABIALIGNSIZE), lowcase);
	}

	std::string SolidityTools::outputFormaterBytes(const std::string &s)
	{
		std::string result;
		boost::algorithm::unhex(s.begin(), s.end(), std::back_inserter(result));
		return result;

		return "0x" + s;
	}

	std::string SolidityTools::inputFormaterInt(const dev::s256 i)
	{
		dev::u256 u(i);
// 		if (i >= 0)
// 		{
// 			u = i.convert_to<dev::u256>();
// 		}
// 		else
// 		{
// 			//补码转换
// 			u = (i + dev::u256("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff") + 1).convert_to<dev::u256>();
// 		}
		return inputFormaterUint(u);
	}

	dev::s256 SolidityTools::outputFormaterInt(const std::string &s)
	{
		std::string strTmp = s;

		unsigned int nFirstHex = 0;

		if (!(s.compare(0, 2, "0x") == 0 || s.compare(0, 2, "0X") == 0))
		{
			nFirstHex = strtoull(strTmp.substr(0, 1).c_str(), NULL, 16);
			strTmp = "0x" + s;
		}
		else
		{
			nFirstHex = strtoull(strTmp.substr(2, 1).c_str(), NULL, 16);
		}

		if (nFirstHex >= 8)
		{//<0
			auto r = (dev::u256("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff") - dev::u256(strTmp)) + 1;
			return dev::s256("-" + r.str());
		}
		else
		{//>=0
			dev::u256 u(strTmp);
			return u.convert_to<dev::s256>();
		}
	}

	std::string SolidityTools::inputFormaterUint(dev::u256 u)
	{
		std::stringstream ss;     //定义字符串流
		ss << std::setw(HEXABIALIGNSIZE)       //显示宽度
			<< std::setfill('0')  //填充字符
			<< std::nouppercase
			<< std::hex
			<< u;     //以十六制形式输出

					  //这里不清楚为什么上面的格式化参数std::nouppercase不起作用,手动转换一下
		auto tmp_str = ss.str();
		for_each(tmp_str.begin(), tmp_str.end(), [](char &ch) {
			ch = std::tolower(ch);
		});

		return tmp_str;
	}

	dev::u256 SolidityTools::outputFormaterUint(const std::string &s)
	{
		if (!(s.compare(0, 2, "0x") == 0 || s.compare(0, 2, "0X") == 0))
		{
			dev::u256 u("0x" + s);
			auto str = u.str();
			return u;
		}

		dev::u256 u(s);
		auto str = u.str();
		return u;
	}

	std::string SolidityTools::inputFormaterBool(bool b)
	{
		return std::string("000000000000000000000000000000000000000000000000000000000000000") + (b ? '1' : '0');
	}

	bool SolidityTools::outputFormaterBool(const std::string &strOut)
	{
		return strOut == "0000000000000000000000000000000000000000000000000000000000000001";
	}
}//namespace libabi
