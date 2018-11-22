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
 * @file: ContractAbiMgr.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "ContractAbiMgr.h"
#include "SolidityTools.h"
#include "SolidityExp.h"
#include "SolidityCoder.h"
#include "ContractAbiDBMgr.h"

#include <libdevcore/CommonJS.h>
#include <libethcore/CommonJS.h>

namespace libabi
{
	void ContractAbiMgr::initialize(const std::string &strDBPath)
	{
		if (m_contractAbiDBMgr)
		{
			ABI_EXCEPTION_THROW("abi db mgr already init.", libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeAbiDBMgrAlreadyInit);
		}

		m_contractAbiDBMgr = std::unique_ptr<ContractAbiDBMgr>(new ContractAbiDBMgr(strDBPath));

		//LOG(INFO) << "[ContractAbiMgr::initialize] init ,path=" << strDBPath;
	}

	void ContractAbiMgr::setSystemContract()
	{
		m_isSystemContractInit = true;

		LOG(INFO) << "[ContractAbiMgr::setSystemContract] set systemcontract.";

		//校验abi信息
		std::map<std::string, libabi::SolidityAbi> allAbis;
		m_contractAbiDBMgr->getAllAbi(allAbis);

		for (auto it = allAbis.begin(); it != allAbis.end(); ++it)
		{
			SolidityAbi abi;
			getContractAbi(it->second.getContractName(), it->second.getVersion(), abi);
			if (abi.rlp() == it->second.rlp())
			{
				//abi信息一致
				LOG(DEBUG) << "[ContractAbiMgr::setSystemContract] set systemcontract"
					<< " ,contract=" << abi.getContractName()
					<< " ,version=" << abi.getVersion()
					<< " ,address=" << abi.getAddr()
					<< " ,blocknumber=" << abi.getBlockNumber().str()
					<< " ,timestamp=" << abi.getTimeStamp().str()
					<< " ,abi=" << abi.getAbi()
					;
			}
			else
			{
				//abi信息不一致
				LOG(WARNING) << "[ContractAbiMgr::setSystemContract] this abi info is differ from sys"
					<< " ,db_contract=" << it->second.getContractName()
					<< " ,db_version=" << it->second.getVersion()
					<< " ,db_address=" << it->second.getAddr()
					<< " ,db_blocknumber=" << it->second.getBlockNumber()
					<< " ,db_timestamp=" << it->second.getTimeStamp()
					<< " ,db_abi=" << it->second.getAbi()
					<< " ,sys_contract=" << abi.getContractName()
					<< " ,sys_version=" << abi.getVersion()
					<< " ,sys_address=" << abi.getAddr()
					<< " ,sys_blocknumber=" << abi.getBlockNumber().str()
					<< " ,sys_timestamp=" << abi.getTimeStamp().str()
					<< " ,sys_abi=" << abi.getAbi()
					;

				ABI_EXCEPTION_THROW("the contract abi info may be changed, contract|version=" + it->second.getContractName() + "|" + it->second.getVersion(), libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeAbiDBDATAChanged);
			}
		}
	}

	void ContractAbiMgr::updateAbiInDB(const SolidityAbi &abi)
	{
		//系统合约没有设置之前不更新,说明处于初始化阶段
		if (!m_isSystemContractInit)
		{
			LOG(TRACE) << "[ContractAbiMgr::updateAbiInDB] systemcontract is not set"
				<< " ,contract=" << abi.getContractName()
				<< " ,version=" << abi.getVersion()
				<< " ,address=" << abi.getAddr()
				<< " ,blocknumber=" << abi.getBlockNumber()
				<< " ,timestamp=" << abi.getTimeStamp()
				<< " ,abi=" << abi.getAbi();

			return;
		}

		m_contractAbiDBMgr->addAbi(abi.getContractName(), abi.getVersion(), abi);
	}

	std::size_t ContractAbiMgr::getContractC()
	{
		std::size_t cnt = 0;
		DEV_READ_GUARDED(m_abis_lock)
		{
			cnt = m_abis.size();
		}

		return cnt;
	}

	void ContractAbiMgr::getContractAbi(const std::string strContractName, const std::string &strVersion, SolidityAbi &abi)
	{
		std::string strCNSName = (strVersion.empty() ? strContractName : strContractName + "/" + strVersion);

		bool isEndOK = false;
		DEV_READ_GUARDED(m_abis_lock)
		{
			if (m_abis.find(strCNSName) != m_abis.end())
			{
				isEndOK = true;
				abi = m_abis[strCNSName];
			}
		}

		if (isEndOK)
		{
			//abi信息一致
			LOG(DEBUG) << "[ContractAbiMgr::getContractAbi] end"
				<< " ,contract=" << abi.getContractName()
				<< " ,version=" << abi.getVersion()
				<< " ,address=" << abi.getAddr()
				<< " ,blocknumber=" << abi.getBlockNumber().str()
				<< " ,timestamp=" << abi.getTimeStamp().str()
				<< " ,abi=" << abi.getAbi()
				;
		}
		else
		{
			ABI_EXCEPTION_THROW("the contract is not exist, contract|version=" + strContractName + "|" + strVersion, libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeContractNotExist);
		}
	}

	void ContractAbiMgr::addContractAbi(const SolidityAbi &abi)
	{
		std::string strCNSName = (abi.getVersion().empty() ? abi.getContractName() : abi.getContractName() + "/" + abi.getVersion());
		//更新缓存
		DEV_READ_GUARDED(m_abis_lock)
		{
			auto it = m_abis.find(strCNSName);
			if (it != m_abis.end())
			{
				//更新操作
				LOG(INFO) << "[ContractAbiMgr::addContractAbi] update operation"
					<< " ,contract=" << it->second.getContractName()
					<< " ,version=" << it->second.getVersion()
					<< " ,address=" << it->second.getAddr()
					<< " ,blocknumber=" << it->second.getBlockNumber().str()
					<< " ,timestamp=" << it->second.getTimeStamp().str()
					<< " ,abi=" << it->second.getAbi()
					;
			}
			m_abis[strCNSName] = abi;

			auto it0 = m_addrToName.find(abi.getAddr());
			if (it0 != m_addrToName.end())
			{
				//更新操作
				LOG(INFO) << "[ContractAbiMgr::addContractAbi] update operation"
					<< " ,cnsname=" << it0->second;
			}
			m_addrToName[abi.getAddr()] = strCNSName;
		}

		//更新level-db
		updateAbiInDB(abi);

		//abi信息一致
		LOG(INFO) << "[ContractAbiMgr::addContractAbi] end"
			<< " ,contract=" << abi.getContractName()
			<< " ,version=" << abi.getVersion()
			<< " ,address=" << abi.getAddr()
			<< " ,blocknumber=" << abi.getBlockNumber().str()
			<< " ,timestamp=" << abi.getTimeStamp().str()
			<< " ,abi=" << abi.getAbi()
			;
	}

	void ContractAbiMgr::addContractAbi(const std::string &strContractName, const std::string &strVersion, const std::string &strAbi, dev::Address addr, dev::u256 blocknumber, dev::u256 timestamp)
	{
		libabi::SolidityAbi abiinfo{ strContractName,strVersion,"0x" + addr.hex(),strAbi ,blocknumber,timestamp };
		addContractAbi(abiinfo);
		return;
	}

	void ContractAbiMgr::getContractAbiByAddr(const std::string &strAddr, SolidityAbi &abi)
	{
		DEV_READ_GUARDED(m_abis_lock)
		{
			auto it = m_addrToName.find(strAddr);
			if (it == m_addrToName.end())
			{
				ABI_EXCEPTION_THROW("not find cnsname by address,strAddr=" + strAddr, libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeContractNotExist);
			}

			auto strCNSName = it -> second;
			auto it0 = m_abis.find(strCNSName);
			if (it0 == m_abis.end())
			{
				ABI_EXCEPTION_THROW("not find abi by cnsname,strAddr=" + strAddr + " ,cnsname=" + strCNSName, libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeContractNotExist);
			}

			abi = it0->second;
		}

		//abi信息
		LOG(DEBUG) << "[ContractAbiMgr::getContractAbiByAddr] end"
			<< " ,contract=" << abi.getContractName()
			<< " ,version=" << abi.getVersion()
			<< " ,address=" << abi.getAddr()
			<< " ,blocknumber=" << abi.getBlockNumber().str()
			<< " ,timestamp=" << abi.getTimeStamp().str()
			<< " ,abi=" << abi.getAbi()
			;
	}

	void ContractAbiMgr::getContractAbi0(const std::string strContractName, const std::string &strVersion, SolidityAbi &abi)
	{
		if (m_isSystemContractInit)
		{
			getContractAbiFromCache(strContractName, strVersion, abi);
		}
		else
		{
			getContractAbiFromAbiDBMgr(strContractName, strVersion, abi);
		}
	}

	void ContractAbiMgr::getContractAbiFromAbiDBMgr(const std::string strContractName, const std::string &strVersion, SolidityAbi &abi)
	{
		if (!m_contractAbiDBMgr->getAbi(strContractName, strVersion, abi))
		{
			ABI_EXCEPTION_THROW("get abi info from abi db mgr failed, contract|func|version=" + strContractName + "|" + strVersion, libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeContractNotExist);
		}
	}

	void ContractAbiMgr::getContractAbiFromCache(const std::string strContractName, const std::string &strVersion, SolidityAbi &abi)
	{
		getContractAbi(strContractName, strVersion, abi);
	}

	std::pair<Address, bytes> ContractAbiMgr::getAddrAndDataInfo(const std::string &strContractName, const std::string &strFunc, const std::string &strVer, const Json::Value &jParams)
	{
		if (m_isSystemContractInit)
		{
			return getAddrAndDataFromCache(strContractName, strFunc, strVer, jParams);
		}
		else
		{
			return getAddrAndDataFromAbiDBMgr(strContractName, strFunc, strVer, jParams);
		}
	}

	//从system 合约缓存获取abi address信息
	std::pair<Address, bytes> ContractAbiMgr::getAddrAndDataFromCache(const std::string &strContractName, const std::string &strFunc, const std::string &strVer, const Json::Value &jParams)
	{
		//获取abi信息
		SolidityAbi abi;
		getContractAbi(strContractName, strVer, abi);
		//序列化abi
		std::string strData = libabi::SolidityCoder::getInstance()->encode(abi.getFunction(strFunc), jParams);
		return std::make_pair(dev::jsToAddress(abi.getAddr()), dev::jsToBytes(strData));
	}

	//从db mgr缓存获取abi address信息
	std::pair<Address, bytes> ContractAbiMgr::getAddrAndDataFromAbiDBMgr(const std::string &strContractName, const std::string &strFunc, const std::string &strVer, const Json::Value &jParams)
	{
		if (!m_contractAbiDBMgr)
		{
			ABI_EXCEPTION_THROW("abi db mgr is not init, contract|func|version=" + strContractName + "|" + strFunc + "|" + strVer, libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeAbiDBMgrNotInit);
		}

		SolidityAbi abi;
		if (!m_contractAbiDBMgr->getAbi(strContractName,strVer,abi))
		{
			ABI_EXCEPTION_THROW("get abi info from abi db mgr failed, contract|func|version=" + strContractName + "|" + strFunc + "|" + strVer, libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeContractNotExist);
		}

		//序列化abi
		std::string strData = libabi::SolidityCoder::getInstance()->encode(abi.getFunction(strFunc), jParams);
		return std::make_pair(dev::jsToAddress(abi.getAddr()), dev::jsToBytes(strData));
	}
}
