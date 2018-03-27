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
 * @file: SolidityCoder.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#ifndef __SOLIDITY_H__
#define __SOLIDITY_H__
#include <string>
#include <vector>
#include <unordered_map>

#include <json/json.h>
#include <libweb3jsonrpc/JsonHelper.h>
#include "SolidityBaseType.h"
#include "SolidityTools.h"
#include "SolidityAbi.h"

namespace std
{
	//libabi::solidity_type 哈希函数
	template<>
	struct hash<libabi::solidity_type>
	{
		size_t operator()(const libabi::solidity_type& s) const
		{
			return std::hash<unsigned int>()(static_cast<unsigned int>(s));
		}
	};
};

namespace libabi
{
	class SolidityCoder
	{
	public:
		//single instance
		static SolidityCoder *getInstance()
		{
			static SolidityCoder instance;
			return &instance;
		}

	private:
		SolidityCoder();
		~SolidityCoder() = default;
		SolidityCoder(const SolidityCoder &) = delete;
		SolidityCoder &operator=(const SolidityCoder &) = delete;

		std::unordered_map<solidity_type, std::function<std::string(const Json::Value &, const std::string &)> > m_map_encoder;
		std::unordered_map<solidity_type, std::function<Json::Value(const std::string &, std::size_t, std::size_t, const std::string &)> > m_map_decoder;

		void registerEncoder();
		void regisgerDecoder();

		std::function<std::string(const Json::Value &, const std::string &)> getEncoder(solidity_type enumType)
		{
			return m_map_encoder[enumType];
		}

		std::function<Json::Value(const std::string &strData, std::size_t nOffset, std::size_t nSize, const std::string &)> getDecoder(solidity_type enumType)
		{
			return m_map_decoder[enumType];
		}

	public:
		//序列化调用信息
		std::string encode(const SolidityAbi::Function &f, const std::string &strParams);
		std::string encode(const SolidityAbi::Function &f, const Json::Value &jParams);
		Json::Value decode(const SolidityAbi::Function &f, const std::string &strData);

		void getCNSParams(const SolidityAbi &abi, dev::eth::CnsParams &params, const std::string &data);

	private:
		//根据接口abi信息序列化函数调用信息，返回为序列化之后的数据
		std::string encodeParams(const std::vector<std::string> &allInputTypes, const Json::Value &jParams);
		//根据接口abi信息反序列化出返回的信息，返回一个json
		Json::Value decodeParams(const std::vector<std::string> &allOutputTypes, const std::string &strData);
		//根据类型序列化对应的数据类型
		std::string encodeParam(const std::string &strType, const Json::Value &jParam, bool bFirst = true);
		//根据接口abi信息序列化函数调用信息，返回为序列化之后的数据
		std::string encodeParamsWithOffset(const std::vector<std::string> &allOutputTypes, const std::vector<std::string> &allResult, std::size_t nOffset);
		//检查abi数据的长度
		void checkAbiDataSize(const std::string &strData, std::size_t nOffset, std::size_t nLen, const std::string &strType);
		//反序列化数据
		Json::Value decodeParam(const std::string &strType, const std::string &strData, std::size_t nOffset);

	};//class SolidityCoder
}//namespace libabi

#endif//__SOLIDITY_H__
