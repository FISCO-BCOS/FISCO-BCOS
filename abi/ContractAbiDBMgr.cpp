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
 * @file: ContractAbiDBMgr.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "ContractAbiDBMgr.h"
#include <boost/filesystem.hpp>
#include <boost/throw_exception.hpp>
#include <libdevcore/easylog.h>
#include <libdevcore/Exceptions.h>
#include <libethcore/Exceptions.h>
#include <libdevcore/CommonJS.h>

using namespace dev;
using namespace dev::eth;

namespace libabi
{
	ContractAbiDBMgr::ContractAbiDBMgr(const std::string &strPath)
	{
		m_db = ContractAbiDBMgr::openDB(strPath);
		rebuildAllAbis();
	}

	ContractAbiDBMgr::~ContractAbiDBMgr()
	{
		if (m_db)
		{
			delete m_db;
			m_db = NULL;
		}
	}

	leveldb::DB* ContractAbiDBMgr::openDB(std::string const & _basePath)
	{
		std::string path = _basePath;
		boost::filesystem::create_directories(path);
		DEV_IGNORE_EXCEPTIONS(boost::filesystem::permissions(path, boost::filesystem::owner_all));

		leveldb::Options o;
		o.max_open_files    = 256;
		o.create_if_missing = true;
		leveldb::DB * db    = nullptr;
		if (dev::g_withExisting == WithExisting::Rescue) {
			ldb::Status status = leveldb::RepairDB(path + "/abiname", o);
			LOG(INFO)<< "repair CNS leveldb:" << status.ToString();
		}

		//打开 state db
		leveldb::Status status = ldb::DB::Open(o, path + "/abiname", &db);
		if (!status.ok() || !db)
		{
			if (boost::filesystem::space(path + "/abiname").available < 1024)
			{
				LOG(ERROR) << "[ContractAbiDBMgr::openDB] Not enough available space found on hard drive.";
				BOOST_THROW_EXCEPTION(NotEnoughAvailableSpace());
			}
			else
			{
				LOG(ERROR) << "[ContractAbiDBMgr::openDB] Not enough available space found on hard drive,status=" << status.ToString()
					<< " ,path=" << (path + "/abiname");

				BOOST_THROW_EXCEPTION(DatabaseAlreadyOpen());
			}
		}

		LOG(INFO) << "[ContractAbiDBMgr::openDB] Opened state DB, path=" << (_basePath + "/abiname");

		return db;
	}

	void ContractAbiDBMgr::rebuildAllAbis()
	{
		leveldb::Iterator* it = m_db->NewIterator(leveldb::ReadOptions());
		for (it->SeekToFirst(); it->Valid(); it->Next())
		{
			SolidityAbi abi(dev::jsToBytes(it->value().ToString()));
			addAbi(abi.getContractName(), abi.getVersion(), abi);
		}

		delete it;

		LOG(INFO) << "[ContractAbiDBMgr::rebuildAllAbis] end ,count = " << getAbiC();

		return;
	}

	std::size_t ContractAbiDBMgr::getAbiC()
	{
		DEV_READ_GUARDED(m_abis_lock)
		{
			return m_abis.size();
		}

		return 0;
	}

	void ContractAbiDBMgr::getAllAbi(std::map<std::string, libabi::SolidityAbi> &allAbis)
	{
		DEV_READ_GUARDED(m_abis_lock)
		{
			allAbis = m_abis;
		}
	}

	bool ContractAbiDBMgr::getAbi(const std::string &strContractName, const std::string &strVersion, SolidityAbi &abi)
	{
		std::string strFindKey = strVersion.empty() ? strContractName : strContractName + "/" + strVersion;
		DEV_READ_GUARDED(m_abis_lock)
		{
			if (m_abis.find(strFindKey) != m_abis.end())
			{
				abi = m_abis[strFindKey];

				LOG(INFO) << "[ContractAbiDBMgr::getAbi] end ok, contract|address|version|blocknumber|timestamp|abi=>"
					<< abi.getContractName() << "|"
					<< abi.getAddr() << "|"
					<< abi.getVersion() << "|"
					<< abi.getBlockNumber() << "|"
					<< abi.getTimeStamp() << "|"
					<< abi.getAbi();

				return true;
			}
		}
		return false;
	}

	void ContractAbiDBMgr::addAbi(const std::string &strContractName, const std::string &strVersion, const SolidityAbi &abi)
	{
		std::string strFindKey = strVersion.empty() ? strContractName : strContractName + "/" + strVersion;

		//level db
		leveldb::Status s = m_db->Put(leveldb::WriteOptions(), strFindKey, dev::toJS(abi.rlp()));
		if (!s.ok())
		{//
			LOG(ERROR) << "[ContractAbiDBMgr::addAbi] write failed, contract|address|version|blocknumber|timestamp|abi=>"
				<< abi.getContractName() << "|"
				<< abi.getAddr() << "|"
				<< abi.getVersion() << "|"
				<< abi.getBlockNumber() << "|"
				<< abi.getTimeStamp() << "|"
				<< abi.getAbi();

			BOOST_THROW_EXCEPTION(DatabaseAlreadyOpen());
		}

		//
		DEV_WRITE_GUARDED(m_abis_lock)
		{
			m_abis[strFindKey] = abi;
		}

		LOG(INFO) << "[ContractAbiDBMgr::addAbi] end ok, contract|address|version|blocknumber|timestamp|abi=>"
			<< abi.getContractName() << "|"
			<< abi.getAddr() << "|"
			<< abi.getVersion() << "|"
			<< abi.getBlockNumber() << "|"
			<< abi.getTimeStamp() << "|"
			<< abi.getAbi();

	}
}//namespace libabi
