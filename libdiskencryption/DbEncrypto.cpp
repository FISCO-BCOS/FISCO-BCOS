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
 * @file: DbEncrypto.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "DbEncrypto.h"
#include <libdevcrypto/AES.h>
#include <libdevcore/FileSystem.h>
#include <libdevcore/easylog.h>
//#include "LRUCache.h"

#include <iostream>
#include <string>
using namespace std;
/*static void DeleteLRU(const ldb::Slice& key,void *v)
{
	LOG(DEBUG)<<"LRUCache::DeleteKeyData"<<key.ToString();
	delete [] v;
}*/

char*  DbEncrypto::ascii2hex(const char* chs,int len)  
{  
	char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8','9', 'A', 'B', 'C', 'D', 'E', 'F'};

	char *ascii = (char*)calloc ( len * 3 + 1, sizeof(char) );// calloc ascii

	int i = 0;
	while( i < len )
	{
		int b= chs[i] & 0x000000ff;
		ascii[i*2] = hex[b/16] ;
		ascii[i*2+1] = hex[b%16] ;
		++i;
	}
	return ascii;                    // ascii not free
}

DbEncrypto::DbEncrypto(ldb::DB* _db)
{
	if (_db != nullptr)
	{
		m_dbFlag = 1;
		LOG(DEBUG)<<"_db is not null DbEncrypto::m_db = _db";
	}
	else
	{
		m_dbFlag = 0;
	}
	m_db = _db;
	m_cryptoMod = dev::getCryptoMod();
	map<int,string> keyData = dev::getDataKey();
	m_superKey = keyData[0] + keyData[1] + keyData[2] + keyData[3];
	LOG(DEBUG)<<"DbEncrypto::cryptoMod:"<<m_cryptoMod;
	LOG(DEBUG)<<"DbEncrypto::m_db:"<<m_db;
	//LOG(DEBUG)<<"DbEncrypto::CacheSize:"<<s_newlrucache->size();
}

DbEncrypto::~DbEncrypto(void)
{
	if (m_dbFlag != 1)
	{
		LOG(DEBUG)<<"DbEncrypto::m_db release success m_db:"<<m_db;
		delete m_db;
		m_db = nullptr;
		LOG(DEBUG)<<"DbEncrypto::m_db release success";
	}
}


ldb::Status DbEncrypto::Open(const ldb::Options& options,const std::string& name)
{
	if (dev::g_withExisting == WithExisting::Rescue) {
            ldb::Status stateStatus = leveldb::RepairDB(name, options);
            LOG(INFO) << "repair DbEncrypto leveldb:" << stateStatus.ToString();
    }
	return ldb::DB::Open(options, name, &m_db);
}

ldb::Status DbEncrypto::Put(const ldb::WriteOptions& options, const ldb::Slice& key, const ldb::Slice& value)
{
	ldb::Status _status;
	if (m_cryptoMod == CRYPTO_DEFAULT)
	{
		_status = m_db->Put(options,key,value);
	}
	else
	{
		try
		{
			/*LOG(DEBUG)<<"DbEncrypto::PutKeyData:"<<key.ToString();
			char* wdata = new char[value.size()];
			memcpy(wdata,value.data(),value.size());
			s_newlrucache->Insert(key,reinterpret_cast<void*>(wdata),value.size(),&DeleteLRU);
			LOG(DEBUG)<<"DbEncrypto::InsertDataToCache";*/

			//data encrypt
			bytes enData = enCryptoData(value.ToString());
			//LOG(DEBUG)<<"DbEncrypto::enCryptoData:"<<asString(enData);
			_status = m_db->Put(options,key,(ldb::Slice)dev::ref(enData));
		}
		catch (Exception& ex)
		{
			LOG(ERROR)<<"DbEncrypto::enCryptoData error";
			throw;
		}
	}
	return _status;
}

ldb::Status DbEncrypto::Write(const ldb::WriteOptions& options, ldb::WriteBatch* updates)
{
	return m_db->Write(options, updates);
}

ldb::Status DbEncrypto::Get(const ldb::ReadOptions& options,const ldb::Slice& key,std::string* value)
{
	ldb::Status _status;
	if (m_cryptoMod == CRYPTO_DEFAULT)
	{
		_status = m_db->Get(options,key,value);
	}
	else
	{
		/*LOG(DEBUG)<<"DbEncrypto::GetKeyData:"<<key.ToString();
		leveldb::Cache::Handle* handle = s_newlrucache->Lookup(key);
		if (handle != NULL) 
		{
			char* rdata = reinterpret_cast<char*>(s_newlrucache->Value(handle));
			LOG(DEBUG)<<"LRUData:"<<rdata;
			if (rdata != NULL && strlen(rdata) > 0)
			{
				*value = rdata;
				LOG(DEBUG)<<"DbEncrypto::GetDataFromCache";
				s_newlrucache->Release(handle);
				return _status;
			}
			else
			{
				s_newlrucache->Release(handle);
			}
		}
		else
		{
			LOG(DEBUG)<<"LRU lookup is null";
		}*/
		
		//get db Data and decrypt
		_status = m_db->Get(options,key,value);
		if(!value->empty())
		{
			try
			{
				bytes deData = deCryptoData(*value);
				//LOG(DEBUG)<<"DbEncrypto::deCryptoData:"<<asString(deData);
				*value = asString(deData);
			}catch(Exception& e)
			{
				LOG(ERROR)<<"DbEncrypto::deCryptoData error";
				throw;
			}
		}
	}
	return _status;
}

ldb::Status DbEncrypto::Delete(const ldb::WriteOptions& options, const ldb::Slice& key)
{
	return m_db->Delete(options,key);
}

ldb::Iterator* DbEncrypto::NewIterator(const ldb::ReadOptions& options)
{
	return m_db->NewIterator(options);
}

bytes DbEncrypto::enCryptoData(std::string const& v)
{  
	string ivData = m_superKey.substr(0,16);
	bytes enData = aesCBCEncrypt(bytesConstRef(&v),m_superKey,m_superKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});
	return enData;
}


bytes DbEncrypto::enCryptoData(bytesConstRef const& v)
{  
	string ivData = m_superKey.substr(0,16);
	bytes enData = aesCBCEncrypt(v,m_superKey,m_superKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});
	return enData;
}


bytes DbEncrypto::enCryptoData(bytes const& v)
{
	string ivData = m_superKey.substr(0,16);
	bytes enData = aesCBCEncrypt(bytesConstRef(&v),m_superKey,m_superKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});
	return enData;
}

bytes DbEncrypto::deCryptoData(std::string const& v) const
{
	string ivData = m_superKey.substr(0,16);
	bytes deData = aesCBCDecrypt(bytesConstRef{(const unsigned char*)v.c_str(),v.length()},m_superKey,m_superKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});
	return deData;
}
