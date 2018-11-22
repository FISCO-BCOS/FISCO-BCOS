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
 * @file: ContractAbiDBMgr.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#ifndef __DBMGR_H__
#define __DBMGR_H__

#include <string>
#include <map>

#include "SolidityAbi.h"

#include <libweb3jsonrpc/LevelDB.h>
#include <libdevcore/OverlayDB.h>
#include <libdevcrypto/Common.h>
#include <libdevcore/Common.h>
#include <libdevcore/RLP.h>
#include <libdevcore/db.h>
#include <libdevcore/Guards.h>
#include <unordered_map>
#include <libdevcore/db.h>
#include <leveldb/db.h>

namespace libabi
{
	class ContractAbiDBMgr
	{
	public:
		ContractAbiDBMgr(const std::string &strPath);
		~ContractAbiDBMgr();

	private:
		leveldb::DB* m_db{ nullptr };
		static leveldb::DB* openDB(std::string const& _basePath);

	public:
		void rebuildAllAbis();

		bool getAbi(const std::string &strContractName, const std::string &strVersion, SolidityAbi &abi);
		void addAbi(const std::string &strContractName, const std::string &strVersion, const SolidityAbi &abi);
		std::size_t getAbiC();
		void getAllAbi(std::map<std::string, libabi::SolidityAbi> &allAbis);

	private:
		mutable dev::SharedMutex  m_abis_lock;//m_nameToEntire的锁
		std::map<std::string, SolidityAbi> m_abis;
	};
}//namespace libabi

#endif//__DBMGR_H__