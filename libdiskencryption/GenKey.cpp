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
 * @file: GenKey.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */
#include <curl/curl.h>
#include <libdevcore/FileSystem.h>
#include <libdevcrypto/AES.h>
#include <libdevcore/RLP.h>
#include <libdevcore/easylog.h>
#if ETH_ENCRYPTTYPE
#include <libdevcrypto/sm2/sm2.h>
#endif
#include "CryptoParam.h"
#include "GenKey.h"
GenKey::GenKey(void)
{
	m_cryptoMod = 0;
	m_superKey1 = "de393b2e";
	m_kcUrl = "https:\\127.0.0.1";
	m_superKey = "123";
	m_superKey2 = "8310a2fd";
	m_nodekeyPath = "./network.rlp";
	m_datakeyPath = "./datakey";
	m_superKey3 = "3b4fc15a";
	
	m_changeKey1 = "webankit";
	m_changeKey2 = "kjsyb1!,";
	m_changeKey3 = "jcjgz2@.";
	m_changeKey4 = "zlkfs3#/";
	m_superKey4 = "0dceece1";
}

GenKey::~GenKey(void)
{

}


int GenKey::writer(char* data, size_t size, size_t nmemb, std::string* writer_data)
{  
	unsigned long sizes = size * nmemb;
	if (NULL == writer_data) 
	{
		return 0;  
	}
	writer_data->append(data, sizes);
	return sizes;  
}

void GenKey::setCryptoMod(int cryptoMod)
{
	m_cryptoMod = cryptoMod;
}

void GenKey::setKcUrl(string kcUrl)
{
	m_kcUrl = kcUrl;
}

void GenKey::setSuperKey(string superKey)
{
	m_superKey = superKey;
}

void GenKey::setNodeKeyPath(string nodekeyPath)
{
	m_nodekeyPath = nodekeyPath;
}


void GenKey::setDataKeyPath(string datakeyPath)
{
	m_datakeyPath = datakeyPath;
}

string GenKey::getKeycenterData(string const& b64data,string const& kcUrl,int keyType)
{
	CURL *curl;
	CURLcode res;
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	string strJson = "";
	if (keyType == KEY_DATA)
	{
		strJson = "{\"reqdata\": \""+ b64data + "\", \"id\": 3, \"jsonrpc\": \"2.0\"}";
	}else if (keyType == KEY_NODE)
	{                    
		strJson = "{\"reqdata\": \""+ b64data + "\", \"id\": 1, \"jsonrpc\": \"2.0\"}";
	}
	string reqData = "";
	if(NULL != curl)
	{
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
		curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, false);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1);
		curl_easy_setopt(curl, CURLOPT_URL,(const char*)kcUrl.c_str());
		curl_slist *plist = curl_slist_append(NULL,"Content-Type:application/json;charset=UTF-8");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, (const char*)strJson.c_str());

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer);  
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reqData);  
		res = curl_easy_perform(curl);
		if (res != CURLE_OK)
		{
			printf("curl_easy_perform() failed:%s\n", curl_easy_strerror(res));
		} 
		curl_easy_cleanup(curl);
	}
	curl_global_cleanup();
	return reqData;
}


void GenKey::genEcdsaKey(string const& sFilePath,int cryptoType,string const& keyData,string const& ivData,string const& kcUrl,int keyType)
{
	KeyPair kp = KeyPair::create();
	RLPStream netData(3);
	netData << dev::p2p::c_protocolVersion << kp.secret().ref();
	int count = 0;
	netData.appendList(count);
	switch(cryptoType)
	{
	case CRYPTO_DEFAULT:
		writeFile(sFilePath, netData.out());
		cout << "write into file [" << sFilePath << "]" << "\n";
		break;
	case CRYPTO_LOCAL:
		{
			string b64Data = toBase64(bytesConstRef{netData.out().data(),netData.out().size()});//keypair data
			auto enData = aesCBCEncrypt(bytesConstRef{(const unsigned char*)b64Data.c_str(), b64Data.length()},keyData,keyData.length(),bytesConstRef{(const unsigned char*)ivData.c_str(), ivData.length()});//keycenter channel encrypt
			b64Data = toBase64(bytesConstRef{enData.data(), enData.size()});
			cout<<"b64Data:"<<b64Data<<"\n";
			LOG(DEBUG)<<"CRYPTO_LOCAL::b64Data:"<<b64Data<<"\n";
			writeFile(sFilePath,b64Data);
			cout << "write into file [" << sFilePath << "]" << "\n";
		}
		break;
	case CRYPTO_KEYCENTER:
		{
			string b64Data = toBase64(bytesConstRef{netData.out().data(),netData.out().size()});
			auto enData = aesCBCEncrypt(bytesConstRef{(const unsigned char*)b64Data.c_str(), b64Data.length()},keyData,keyData.length(),bytesConstRef{(const unsigned char*)ivData.c_str(), ivData.length()});//keycenter channel encrypt
			b64Data = toBase64(bytesConstRef{enData.data(), enData.size()});
			cout<<"b64Data:"<<b64Data<<"\n";
			LOG(DEBUG)<<"CRYPTO_KEYCENTER::Encryptob64Data:"<<b64Data;

			string reqData = getKeycenterData(b64Data,kcUrl,keyType);
			if (reqData == "")
			{
				cout<<"Connect KeyCenter Error"<<"\n";
				LOG(DEBUG)<<"Connect KeyCenter Error";
				return;
			}
			cout<<"kcresqdata:"<<reqData<<"\n";
			LOG(DEBUG)<<"CRYPTO_KEYCENTER::KeyCenterResqData:"<<reqData;

			CryptoParam cryptoParam;
			cryptoParam = cryptoParam.loadKeyCenterReq(reqData);
			cout<<"cryptoParam.resData:"<<cryptoParam.m_resData<<"\n";
			cout<<"cryptoParam.ID:"<<cryptoParam.m_ID<<"\n";
			cout<<"cryptoParam.errCode:"<<cryptoParam.m_errCode<<"\n";

			LOG(DEBUG)<<"cryptoParam.resData:"<<cryptoParam.m_resData;
			LOG(DEBUG)<<"cryptoParam.ID:"<<cryptoParam.m_ID;
			LOG(DEBUG)<<"cryptoParam.errCode:"<<cryptoParam.m_errCode;

			if (cryptoParam.m_errCode != 0)//keycenter response
			{
				cout<<"keycenter response err"<<"\n";
				LOG(DEBUG)<<"CRYPTO_KEYCENTER::KeyCenterResqData Error";
				exit(-1);
			}
			auto deb64Data = fromBase64(cryptoParam.m_resData);
			auto deData = aesCBCDecrypt(bytesConstRef{deb64Data.data(), deb64Data.size()},keyData,keyData.length(),bytesConstRef{(const unsigned char*)ivData.c_str(), ivData.length()});//keycenter channel encrypt
			writeFile(sFilePath,deData);
			cout << "write into file [" << sFilePath << "]" << "\n";
		}
		break;
	}
	if (keyType != KEY_DATA)
	{
		writeFile(sFilePath+".pub", kp.pub().hex());
		cout << "eth generate network.rlp. " << "\n";
		cout << "eth public id is :[" << kp.pub().hex() << "]" << "\n";
		cout << "write into file [" << sFilePath + ".pub" << "]" << "\n";
	}
}

void GenKey::generateNetworkRlp(string const& sFilePath,int cryptoType,string const& keyData,string const& ivData,string const& kcUrl,int keyType)//1:nodekey 3:datakey
{
	genEcdsaKey(sFilePath,cryptoType,keyData,ivData,kcUrl,keyType);
}

string GenKey::getSuperKey()
{
	if (m_superKey == "")
	{
		cout<<"cryptomod.json superkey config err"<<"\n";
		exit(-1);
	}
	string superKey = m_superKey1 + m_superKey2 + m_superKey3 + m_superKey4;
	if (m_superKey.length() <= 16)
	{
		superKey = superKey.substr(m_superKey.length(),32 - m_superKey.length());
		superKey = m_superKey +  superKey;
	}else if (m_superKey.length() > 16)
	{	
		superKey = m_superKey.substr(0,16) + m_superKey3 + m_superKey4;
	}
	return superKey;
}


void GenKey::setKeyData()
{
	switch(m_cryptoMod)
	{
	case CRYPTO_DEFAULT:
		generateNetworkRlp(m_nodekeyPath,CRYPTO_DEFAULT);
		break;
	case CRYPTO_LOCAL:
		{
			string superKey = getSuperKey();
			generateNetworkRlp(m_nodekeyPath,CRYPTO_LOCAL,superKey,superKey.substr(0,16));
			generateNetworkRlp(m_datakeyPath,CRYPTO_LOCAL,superKey,superKey.substr(0,16),"",3);
		}
		break;
	case CRYPTO_KEYCENTER:
		{
			string changeKey = m_changeKey1 + m_changeKey2 + m_changeKey3 + m_changeKey4;
			cout<<"nodekeypath:"<<m_nodekeyPath<<"\n";
			cout<<"datakeypath:"<<m_datakeyPath<<"\n";
			cout<<"keycenterUrl:"<<m_kcUrl + "/uploadkey"<<"\n";

			LOG(DEBUG)<<"nodekeypath:"<<m_nodekeyPath;
			LOG(DEBUG)<<"datakeypath:"<<m_datakeyPath;
			LOG(DEBUG)<<"keycenterUrl:"<<m_kcUrl + "/uploadkey";
			generateNetworkRlp(m_nodekeyPath,CRYPTO_KEYCENTER,changeKey,changeKey.substr(0,16),m_kcUrl + "/uploadkey");
			generateNetworkRlp(m_datakeyPath,CRYPTO_KEYCENTER,changeKey,changeKey.substr(0,16),m_kcUrl + "/uploadkey",3);
		}
		break;
	}
}

bytes GenKey::getEcdsaKey()
{
	auto nodesState = contents(getDataDir() + "/network.rlp");
	switch(m_cryptoMod)
	{
	case CRYPTO_DEFAULT:
		break;
	case CRYPTO_LOCAL:
		{
			string superKey = getSuperKey();//local encrypt superkey
			//nodekey
			cout<<"nodekeyfileData:"<<asString(nodesState)<<"\n";
			auto deb64Data = fromBase64(asString(nodesState));
			string ivData = superKey.substr(0,16);
			auto deData = aesCBCDecrypt(bytesConstRef(&deb64Data),superKey,superKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});//decrypt nodekey
			cout<<"aesCBCDecrypt:"<<asString(deData)<<"\n";
			nodesState = fromBase64(asString(deData));

			//datakey
			auto datasState = contents(getDataDir() + "/datakey");
			if (asString(datasState) == "")
			{
				cout<<"datakey file is empty"<<"\n";
				exit(-1);
			}
			deb64Data = fromBase64(asString(datasState));
			deData = aesCBCDecrypt(bytesConstRef(&deb64Data),superKey,superKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});
			string dataKey = asString(deData);
			dataKey = dataKey.substr(0,32);
			cout<<"dataKey:"<<dataKey<<"\n";
			dev::setDataKey(dataKey.substr(0,8),dataKey.substr(8,8),dataKey.substr(16,8),dataKey.substr(24,8));//password subsection memory storage
			dev::setCryptoMod(CRYPTO_LOCAL);
		}
		break;
	case CRYPTO_KEYCENTER:
		{
			//nodekey
			cout<<"nodekeyfileData:"<<asString(nodesState)<<"\n";
			cout<<"keycenterurl:"<<m_kcUrl<<"\n";
			string changeKey = m_changeKey1 + m_changeKey2 + m_changeKey3 + m_changeKey4;
			string ivData = changeKey.substr(0,16);
			auto enData = aesCBCEncrypt(bytesConstRef(&nodesState),changeKey,changeKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});
			string  b64Data = toBase64(bytesConstRef(&enData));
			string reqData =getKeycenterData(b64Data,m_kcUrl + "/getkey",KEY_NODE);
			if (reqData == "")
			{
				cout<<"Connect KeyCenter Error"<<"\n";
				exit(0);
			}
			cout<<"kcreqdata:"<<reqData<<"\n";

			CryptoParam cryptoParam;
			cryptoParam = cryptoParam.loadKeyCenterReq(reqData);
			cout<<"cryptoParam.resData:"<<cryptoParam.m_resData<<"\n";
			cout<<"cryptoParam.ID:"<<cryptoParam.m_ID<<"\n";
			cout<<"cryptoParam.errCode:"<<cryptoParam.m_errCode<<"\n";

			auto deb64Data = fromBase64(cryptoParam.m_resData);
			auto deData = aesCBCDecrypt(bytesConstRef(&deb64Data),changeKey,changeKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(), ivData.length()});
			nodesState = fromBase64(asString(deData));
			//datakey
			auto datasState = contents(getDataDir() + "/datakey");
			if (asString(datasState) == "")
			{
				cout<<"datakey file is empty"<<"\n";
				exit(-1);
			}
			enData = aesCBCEncrypt(bytesConstRef(&datasState),changeKey,changeKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(),ivData.length()});//channel encrypt
			b64Data = toBase64(bytesConstRef(&enData));
			reqData = getKeycenterData(b64Data,m_kcUrl + "/getkey",KEY_DATA);
			if (reqData == "")
			{
				cout<<"Connect KeyCenter Err"<<"\n";
				exit(0);
			}
			cout<<"kcreqdata:"<<reqData<<"\n";

			cryptoParam = cryptoParam.loadKeyCenterReq(reqData);
			cout<<"cryptoParam.resData:"<<cryptoParam.m_resData<<"\n";
			cout<<"cryptoParam.ID:"<<cryptoParam.m_ID<<"\n";
			cout<<"cryptoParam.errCode:"<<cryptoParam.m_errCode<<"\n";

			deb64Data = fromBase64(cryptoParam.m_resData);
			deData = aesCBCDecrypt(bytesConstRef(&deb64Data),changeKey,changeKey.length(),bytesConstRef{(const unsigned char*)ivData.c_str(), ivData.length()});//decrypt channel encrypt
			string dataKey = asString(deData);
			dataKey = dataKey.substr(0,32);
			dev::setDataKey(dataKey.substr(0,8),dataKey.substr(8,8),dataKey.substr(16,8),dataKey.substr(24,8));//password subsection memory storage
			dev::setCryptoMod(CRYPTO_KEYCENTER);
		}
		break;
	}
	return nodesState;
}

bytes GenKey::getKeyData()
{
	return getEcdsaKey();
}