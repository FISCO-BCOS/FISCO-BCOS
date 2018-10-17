/*
	This file is part of FISCO BCOS.

	FISCO BCOS is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FISCO BCOS is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FISCO BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: UTXOSharedData.cpp
 * @author: chaychen
 * 
 * @date: 2018
 */

#include <libethereum/Block.h>
#include <boost/filesystem.hpp>
#include "UTXOSharedData.h"

namespace UTXOModel
{
	void UTXOSharedData::initialize(const string& strDBPath, WithExisting _we)
	{
		LOG(TRACE) << "UTXOMgr::initialize ,path=" << strDBPath;
		
		m_vaultdb = openUTXODB(strDBPath + "/UTXO/vault", _we);
		m_db = openUTXODB(strDBPath + "/UTXO/db", _we);
		m_extradb = openUTXODB(strDBPath + "/UTXO/extra", _we);

		m_pPreBlock = NULL;
		m_nonce = 0;		
	}

	leveldb::DB* UTXOSharedData::openUTXODB(string const & _basePath, WithExisting _we) {
		string path = _basePath;
		boost::filesystem::create_directories(path);
		DEV_IGNORE_EXCEPTIONS(boost::filesystem::permissions(path, boost::filesystem::owner_all));

		leveldb::Options o;
		o.max_open_files    = 256;
		o.create_if_missing = true;
		leveldb::DB * db    = nullptr;
		if (_we == WithExisting::Rescue) {
			ldb::Status status = leveldb::RepairDB(path, o);
			LOG(INFO)<< "repair UTXO leveldb:" << status.ToString();
		}
		leveldb::Status status = ldb::DB::Open(o, path, &db);
		if (!status.ok() || !db)
		{
			if (boost::filesystem::space(path).available < 1024)
			{
				LOG(ERROR) << "UTXODBMgr::openUTXODB Not enough available space found on hard drive.";
				BOOST_THROW_EXCEPTION(NotEnoughAvailableSpace());
			}
			else
			{
				LOG(ERROR) << "UTXODBMgr::openUTXODB Not enough available space found on hard drive,status=" << status.ToString()
					<< " ,path=" << path ;

				BOOST_THROW_EXCEPTION(DatabaseAlreadyOpen());
			}
		}

		LOG(TRACE) << "UTXODBMgr::openUTXODB Opened state DB, path=" << path;

		return db;
	}

	void UTXOSharedData::setPreBlockInfo(shared_ptr<dev::eth::Block> _tempblock, LastHashes const& _lh)
	{
		u256 blockNumber = _tempblock->info().number();
		LOG(TRACE) << "UTXOSharedData::setBlockInfo mumber:" << blockNumber;
		m_pPreBlock = _tempblock;
		m_preLH = _lh;
		setBlockNum(blockNumber+1);
	}

	pair<shared_ptr<dev::eth::Block>, LastHashes> UTXOSharedData::getPreBlockInfo()
	{
		return make_pair(m_pPreBlock, m_preLH);
	}

	void UTXOSharedData::setBlockNum(u256 blockNum)
	{
		LOG(TRACE) << "UTXOSharedData::setBlockNum mumber:" << blockNum;
		m_blockNumber = blockNum;
	}

	u256 UTXOSharedData::getBlockNum()
	{
		return m_blockNumber;
	}

	const u256 NONCEBASE = (u256)(rand());
	const u256 IncreaseMax = 1000 * 10000;

	u256 UTXOSharedData::getNonce()
	{
		u256 nonceTmp = (m_nonce++)%IncreaseMax;
		nonceTmp += NONCEBASE;
		return nonceTmp;
	}

	void UTXOSharedData::pushAcccountToList(h256 account)
	{
		DEV_WRITE_GUARDED(m_vaultMem_lock)
			m_accountList.push_back(account);
	}

	vector<h256> UTXOSharedData::getAccountList()
	{
		DEV_READ_GUARDED(m_accountList_lock)
		{
			return m_accountList;
		}

		return m_accountList;
	}

	void UTXOSharedData::setCacheVault(h256 account, const string& tokenKey, const Token_Record& tokenRecord)
	{
		DEV_WRITE_GUARDED(m_accountList_lock)
			m_cacheVault[account][tokenKey] = tokenRecord;
	}

	void UTXOSharedData::updateCacheState(h256 account, const string& tokenKey, TokenState state)
	{
		DEV_WRITE_GUARDED(m_accountList_lock)
			m_cacheVault[account][tokenKey].tokenState = state;
	}

	map<string, Token_Record> UTXOSharedData::getCacheVaultByAccount(h256 account)
	{
		DEV_READ_GUARDED(m_vaultMem_lock)
		{
			return m_cacheVault[account];
		}
		
		return m_cacheVault[account];
	}

	pair<vector<string>, u256> UTXOSharedData::getSelectTokensByKey(const pair<h256, u256>& key)
	{
		DEV_READ_GUARDED(m_selectTokens_lock)
		{
			return tokenMapForSelectTokens[key];
		}
			
		return tokenMapForSelectTokens[key];
	}

	void UTXOSharedData::setSelectTokensByKey(const pair<h256, u256>& key, pair<vector<string>, u256> value)
	{
		DEV_WRITE_GUARDED(m_selectTokens_lock)
			tokenMapForSelectTokens[key] = value;
	}

	void UTXOSharedData::clearTokenMapForSelectTokens()
	{
		DEV_WRITE_GUARDED(m_selectTokens_lock)
			tokenMapForSelectTokens.clear();
	}

	vector<string> UTXOSharedData::getTokenMapForGetVault(const pair<h256, TokenState>& key)
	{
		DEV_READ_GUARDED(m_getVault_lock)
		{
			return tokenMapForGetVault[key];
		}
			
		return tokenMapForGetVault[key];
	}

	void UTXOSharedData::setTokenMapForGetVault(const pair<h256, TokenState>& key, vector<string> value)
	{
		DEV_WRITE_GUARDED(m_getVault_lock)
			tokenMapForGetVault[key] = value;
	}

	void UTXOSharedData::clearTokenMapForGetVault()
	{
		DEV_WRITE_GUARDED(m_getVault_lock)
			tokenMapForGetVault.clear();
	}
}
