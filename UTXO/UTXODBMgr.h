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
 * @file: UTXODBMgr.h
 * @author: chaychen
 * 
 * @date: 2017
 */

#ifndef __UTXODBMGR_H__
#define __UTXODBMGR_H__

#include <string>
#include <map>

#include <leveldb/db.h>
#include <libdevcore/Common.h>
#include <libdevcore/Guards.h>
#include <libdevcrypto/Common.h>

#include "UTXOData.h"

using namespace dev;
using namespace std;

namespace UTXOModel
{
	class UTXODBMgr
	{
	public:
		UTXODBMgr();
		UTXODBMgr(const UTXODBMgr& _s);
		UTXODBMgr& operator=(const UTXODBMgr& _s);
		~UTXODBMgr();

	public:
		// Build caching for registered accounts.
		void initVaultCache();
		// Interface to register an account.
		void registerAccount(const string& account);
		// Update the records of registered accounts.
		void updateHistoryAccount(const string& account);
		// Load registered accounts and build caching when the program starts.
		void registerHistoryAccount();

		// Database operations
		void addTokenIdx(const string& key, const TokenExtOnBlockNum& tokenIdx);
		bool getTokenIdx(const string& key, TokenExtOnBlockNum& tokenIdx);
		u256 getTokenBlockNum(const string& key);
		void addToken(const string& key, const TokenBase& tokenBase, const TokenExt& tokenExt, bool bNew);
		bool getToken(const string& strKey, Token& token);
		void addTx(const string& strKey, const UTXOTx& tx);
		bool getTx(const string& strKey, UTXOTx& tx);
		void addVault(const Vault& vault, bool commitDB);					// "commitDB" indicates whether to write DB immediately
		void updateVault(const Vault& vault, bool writeMemory);				// "writeMemory" indicates whether memory is updated immediately
		void showAllDB();
		void commitDB();
		
		// Determine whether the account is registered.
		bool accountIsRegistered(Address account);
		// Find tokens that meet the payment amount.
		bool selectTokensInAccount(h256 account, u256 value, vector<string>& tokenKeys, u256& totalValue);
		// Token backtracking.
		pair<UTXOExecuteState, string> tokenTracking(const string& tokenKey, vector<string>& tokenKeys);
		// Gets a list of all tokens under an account, depending on the token state.
		bool getVaultByAccount(h256 account, TokenState tokenState, vector<string>& tokenKeys);
		// Get account balance.
		bool getBalanceByAccount(Address sender, u256& balance);

		// Calculate the hash of the data used for consistency checking.
		h256 getHash();
		// Clear the hash of the data used for consistency checking.
		void clearDBRecord();
		
		void setBlockNum(u256 blockNum);
		
	private:
		mutable SharedMutex m_dbCache_lock;
		map<string, UTXODBCache> m_cacheWritedtoDB;					// DB cache
		u256 m_dbCacheCnt;											// The count of m_cacheWritedtoDB
		h256 m_dbHash;												// The hash of the data used for consistency checking

		mutable SharedMutex m_tokenCache_lock;
		map<string, Token> m_cacheToken;							// Token cache
		map<string, TokenExtOnBlockNum> m_cacheTokenBlockNum;		// Token BlockNum cache

		bool GetFromDB(leveldb::DB* db, const string& key, string* value);

		u256 m_blockNumber = 0;
	};
}//namespace UTXOModel

#endif//__UTXODBMGR_H__