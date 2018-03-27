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
 * @file: SolidityTools.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#ifndef __SOLIDITYHELPER_H__
#define __SOLIDITYHELPER_H__
#include <string>
#include <vector>

#include <libdevcore/Common.h>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/cpp_int/cpp_int_config.hpp>

//abi序列化需要对齐的字节数
const static unsigned int ABIALIGNSIZE = (32);
const static unsigned int HEXABIALIGNSIZE = (32 * 2);

namespace libabi
{
	class SolidityTools
	{
	public:
		SolidityTools() = default;
		~SolidityTools() = default;
		SolidityTools(const SolidityTools &) = delete;
		SolidityTools(SolidityTools &&) = delete;
		SolidityTools& operator=(const SolidityTools &) = delete;

		const static std::string CNS_SPLIT_STRING;

	public:
		static std::vector<std::string> splitString(const std::string &str, const std::string &splitString);
		//数据类型如果是数组,则返回数组降维度后的数据类型 int[3] -> int ; int[2][3] -> int[2]
		static std::string nestName(const std::string &strType);
		//数据类型如果是数组,返回数组的各个维度信息  int[2][3][4] => "[2]"  "[3]"  "[4]"否则返回vector为空
		static std::vector<std::string> nestTypes(const std::string &strTypeName);
		//是否是动态类型
		static bool isDynamic(const std::string &strType);
		//数据类型是否是数组类型
		static bool isArray(const std::string &strType);
		//数据类型是否是动态数组类型
		static bool isDynamicArray(const std::string &strType);
		//数据类型是否是静态数组类型
		static bool isStaticArray(const std::string &strType);
		//获取静态数组的维度大小
		static std::size_t getStaticArraySize(const std::string &strType);
		//获取类型静态部分的长度,动态类型返回32
		static std::size_t getStaticPartLen(const std::string &strType);
		//eg int8 => 8   int256 => 256
		static std::size_t getIntNWidth(const std::string &strType);
		//eg uint8 => 8  uint256 => 256
		static std::size_t getUintNWidth(const std::string &strType);
		//eg bytes32 => 32
		static std::size_t getBytesNWidth(const std::string &strType);

		//检查是否是正常的aib的数组类型，abi中数组可以是多维数组,不过只能在最后一维是动态类型,即 int a[2][3][]是合法的  int a[][2] int a[2][][4]都是非法的
		static void invalidAbiArray(const std::string &strType);

		//填充string到指定的字节数
		static std::string padLeft(const std::string &strSource, std::string::size_type nStrSize, char defaultChar = '0');
		static std::string padRight(const std::string &strSource, std::string::size_type nStrSize, char defaultChar = '0');

		//填充string的长度为size nAlignSize的整数倍
		static std::string padAlignLeft(const std::string &strSource, std::string::size_type nAignSize, char defaultChar = '0');
		static std::string padAlignRight(const std::string &strSource, std::string::size_type nAignSize, char defaultChar = '0');

		//序列化地址
		static std::string inputFormaterAddr(const dev::u160 u);
		static dev::u160 outputFormaterAddr(const std::string &s);

		//序列化bytes数据
		static std::string inputFormaterBytes(const std::string &s, bool lowcase = true);
		static std::string outputFormaterBytes(const std::string &s);

		//序列化dynamic bytes数据
		static std::string inputFormaterDBytes(const std::string &s, bool lowcase = true);
		static std::string outputFormaterDBytes(const std::string &s);

		//序列化string
		static std::string inputFormaterString(const std::string &s, bool lowcase = true);
		static std::string outputFormaterString(const std::string &s);

		//序列化整形
		static std::string inputFormaterInt(const dev::s256 i);
		static dev::s256 outputFormaterInt(const std::string &s);
		static std::string inputFormaterUint(dev::u256 u);
		static dev::u256 outputFormaterUint(const std::string &s);

		//序列化bool
		static std::string inputFormaterBool(bool b);
		static bool outputFormaterBool(const std::string &strOut);
	};//class SolidityTools

}//namespace libabi

#endif//__SOLIDITYHELPER_H__