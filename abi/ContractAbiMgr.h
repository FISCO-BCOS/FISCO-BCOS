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
 * @file: ContractAbiMgr.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#ifndef __CONTRACTABIMGR_H__
#define __CONTRACTABIMGR_H__
#include <atomic>
#include <libethereum/ChainParams.h>
#include <libethereum/Transaction.h>
#include "SolidityAbi.h"
#include "ContractAbiDBMgr.h"

using namespace dev;
using namespace dev::eth;

namespace libabi
{
	class ContractAbiMgr
	{
	public:
		//single instance
		static ContractAbiMgr* getInstance()
		{
			static ContractAbiMgr instance;
			return &instance;
		}
		//初始化工作，最主要是level-db的初始化
		void initialize(const std::string &strDBPath);
		//通知系统合约已经初始化完毕
		void setSystemContract();
		//合约信息发生改变,更新level-db里面的内容
		void updateAbiInDB(const SolidityAbi &abi);

	private:
		ContractAbiMgr() = default;
		~ContractAbiMgr() = default;

		std::unique_ptr<ContractAbiDBMgr>  m_contractAbiDBMgr;
		//系统合约是否初始化
		std::atomic<bool>                  m_isSystemContractInit{ false };

		//缓存的abi信息
		mutable SharedMutex  m_abis_lock;//abi列表更新
		//name到abi address的具体信息的映射
		std::map <std::string, libabi::SolidityAbi > m_abis;
		//地址到name的映射
		std::map <std::string, std::string> m_addrToName;

	public:
		std::size_t getContractC();
		//从缓存获取abi信息
		void getContractAbi(const std::string strContractName, const std::string &strVersion, SolidityAbi &abi);
		void addContractAbi(const SolidityAbi &abi);
		void addContractAbi(const std::string &strContractName, const std::string &strVersion, const std::string &strAbi, dev::Address addr, dev::u256 blocknumber, dev::u256 timestamp);

		void getContractAbiByAddr(const std::string &strAddr, SolidityAbi &abi);
		void getContractAbi0(const std::string strContractName, const std::string &strVersion, SolidityAbi &abi);
		void getContractAbiFromAbiDBMgr(const std::string strContractName, const std::string &strVersion, SolidityAbi &abi);
		void getContractAbiFromCache(const std::string strContractName, const std::string &strVersion, SolidityAbi &abi);

		//根据name获取abi address信息
		std::pair<Address, bytes> getAddrAndDataInfo(const std::string &strContractName, const std::string &strFunc,const std::string &strVer, const Json::Value &jParams);
		//从system 合约缓存获取abi address信息
		std::pair<Address, bytes> getAddrAndDataFromCache(const std::string &strContractName, const std::string &strFunc, const std::string &strVer, const Json::Value &jParams);
		//从db mgr缓存获取abi address信息
		std::pair<Address, bytes> getAddrAndDataFromAbiDBMgr(const std::string &strContractName, const std::string &strFunc, const std::string &strVer, const Json::Value &jParams);
	};
}

#endif // __CONTRACTABIMGR_H__
