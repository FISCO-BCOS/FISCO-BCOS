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
 * @file: BatchEncrypto.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "BatchEncrypto.h"
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

char*  BatchEncrypto::ascii2hex(const char* chs,int len)  
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
	return ascii;                    
}

BatchEncrypto::BatchEncrypto(void)
{
	m_cryptoMod = dev::getCryptoMod();
	map<int,string> keyData = dev::getDataKey();
	m_superKey = keyData[0] + keyData[1] + keyData[2] + keyData[3];
	//LOG(DEBUG)<<"BatchEncrypto::CacheSize:"<<s_newlrucache->size();
}


BatchEncrypto::~BatchEncrypto(void)
{

}

ldb::Status BatchEncrypto::Put(ldb::Slice const& key, ldb::Slice const& value)
{
	ldb::Status _status;
	if (m_cryptoMod == CRYPTO_DEFAULT)
	{
		ldb::WriteBatch::Put(key,value);
	}
	else
	{
		try
		{
			/*LOG(DEBUG)<<"BatchEncrypto::PutKeyData:"<<key.ToString();
			char* wdata = new char[value.size()];
			memcpy(wdata,value.data(),value.size());
			s_newlrucache->Insert(key,reinterpret_cast<void*>(wdata),value.size(),&DeleteLRU);
			LOG(DEBUG)<<"BatchEncrypto::InsertDataToCache";*/

		
			bytes enData = enCryptoData(value.ToString());
			//LOG(DEBUG)<<"BatchEncrypto::enCryptoData:"<<asString(enData);
			ldb::WriteBatch::Put(key,(ldb::Slice)dev::ref(enData));
		}
		catch(Exception& e)
		{
			LOG(ERROR)<<"BatchEncrypto::enCryptoData error";
			throw;
		}
	}
	return _status;
}


bytes BatchEncrypto::enCryptoData(std::string const& v)
{  
	string ivData = m_superKey.substr(0,16);
	bytes enData = aesCBCEncrypt(bytesConstRef(&v),m_superKey,m_superKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});
	return enData;
}


bytes BatchEncrypto::enCryptoData(bytesConstRef const& v)
{  
	string ivData = m_superKey.substr(0,16);
	bytes enData = aesCBCEncrypt(v,m_superKey,m_superKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});
	return enData;
}


bytes BatchEncrypto::enCryptoData(bytes const& v)
{
	string ivData = m_superKey.substr(0,16);
	bytes enData = aesCBCEncrypt(bytesConstRef(&v),m_superKey,m_superKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});
	return enData;
}

bytes BatchEncrypto::deCryptoData(std::string const& v) const
{
	string ivData = m_superKey.substr(0,16);
	bytes deData = aesCBCDecrypt(bytesConstRef{(const unsigned char*)v.c_str(),v.length()},m_superKey,m_superKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});
	return deData;
}