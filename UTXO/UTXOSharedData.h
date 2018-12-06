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
 * @file: UTXOSharedData.h
 * @author: chaychen
 * 
 * @date: 2018
 */

#ifndef __UTXOSHAREDDATA_H__
#define __UTXOSHAREDDATA_H__

#include <leveldb/db.h>
#include <libdevcore/Guards.h>
#include <libethcore/CommonJS.h>
#include <libevm/ExtVMFace.h>

#include "Common.h"
#include "UTXOData.h"

using namespace dev;
using namespace dev::eth;
using namespace std;

namespace dev 
{
	namespace eth 
	{
		class Block;
	}
}

namespace UTXOModel
{
	class UTXOSharedData
	{
	public:
		static UTXOSharedData* getInstance()
		{
			static UTXOSharedData instance;
			return &instance;
		}

		// Database initialization
		void initialize(const string& strDBPath, WithExisting _we);
		leveldb::DB* getVaultDB() { return m_vaultdb; }
		leveldb::DB* getDB() { return m_db; }
		leveldb::DB* getExtraDB() { return m_extradb; }

		// Set and get parameters of block.
		void setPreBlockInfo(shared_ptr<dev::eth::Block>, LastHashes const& _lh);
		pair<shared_ptr<dev::eth::Block>, LastHashes> getPreBlockInfo();
		void setBlockNum(u256 blockNum);
		u256 getBlockNum();
		u256 getNonce();

		// Set and get vault related records.
		void pushAcccountToList(h256 account);
		vector<h256> getAccountList();
		void setCacheVault(h256 account, const string& tokenKey, const Token_Record& tokenRecord);
		void updateCacheState(h256 account, const string& tokenKey, TokenState state);
		map<string, Token_Record> getCacheVaultByAccount(h256 account);

		// Set and get cache of SelectTokens interface.
		pair<vector<string>, u256> getSelectTokensByKey(const pair<h256, u256>& key);
		void setSelectTokensByKey(const pair<h256, u256>& key, pair<vector<string>, u256> value);
		void clearTokenMapForSelectTokens();

		// Set and get cache of GetVault interface.
		vector<string> getTokenMapForGetVault(const pair<h256, TokenState>& key);
		void setTokenMapForGetVault(const pair<h256, TokenState>& key, vector<string> value);
		void clearTokenMapForGetVault();

	private:
		UTXOSharedData() = default;
		~UTXOSharedData() = default;

		leveldb::DB* m_vaultdb{ nullptr };					// Database for inconsistent vault data
		leveldb::DB* m_db{ nullptr };						// Database for consistent data
		leveldb::DB* m_extradb{ nullptr };					// Database for other consistent data
		leveldb::DB* openUTXODB(string const& _basePath, WithExisting _we);

		// The parameters of block that execute the transaction
		std::shared_ptr<Block> m_pPreBlock;
		LastHashes m_preLH;
		u256 m_blockNumber;
		u256 m_nonce;

		// vault related records
		mutable SharedMutex m_accountList_lock;
		vector<h256> m_accountList;							// List of registered accounts
		mutable SharedMutex m_vaultMem_lock;
		map<h256, map<string, Token_Record>> m_cacheVault;	// The token cache under per registered account

		mutable SharedMutex m_selectTokens_lock;
		map<pair<h256, u256>, pair<vector<string>, u256>> tokenMapForSelectTokens;		// The cache of SelectTokens interface

		mutable SharedMutex m_getVault_lock;
		map<pair<h256, TokenState>, vector<string>> tokenMapForGetVault;				// The cache of GetVault interface
	};
}

#endif // __UTXOSHAREDDATA_H__