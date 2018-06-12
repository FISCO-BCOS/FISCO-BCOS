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
 * @file: UTXODBMgr.cpp
 * @author: chaychen
 * 
 * @date: 2018
 */


#include <boost/filesystem.hpp>

#include <libdevcore/easylog.h>
#include <libdevcore/OverlayDB.h>
#include <libdevcore/TrieHash.h>
#include <libethcore/Exceptions.h>

#include "UTXODBMgr.h"
#include "UTXOSharedData.h"

using namespace dev;
using namespace eth;
using namespace std;

namespace UTXOModel
{
	UTXODBMgr::UTXODBMgr()
	{
	}

	UTXODBMgr::UTXODBMgr(const UTXODBMgr& _s):
    	m_cacheWritedtoDB(_s.m_cacheWritedtoDB),
		m_dbCacheCnt(_s.m_dbCacheCnt),
		m_dbHash(_s.m_dbHash),
		m_cacheToken(_s.m_cacheToken),
		m_cacheTokenBlockNum(_s.m_cacheTokenBlockNum)
	{
	}

	UTXODBMgr& UTXODBMgr::operator=(const UTXODBMgr& _s)
	{
		if (&_s == this)
			return *this;

		m_cacheWritedtoDB = _s.m_cacheWritedtoDB;
		m_dbCacheCnt = _s.m_dbCacheCnt;
		m_dbHash = _s.m_dbHash;
		m_cacheToken = _s.m_cacheToken;
		m_cacheTokenBlockNum = _s.m_cacheTokenBlockNum;

		return *this;
	}

	UTXODBMgr::~UTXODBMgr()
	{
		m_cacheWritedtoDB.clear();
		m_cacheToken.clear();
		m_cacheTokenBlockNum.clear();
	}
	
	void UTXODBMgr::initVaultCache()
	{
		h256 accountRegistered = (h256)0;	// Newly registered account
		bool bFind = false;

		vector<h256> accountList = UTXOSharedData::getInstance()->getAccountList();
		if (accountList.size() > 0)
		{
			accountRegistered = accountList[accountList.size() - 1];
		}
		
		LOG(TRACE) << "UTXODBMgr::initVaultCache account:" << toJS(accountRegistered);

		leveldb::Iterator* it = UTXOSharedData::getInstance()->getVaultDB()->NewIterator(leveldb::ReadOptions());
		for (it->SeekToFirst(); it->Valid(); it->Next())
		{
			Vault vault(jsToBytes(it->value().ToString()));
			//vault.Print();

			h256 owner = vault.getOwnerHash();
			bFind = bFind || (owner == accountRegistered);
			
			if (owner == accountRegistered)
			{
				string tokenKey = vault.getTokenKey();
				Token token;
				getToken(tokenKey, token);
				UTXOSharedData::getInstance()->setCacheVault(owner, tokenKey, Token_Record(tokenKey, token.m_tokenBase.getValue(), token.m_tokenExt.getTokenState()));
				//LOG(TRACE) << "UTXODBMgr::addVault Vault db to Memory, key:" << tokenRecord.tokenKey << ",value:" << (int)token.m_tokenBase.getValue() << ",state:" <<(int)token.m_tokenExt.getTokenState();
			}
			vector<Vault> vaultList;
			{
				map<string, Token_Record> list = UTXOSharedData::getInstance()->getCacheVaultByAccount(owner);
				for (map<string, Token_Record>::iterator it = list.begin(); it != list.end(); it++)
				{
					string tokenKey = it->first;
					Token token;
					getToken(tokenKey, token);
					Vault vault(token.m_tokenBase.getOwnerHash(), tokenKey);
					vaultList.push_back(vault);
				}
			} 
			for (const Vault& vault : vaultList)
			{
				addVault(vault, true);
			}
		}
		delete it;
		LOG(TRACE) << "UTXODBMgr::initVaultCache cur tokenRecord(Vault db + Vault cache) size:" << UTXOSharedData::getInstance()->getCacheVaultByAccount(accountRegistered).size();
		
		// Get tokens from the Token DB
		if (false == bFind)
		{
			leveldb::Iterator* it = UTXOSharedData::getInstance()->getDB()->NewIterator(leveldb::ReadOptions());
			for (it->SeekToFirst(); it->Valid(); it->Next())
			{
				string key = it->key().ToString();
				if (key.length() >= 4 && 
					key.substr(0,2) == TOKEN_BASE_KEY_PERFIX)					// TokenBase prefix
				{
					TokenBase tokenBase(jsToBytes(it->value().ToString()));
					if (tokenBase.getOwnerHash() == accountRegistered)
					{
						string tokenKey = key.substr(3);
						Vault vault(accountRegistered, tokenKey);
						addVault(vault, true);
					} 
				}
			} 
			delete it;
			LOG(TRACE) << "UTXODBMgr::initVaultCache cur tokenRecord(Token db) size:" << UTXOSharedData::getInstance()->getCacheVaultByAccount(accountRegistered).size();
		}

		return;
	}

	void UTXODBMgr::registerAccount(const string& account)
	{
		UTXOSharedData::getInstance()->pushAcccountToList(sha3(account));
		LOG(TRACE) << "UTXODBMgr::registerAccount cur account(" << account << "," << toJS(sha3(account)) << "),size:" << UTXOSharedData::getInstance()->getAccountList().size();
		initVaultCache();
		updateHistoryAccount(account);
		return;
	}

	void UTXODBMgr::updateHistoryAccount(const string& account)
	{
		vector<h256> hashList;
		h256 hashAccount = sha3(account);
		AccountRecord accountRecord;

		string accountList;
		GetFromDB(UTXOSharedData::getInstance()->getExtraDB(), VAULT_KEY_IN_EXTRA, &accountList);

		if (accountList.length() > 0)
		{
			AccountRecord temp(jsToBytes(accountList));
			hashList = temp.getAccountList();
			if (find(hashList.begin(),hashList.end(),hashAccount) == hashList.end())
			{
				temp.addAccountList(hashAccount);
			}
			accountRecord = temp;
		}
		else 
		{
			accountRecord.addAccountList(hashAccount);
		}

		try {
			leveldb::Status s = UTXOSharedData::getInstance()->getExtraDB()->Put(leveldb::WriteOptions(), VAULT_KEY_IN_EXTRA, toJS(accountRecord.rlp()));
			if (s.ok())
			{
				LOG(TRACE) << "UTXODBMgr::updateHistoryAccount Success, accountHash:" << hashAccount;
			}
			else 
			{
				LOG(TRACE) << "UTXODBMgr::updateHistoryAccount Status:" << s.ToString();
			}
		}
		catch (...) {
			LOG(ERROR) << "UTXODBMgr::updateHistoryAccount UTXODBError";
			BOOST_THROW_EXCEPTION(UTXODBError());
		}
	}

	void UTXODBMgr::registerHistoryAccount()
	{
		string accountList;
		GetFromDB(UTXOSharedData::getInstance()->getExtraDB(), VAULT_KEY_IN_EXTRA, &accountList);
		if (accountList.length() == 0)
		{
			LOG(TRACE) << "UTXODBMgr::registerHistoryAccount NULL";
			return;
		}
		AccountRecord accountRecord(jsToBytes(accountList));
		vector<h256> hashList = accountRecord.getAccountList();
		for (h256 hash : hashList)
		{
			UTXOSharedData::getInstance()->pushAcccountToList(hash);
			LOG(TRACE) << "UTXODBMgr::registerHistoryAccount cur account(" << toJS(hash) << "),size:" << UTXOSharedData::getInstance()->getAccountList().size();
			initVaultCache();
		}
		return;
	}

	bool UTXODBMgr::GetFromDB(leveldb::DB* db, const string& key, string* value)
	{
		//LOG(TRACE) << "UTXODBMgr::GetFromDB, key:" << key;
		try {
			leveldb::Status s = db->Get(leveldb::ReadOptions(), key, value);
			if (!s.ok())
			{
				LOG(TRACE) << "UTXODBMgr::GetFromDB Status:" << s.ToString();
			}
			return s.ok();
		}
		catch (...) {
			LOG(ERROR) << " UTXODBMgr::GetFromDB UTXODBError";
			BOOST_THROW_EXCEPTION(UTXODBError());
		}
	}

	void UTXODBMgr::addTokenIdx(const string& key, const TokenExtOnBlockNum& tokenIdx)		// 参数key为Token原Key
	{
		string strKey = GET_TOKEN_NUM_KEY(key);
		string strValue = toJS(tokenIdx.rlp());
		//LOG(TRACE) << "UTXODBMgr::addTokenIdx key:" << strKey;
		UTXODBCache tokenIdxRecord(strKey, strValue);
		DEV_WRITE_GUARDED(m_dbCache_lock)
		{
			m_cacheWritedtoDB[strKey] = tokenIdxRecord;
			m_dbCacheCnt++;
		}
		DEV_WRITE_GUARDED(m_tokenCache_lock)
		{
			m_cacheTokenBlockNum[key] = tokenIdx;
		}
	}

	bool UTXODBMgr::getTokenIdx(const string& key, TokenExtOnBlockNum& tokenIdx)			// 参数key为Token原Key
	{
		//LOG(TRACE) << "UTXODBMgr::getTokenIdx, key:" << key;
		DEV_READ_GUARDED(m_tokenCache_lock)
		{
			map<string, TokenExtOnBlockNum>::iterator it = m_cacheTokenBlockNum.find(key);
			if (it != m_cacheTokenBlockNum.end())
			{
				//LOG(TRACE) << "UTXODBMgr::getTokenIdx from memory";
				tokenIdx = it->second;
				return true;
			}
		}

		string strKey = GET_TOKEN_NUM_KEY(key);
		string strTokenIdx;
		GetFromDB(UTXOSharedData::getInstance()->getDB(), strKey, &strTokenIdx);
		if (strTokenIdx.length() == 0)
		{
			LOG(ERROR) << "UTXODBMgr::getTokenIdx NOT FOUND";
			return false;
		}

		TokenExtOnBlockNum _tokenIdx(jsToBytes(strTokenIdx));
		tokenIdx = _tokenIdx;
		DEV_WRITE_GUARDED(m_tokenCache_lock)
		{
			m_cacheTokenBlockNum[key] = _tokenIdx;
		}
		//LOG(TRACE) << "UTXODBMgr::getTokenIdx from db";

		return true;
	}

	u256 UTXODBMgr::getTokenBlockNum(const string& key)
	{
		u256 blockNumber = (m_blockNumber > 0)?m_blockNumber:UTXOSharedData::getInstance()->getBlockNum();
		//LOG(TRACE) << "UTXODBMgr::getTokenBlockNum, key:" << key << ", blockNum:" << blockNumber;
		
		TokenExtOnBlockNum tokenIdx;
		if (getTokenIdx(key, tokenIdx) == false || 
			tokenIdx.getUpdatedBlockNum().size() == 0)
		{
			return 0;
		}

		vector<u256> blockNum = tokenIdx.getUpdatedBlockNum();
		// TODO：Improve query performance
		for (int i = (int)blockNum.size() - 1; i >= 0; i--)
		{
			if (blockNumber > blockNum[i])
			{
				return blockNum[i];
			}
		}

		return 0;
	}

	void UTXODBMgr::addToken(const string& key, const TokenBase& tokenBase, const TokenExt& tokenExt, bool bNew) 						// 参数key为Token原Key
	{
		if (bNew)
		{
			string strBaseKey = GET_TOKEN_BASE_KEY(key);
			string strBaseValue = toJS(tokenBase.rlp());
			UTXODBCache tokenBaseRecord(strBaseKey, strBaseValue);
			DEV_WRITE_GUARDED(m_dbCache_lock)
			{
				m_cacheWritedtoDB[strBaseKey] = tokenBaseRecord;
				m_dbCacheCnt++;
			}
		}

		u256 blockNumber = (m_blockNumber > 0)?m_blockNumber:UTXOSharedData::getInstance()->getBlockNum();
		string strExtKey = GET_TOKEN_EXT_KEY(key, toJS(blockNumber));
		string strExtValue = toJS(tokenExt.rlp());
		UTXODBCache tokenExtRecord(strExtKey, strExtValue);
		DEV_WRITE_GUARDED(m_dbCache_lock)
		{
			m_cacheWritedtoDB[strExtKey] = tokenExtRecord;
			m_dbCacheCnt++;
		}

		//LOG(TRACE) << "UTXODBMgr::addToken key:" << key << ",new:" << bNew << ",used:" << (tokenExt.getTokenState() == TokenStateUsed);

		TokenExtOnBlockNum tokenIdx;
		if (bNew)
		{
			tokenIdx.addUpdatedBlockNum(blockNumber);
		}
		else 
		{
			getTokenIdx(key, tokenIdx);
			vector<u256> updatedBlockNum = tokenIdx.getUpdatedBlockNum();
			if (find(updatedBlockNum.begin(),updatedBlockNum.end(),blockNumber) == updatedBlockNum.end())
			{
				tokenIdx.addUpdatedBlockNum(blockNumber);
			}
		}
		addTokenIdx(key, tokenIdx);

		Token token(tokenBase, tokenExt);
		DEV_WRITE_GUARDED(m_tokenCache_lock)
		{
			m_cacheToken[key] = token;
		}
	}

	bool UTXODBMgr::getToken(const string& key, Token& token)
	{
		LOG(TRACE) << "UTXODBMgr::getToken, key:" << key;

		DEV_READ_GUARDED(m_tokenCache_lock)
		{
			map<string, Token>::iterator it = m_cacheToken.find(key);
			if (it != m_cacheToken.end())
			{
				// Get token from cache
				token = it->second;
				LOG(TRACE) << "UTXODBMgr::getToken from memory" << ",used:" << (token.m_tokenExt.getTokenState() == TokenStateUsed);
				return true;
			}
		}

		// Get token from DB
		string strBaseKey = GET_TOKEN_BASE_KEY(key);
		string strBaseValue;
		GetFromDB(UTXOSharedData::getInstance()->getDB(), strBaseKey, &strBaseValue);
		
		u256 blockNumber = getTokenBlockNum(key);
		//LOG(TRACE) << "UTXODBMgr::getToken, key at block:" << blockNumber;
		if (0 == blockNumber)
		{
			LOG(ERROR) << "UTXODBMgr::getToken Error";
			return false;
		}
		string strExtKey = GET_TOKEN_EXT_KEY(key, toJS(blockNumber));
		string strExtValue;
		GetFromDB(UTXOSharedData::getInstance()->getDB(), strExtKey, &strExtValue);

		if (strBaseValue.length() == 0 || 
			strExtValue.length() == 0)
		{
			LOG(ERROR) << "UTXODBMgr::getToken NOT FOUND, baselength:" << strBaseValue.length() << ", extlength:" << strExtValue.length();
			return false;
		}

		TokenBase tokenBase(jsToBytes(strBaseValue));
		TokenExt tokenExt(jsToBytes(strExtValue));
		Token _token(tokenBase, tokenExt);
		token = _token;
		DEV_WRITE_GUARDED(m_tokenCache_lock)
		{
			m_cacheToken[key] = _token;
		}

		LOG(TRACE) << "UTXODBMgr::getToken from db" << ",used:" << (token.m_tokenExt.getTokenState() == TokenStateUsed);

		return true;
	}

	void UTXODBMgr::addTx(const string& key, const UTXOTx& tx) 
	{
		string strKey = GET_TX_KEY(key);
		string strValue = toJS(tx.rlp());
		UTXODBCache txRecord(strKey, strValue);
		DEV_WRITE_GUARDED(m_dbCache_lock)
		{
			m_cacheWritedtoDB[strKey] = txRecord;
			m_dbCacheCnt++;
		}
	}

	bool UTXODBMgr::getTx(const string& key, UTXOTx& tx) 
	{
		string strKey = GET_TX_KEY(key);
		string strTx;
		GetFromDB(UTXOSharedData::getInstance()->getDB(), strKey, &strTx);
		if (strTx.length() == 0)
		{
			LOG(TRACE) << "UTXODBMgr::getTx NOT FOUND";
			return false;
		}

		UTXOTx _tx(jsToBytes(strTx));
		tx = _tx;
		return true;
	}

	void UTXODBMgr::addVault(const Vault& vault, bool commitDB)
	{
		if (false == commitDB)
		{
			string key = vault.getTokenKey();
			string strKey = GET_VAULT_KEY(key);
			string strValue = toJS(vault.rlp());
			UTXODBCache vaultRecord(strKey, strValue);
			DEV_WRITE_GUARDED(m_dbCache_lock)
			{
				m_cacheWritedtoDB[strKey] = vaultRecord;
			}
			return;
		}

		bool bFind;
		h256 owner = vault.getOwnerHash();
		vector<h256> accountList = UTXOSharedData::getInstance()->getAccountList();
		bFind = find(accountList.begin(), accountList.end(), owner) != accountList.end();

		if (bFind)
		{
			try 
			{
				leveldb::Status s = UTXOSharedData::getInstance()->getVaultDB()->Put(leveldb::WriteOptions(), vault.getTokenKey(), toJS(vault.rlp()));
				if (s.ok())
				{
					LOG(TRACE) << "UTXODBMgr::addVault Success.";
				}
				else 
				{
					LOG(ERROR) << "UTXODBMgr::addVault Fail.";
				}
			}
			catch (...) 
			{
				LOG(ERROR) << " UTXODBMgr::addVault UTXODBError";
				BOOST_THROW_EXCEPTION(UTXODBError());
			}
		}
		else {
			LOG(TRACE) << "UTXODBMgr::addVault invalid account";
		}

		string tokenKey = vault.getTokenKey();
		Token token;
		getToken(tokenKey, token);
		/*LOG(TRACE) << "UTXODBMgr::addVault vaultMemory, key:" << tokenKey << ",value:" << 
					  (int)token.m_tokenBase.getValue() << ",state:" << 
					  (int)token.m_tokenExt.getTokenState();*/
		UTXOSharedData::getInstance()->setCacheVault(owner, tokenKey, Token_Record(tokenKey, token.m_tokenBase.getValue(), token.m_tokenExt.getTokenState()));
	}

	void UTXODBMgr::updateVault(const Vault& vault, bool writeMemory)
	{
		if (false == writeMemory)
		{
			DEV_WRITE_GUARDED(m_dbCache_lock)
			{
				string key = vault.getTokenKey();
				string strKey = GET_VAULT_KEY(key);
				string strValue = toJS(vault.rlp());
				UTXODBCache vaultRecord(strKey, strValue);
				m_cacheWritedtoDB[strKey] = vaultRecord;
			}
			return;
		}

		h256 owner = vault.getOwnerHash();
		vector<h256> accountList = UTXOSharedData::getInstance()->getAccountList();
		vector<h256>::iterator it = find(accountList.begin(), accountList.end(), owner);
		if (it != accountList.end())
		{
			string tokenKey = vault.getTokenKey();
			//LOG(TRACE) << "UTXODBMgr::updateVault account is registered, tokenKey:" << tokenKey;
			UTXOSharedData::getInstance()->updateCacheState(owner, tokenKey, TokenStateUsed);
		}
		else 
		{
			//LOG(TRACE) << "UTXODBMgr::updateVault account is no registered";
		}
	}

	void UTXODBMgr::showAllDB()
	{
		LOG(TRACE) << "Show db:";
		{
			map<string, TokenBase> tokenBaseList;
			map<string, TokenExt> tokenExtList;
			map<string, TokenExtOnBlockNum> tokenIdxList;
			map<string, UTXOTx> txList;
			
			leveldb::Iterator* it = UTXOSharedData::getInstance()->getDB()->NewIterator(leveldb::ReadOptions());
			for (it->SeekToFirst(); it->Valid(); it->Next())
			{
				string keyPerFix = it->key().ToString().substr(0,2);
				if (keyPerFix == TOKEN_BASE_KEY_PERFIX)
				{
					TokenBase tokenBase(jsToBytes(it->value().ToString()));
					tokenBaseList[it->key().ToString()] = tokenBase;
				}
				else if (keyPerFix == TOKEN_EXT_KEY_PERFIX)
				{
					TokenExt tokenExt(jsToBytes(it->value().ToString()));
					tokenExtList[it->key().ToString()] = tokenExt;
				}
				else if (keyPerFix == TOKEN_NUM_KEY_PERFIX)
				{
					TokenExtOnBlockNum tokenExtOnBlockNum(jsToBytes(it->value().ToString()));
					tokenIdxList[it->key().ToString()] = tokenExtOnBlockNum;
				}
				else if (keyPerFix == TX_KEY_PERFIX)
				{
					UTXOTx tx(jsToBytes(it->value().ToString()));
					txList[it->key().ToString()] = tx;
				}
				else if (keyPerFix != VAULT_KEY_PERFIX)
				{
					LOG(ERROR) << "UTXODBMgr::showAllDB Error Key, key:" << it->key().ToString();
				}
			} 
			delete it;

			for (map<string, TokenBase>::iterator it = tokenBaseList.begin(); it != tokenBaseList.end(); it++)
			{
				LOG(TRACE) << "TokenBase key:" << it->first;
				it->second.Print();
			}
    		for (map<string, TokenExt>::iterator it = tokenExtList.begin(); it != tokenExtList.end(); it++)
			{
				LOG(TRACE) << "TokenExt key:" << it->first;
				it->second.Print();
			}
    		for (map<string, TokenExtOnBlockNum>::iterator it = tokenIdxList.begin(); it != tokenIdxList.end(); it++)
			{
				LOG(TRACE) << "TokenIdx key:" << it->first;
				it->second.Print();
			}
			for (map<string, UTXOTx>::iterator it = txList.begin(); it != txList.end(); it++)
			{
				LOG(TRACE) << "UTXOTx key:" << it->first;
				it->second.Print();
			}
		}

		LOG(TRACE) << "Show Vaults:";
		{
			int i = 0;
			leveldb::Iterator* it = UTXOSharedData::getInstance()->getVaultDB()->NewIterator(leveldb::ReadOptions());
			for (it->SeekToFirst(); it->Valid(); it->Next())
			{
				LOG(TRACE) << "Vault[" << i++ << "]-key:" << it->key().ToString();
				Vault vault(jsToBytes(it->value().ToString()));
				vault.Print();
			} 
			delete it;
		}

		LOG(TRACE) << "Show Vault Cache:";
		vector<h256> accountList = UTXOSharedData::getInstance()->getAccountList();
		for (size_t i = 0; i < accountList.size(); i++)
		{
			h256 owner = accountList[i];
			map<string, Token_Record> list = UTXOSharedData::getInstance()->getCacheVaultByAccount(owner);
			LOG(TRACE) << "cache owner[" << i << "]:" << toJS(owner) << ",size:" << list.size();
			int idx = 0;
			for (map<string, Token_Record>::iterator it = list.begin(); it != list.end(); it++)
			{
				string tokenKey = it->second.tokenKey;
				string content = tokenKey + "-" + toJS(it->second.tokenValue) + "-" + toJS(it->second.tokenState);
				LOG(TRACE) << owner << "'s token[" << idx++ << "]:" << content;
			}
		}
	}

	void UTXODBMgr::commitDB()
	{
		ldb::WriteBatch batch;

		DEV_READ_GUARDED(m_dbCache_lock)
		{
			int writeCnt = 0;

			for (map<string, UTXODBCache>::iterator it = m_cacheWritedtoDB.begin(); it != m_cacheWritedtoDB.end(); it++)
			{	
				string key = it->first;
				string perfix = key.substr(0,2);
				if (TOKEN_BASE_KEY_PERFIX == perfix || 
					TOKEN_EXT_KEY_PERFIX == perfix || 
					TOKEN_NUM_KEY_PERFIX == perfix || 
					TX_KEY_PERFIX == perfix)
				{
					batch.Put(key, it->second.getValue());
					writeCnt++;
				}
				else if (perfix == VAULT_KEY_PERFIX)
				{
					Vault vault(jsToBytes(it->second.getValue()));
					addVault(vault, true);
				}
				else
				{
					LOG(ERROR) << "UTXODBMgr::commitDB Invalid perfix, key:" << key;
				}
			}
			LOG(TRACE) << "UTXODBMgr::commitDB, cnt:" << writeCnt << "=" << m_dbCacheCnt;
		}

		try
		{
			leveldb::Status s = UTXOSharedData::getInstance()->getDB()->Write(leveldb::WriteOptions(), &batch);
			if (s.ok())
			{
				LOG(TRACE) << "UTXODBMgr::commitDB Success.";
			}
			else 
			{
				LOG(ERROR) << "UTXODBMgr::commitDB Fail,status:" << s.ToString();
				BOOST_THROW_EXCEPTION(UTXODBError());
			}
		}
		catch (...)
		{
			LOG(ERROR) << " UTXODBMgr::commitDB UTXODBError";
			BOOST_THROW_EXCEPTION(UTXODBError());
		}
	}

	bool UTXODBMgr::accountIsRegistered(Address account)
	{
		vector<h256> accountList = UTXOSharedData::getInstance()->getAccountList();
		return (find(accountList.begin(), accountList.end(), sha3(toJS(account))) != accountList.end());
	}

	// BTC logic
	bool UTXODBMgr::selectTokensInAccount(h256 account, u256 nTargetValue, vector<string>& tokenKeys, u256& totalValue)
	{
		tokenKeys.clear();
		totalValue = 0;

		u256 nLowestLarger = 0;

		map<string, Token_Record> records = UTXOSharedData::getInstance()->getCacheVaultByAccount(account);
		for (map<string, Token_Record>::iterator it = records.begin(); it != records.end(); it++)
		{
			if (it->second.tokenState == TokenStateInit)
			{
				nLowestLarger += it->second.tokenValue;
			}
		}
		if (nLowestLarger < nTargetValue)
		{
			return false;
		}
		nLowestLarger += 1;

		string tokenLowestLarger;
		vector<pair<u256, string>> vValue;
		u256 nTotalLower = 0;

		{
			//map<string, Token_Record> records = UTXOSharedData::getInstance()->getCacheVaultByAccount(account);

			for (map<string, Token_Record>::iterator it = records.begin(); it != records.end(); it++)
			{
				if (TokenStateInit != it->second.tokenState)
				{
					continue;
				}
				
				u256 value = it->second.tokenValue;

				if (value < nTargetValue)
           		{
                	vValue.push_back(make_pair(value, it->second.tokenKey));
                	nTotalLower += value;
            	}
            	else if (value == nTargetValue)
            	{
                	tokenKeys.push_back(it->second.tokenKey);
					totalValue = value;
                	return true;
            	}
            	else if (value < nLowestLarger)
            	{
                	nLowestLarger = value;
                	tokenLowestLarger = it->second.tokenKey;
            	}
			}
		}
		
		if (nTotalLower < nTargetValue)
		{
			if (tokenLowestLarger == "")
			{
				LOG(TRACE) << "UTXODBMgr::SelectTokensInAccount totalValue = " << totalValue << ", less than " << nTargetValue;
				return false;
			}
			tokenKeys.push_back(tokenLowestLarger);
			totalValue = nLowestLarger;
			return true;
		}
		
		// Solve subset sum by stochastic approximation
		sort(vValue.rbegin(), vValue.rend());
		vector<char> vfIncluded;
		vector<char> vfBest(vValue.size(), true);
		u256 nBest = nTotalLower;

		for (int nRep = 0; nRep < 50 && nBest != nTargetValue; nRep++)
		{
			vfIncluded.assign(vValue.size(), false);
			u256 nTotal = 0;
			bool fReachedTarget = false;
			for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
			{
				for (size_t i = 0; i < vValue.size(); i++)
				{
					if (nPass == 0 ? rand() % 2 : !vfIncluded[i])
					{
						nTotal += vValue[i].first;
						vfIncluded[i] = true;
						if (nTotal >= nTargetValue)
						{
							fReachedTarget = true;
							if (nTotal < nBest)
							{
								nBest = nTotal;
								vfBest = vfIncluded;
							}
							nTotal -= vValue[i].first;
							vfIncluded[i] = false;
						}
					}
				}
			}
		}

		// If the next larger is still closer, return it
		if (tokenLowestLarger != "" && 
			nLowestLarger - nTargetValue <= nBest - nTargetValue)
		{
			tokenKeys.push_back(tokenLowestLarger);
			totalValue = nLowestLarger;
		}
		else
		{
			for (size_t i = 0; i < vValue.size(); i++)
			{
				if (vfBest[i])
				{
					tokenKeys.push_back(vValue[i].second);
				}
			} 
			totalValue = nBest;
		}

		return true;
	}

	pair<UTXOExecuteState, string> UTXODBMgr::tokenTracking(const string& tokenKey, vector<string>& tokenKeys)
	{
		tokenKeys.clear();
		
		vector<string> tokenKeyToProcess;
		tokenKeyToProcess.push_back(tokenKey);
		size_t idxProcessing = 0;
		map<string, bool> tokenKeyProcessed;
		tokenKeyProcessed[tokenKey] = true;
		map<string, bool> txKeyProcessed;

		while (idxProcessing < tokenKeyToProcess.size())
		{
			string key = tokenKeyToProcess[idxProcessing];

			size_t index = key.find_first_of("_", 0);
			if (index == string::npos)
			{
				LOG(ERROR) << "UTXOMgr::TokenTracking Error:" << UTXOExecuteState::TokenIDInvalid;
				return make_pair(UTXOExecuteState::TokenIDInvalid, UTXOModel::getMsgByCode(UTXOExecuteState::TokenIDInvalid));	
			}
			string txKey = key.substr(0, index);

			UTXOTx tx;
			if (getTx(txKey, tx) == false)
			{
				LOG(ERROR) << "UTXOMgr::TokenTracking Error:" << UTXOExecuteState::TxIDInvalid;
				return make_pair(UTXOExecuteState::TxIDInvalid, UTXOModel::getMsgByCode(UTXOExecuteState::TxIDInvalid));				
			}

			if (false == txKeyProcessed[txKey])
			{
				txKeyProcessed[txKey] = true;
				string content = tx.ToJsonString();
				content.pop_back();
				tokenKeys.push_back(content);
			}

			vector<string> inTokenKeys = tx.getInTokenKey();
			for (size_t i = 0; i < inTokenKeys.size(); i++)
			{
				string inTokenKey = inTokenKeys[i];
				if (false == tokenKeyProcessed[inTokenKey])
				{
					tokenKeyProcessed[inTokenKey] = true;
					tokenKeyToProcess.push_back(inTokenKey);
				}
			}

			idxProcessing++;
		}

		return make_pair(UTXOExecuteState::Success, UTXOModel::getMsgByCode(UTXOExecuteState::Success));
	}

	bool UTXODBMgr::getVaultByAccount(h256 account, TokenState tokenState, vector<string>& tokenKeys)
	{
		map<string, Token_Record> records = UTXOSharedData::getInstance()->getCacheVaultByAccount(account);
		for (map<string, Token_Record>::iterator it = records.begin(); it != records.end(); it++)
		{
			if (TokenStateInValid == tokenState || 
				it->second.tokenState == tokenState)
			{
				tokenKeys.push_back(it->first);
			}
		}

		LOG(TRACE) << "UTXODBMgr::getVaultByAccount Token account:" << account << ",state: " << tokenState << ",size = " << tokenKeys.size();
		return true;
	}

	bool UTXODBMgr::getBalanceByAccount(Address sender, u256& balance)
	{
		balance = 0;
		h256 account = sha3(toJS(sender));
	
		map<string, Token_Record> records = UTXOSharedData::getInstance()->getCacheVaultByAccount(account);
		for (map<string, Token_Record>::iterator it = records.begin(); it != records.end(); it++)
		{
			if (it->second.tokenState == TokenStateInit)
			{
				balance += it->second.tokenValue;
			}
		}

		LOG(TRACE) << "UTXODBMgr::getBalanceByAccount account(" << toJS(sender) << "," << toJS(account) << "), balance:" << account;
		return true;
	}

	h256 UTXODBMgr::getHash()
	{
		LOG(TRACE) << "UTXODBMgr::getHash";
		DEV_READ_GUARDED(m_dbCache_lock)
		{
			if (!m_dbHash && m_dbCacheCnt > 0)
			{
				BytesMap recordsMap;
				map<string, UTXODBCache>::iterator it;
				for (it = m_cacheWritedtoDB.begin(); it != m_cacheWritedtoDB.end(); it++)
				{
					string key = it->first;
					if (key.substr(0,2) == VAULT_KEY_PERFIX)
					{
						continue;
					}
					RLPStream k;
        			k << key;
					RLPStream rlp;
       				it->second.streamRLP(rlp);
					//LOG(TRACE) << "key:" << key << ",value:" << it->second.getValue();
        			recordsMap.insert(make_pair(k.out(), rlp.out()));
				}
				
				m_dbHash = hash256(recordsMap);
				LOG(TRACE) << "UTXODBMgr::getHash , size = " << m_cacheWritedtoDB.size() << ",dbHash = " << toJS(m_dbHash);
			}
		}

		return m_dbHash;
	}

	void UTXODBMgr::clearDBRecord()
	{
		LOG(TRACE) << "UTXODBMgr::clearDBRecord";

		DEV_WRITE_GUARDED(m_dbCache_lock)
		{
			m_dbHash = (h256)0;
			m_dbCacheCnt = 0;
			m_cacheWritedtoDB.clear();
		}
		DEV_WRITE_GUARDED(m_tokenCache_lock)
		{
			m_cacheToken.clear();
			m_cacheTokenBlockNum.clear();
		}	
	}

	void UTXODBMgr::setBlockNum(u256 blockNum)
	{
		m_blockNumber = blockNum;
	}
}//namespace UTXOModel
