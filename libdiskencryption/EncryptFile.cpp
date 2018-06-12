/*
	This file is part of FISCO-BCOS.

	FISCO-BCOS is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FISCO-BCOS is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: EncryptFile.cpp
 * @author: websterchen
 * 
 * @date: 2018
 */
#include <curl/curl.h>
#include <boost/filesystem.hpp>
#include <libdevcore/easylog.h>
#include <libdevcrypto/AES.h>
#include "CryptoParam.h"
#include "EncryptFile.h"


int EncryptFile::encryptFile(int fileType)
{	
	string _filePath = filePath[fileType];//data key must create and use superKey encrypt
	string _enFilePath = enFilePath[fileType];

	LOG(DEBUG)<<"filePath:"<<_filePath<<" enFilePath:"<<_enFilePath<<" fileType:"<<fileType;
	if(fileType == FILE_DATAKEY)//if FILE_DATAKEY type must create datakey
	{
		return encrypt("",_enFilePath);
	}
	else if(fileExist(_filePath))
	{
		return encrypt(_filePath,_enFilePath);
	}
	else
	{
		LOG(ERROR)<<_filePath + " is not exist";
		return -1; //file not exist
	}
}

bytes EncryptFile::decrypt(const string& _enFilePath)
{
	if (!fileExist(_enFilePath))
	{
		LOG(ERROR)<<_enFilePath + "is not exist";
		return bytes{};
	}
	auto enFileData = contents(_enFilePath);
	string b64Data = "";
	switch(encryptType)
	{
	case CRYPTO_DEFAULT:
		break;
	case CRYPTO_LOCAL:
		{
			auto deData = aesCBCDecrypt(&enFileData,superKey,superKey.length(),bytesConstRef{(const unsigned char*)ivKey.c_str(), ivKey.length()});
			return deData;
		}
		break;
	case CRYPTO_KEYCENTER:
		{
			auto enData = aesCBCEncrypt(&enFileData,changeKey,changeKey.length(),bytesConstRef{(const unsigned char*)ivChangeKey.c_str(), ivChangeKey.length()});//encrypt channel
			b64Data = toBase64(bytesConstRef{enData.data(), enData.size()});
			LOG(DEBUG)<<"CRYPTO_KEYCENTER::Encryptob64Data:"<<b64Data;

			string reqData = getKeycenterResponse(b64Data,keyCenterUrl);//1:node 
			if (reqData == "")
			{
				LOG(ERROR)<<"Connect KeyCenter Error";
				exit(-1);
			}
			LOG(DEBUG)<<"CRYPTO_KEYCENTER::KeyCenterResqData:"<<reqData;
			CryptoParam cryptoParam;
			cryptoParam = cryptoParam.loadKeyCenterReq(reqData);

			LOG(DEBUG)<<"cryptoParam.resData:"<<cryptoParam.m_resData<<" cryptoParam.ID:"<<cryptoParam.m_ID<<" cryptoParam.errCode:"<<cryptoParam.m_errCode;
			if (cryptoParam.m_errCode != 0)
			{
				LOG(ERROR)<<"CRYPTO_KEYCENTER::KeyCenterResqData Error";
				exit(-1);
			}
			auto deb64Data = fromBase64(cryptoParam.m_resData);
			auto deData = aesCBCDecrypt(bytesConstRef{deb64Data.data(), deb64Data.size()},changeKey,changeKey.length(),bytesConstRef{(const unsigned char*)ivChangeKey.c_str(), ivChangeKey.length()});//decrypt channel
			return deData;
		}
		break;
	}
	return enFileData;
}

int EncryptFile::encrypt(const string& _filePath,const string& _enFilePath)
{
	bytes fileData;
	if (_filePath == "") //when is FILE_DATAKEY type  create dataKey
	{
		KeyPair kp = KeyPair::create();
		string dataKey = kp.pub().hex().substr(0,32);
		fileData = asBytes(dataKey);
	}
	else
	{
		fileData = contents(_filePath);
	}

	string b64Data = "";
	switch(encryptType)
	{
	case CRYPTO_DEFAULT:
		break;
	case CRYPTO_LOCAL:
		{
			auto enData = aesCBCEncrypt(&fileData,superKey,superKey.length(),bytesConstRef{(const unsigned char*)ivKey.c_str(), ivKey.length()});
			writeFile(_enFilePath,enData);
		}
		break;
	case CRYPTO_KEYCENTER:
		{
			auto enData = aesCBCEncrypt(&fileData,changeKey,changeKey.length(),bytesConstRef{(const unsigned char*)ivChangeKey.c_str(), ivChangeKey.length()});//encrypt channel
			b64Data = toBase64(bytesConstRef{enData.data(), enData.size()});
			LOG(DEBUG)<<"CRYPTO_KEYCENTER::Encryptob64Data:"<<b64Data;

			string reqData = getKeycenterResponse(b64Data,keyCenterUrl);
			if (reqData == "")
			{
				LOG(ERROR)<<"Connect KeyCenter Error";
				exit(-1);
			}
			LOG(DEBUG)<<"CRYPTO_KEYCENTER::KeyCenterResqData:"<<reqData;
			CryptoParam cryptoParam;
			cryptoParam = cryptoParam.loadKeyCenterReq(reqData);

			LOG(DEBUG)<<"cryptoParam.resData:"<<cryptoParam.m_resData<<" cryptoParam.ID:"<<cryptoParam.m_ID<<" cryptoParam.errCode:"<<cryptoParam.m_errCode;
			if (cryptoParam.m_errCode != 0)
			{
				LOG(ERROR)<<"CRYPTO_KEYCENTER::KeyCenterResqData Error";
				exit(-1);
			}
			auto deb64Data = fromBase64(cryptoParam.m_resData);
			auto deData = aesCBCDecrypt(bytesConstRef{deb64Data.data(), deb64Data.size()},changeKey,changeKey.length(),bytesConstRef{(const unsigned char*)ivChangeKey.c_str(), ivChangeKey.length()});//decrypt channel
			writeFile(_enFilePath,deData);
		}
		break;
	}
	return 0;
}

EncryptFile::EncryptFile(int encryptType,const string& superKey,const string& keyCenterUrl)
{
	string tempKey = superKey1 + superKey2 + superKey3 + superKey4;
	changeKey = changeKey1 + changeKey2 + changeKey3 + changeKey4;
	ivChangeKey = changeKey.substr(0,16);
	if (superKey.length() <= 16)
	{
		tempKey = tempKey.substr(superKey.length(),32 - superKey.length());
		tempKey = superKey +  tempKey;
	}else if (superKey.length() > 16)
	{	
		tempKey = superKey.substr(0,16) + superKey3 + superKey4;
	}

	this->encryptType = encryptType;
	this->superKey = tempKey;
	this->keyCenterUrl = keyCenterUrl;
	this->ivKey = tempKey.substr(0,16);

	filePath.clear();
	enFilePath.clear();
	filePath.insert(filePath.begin(), 20, "");
	enFilePath.insert(enFilePath.begin(),20,"");
	string dataDir = getDataDir();
	if(dataDir == "")
	{
		LOG(ERROR)<<"DataDir is not set";
	}
	LOG(DEBUG)<<"FilePath:"<< dataDir+"/gmnode.key";
#if ETH_ENCRYPTTYPE
	filePath[FILE_NODECERTKEY] = dataDir+"/gmnode.key";
	enFilePath[FILE_NODECERTKEY] = dataDir+"/gmnode.key.encrypt";

	filePath[FILE_NODEPRIVATE] = dataDir+"/gmnode.private";
	enFilePath[FILE_NODEPRIVATE] = dataDir+"/gmnode.private.encrypt";

	filePath[FILE_ENNODECERTKEY] = dataDir+"/gmennode.key";
	enFilePath[FILE_ENNODECERTKEY] = dataDir+"/gmennode.key.encrypt";

	filePath[FILE_DATAKEY] = dataDir+"/datakey";
	enFilePath[FILE_DATAKEY] = dataDir+"/datakey.encrypt";

	filePath[FILE_CACERTKEY] = dataDir+"/gmca.key";
	enFilePath[FILE_CACERTKEY] = dataDir+"/gmca.key.encrypt";

	filePath[FILE_AGENCYCERTKEY] = dataDir+"/gmagency.key";
	enFilePath[FILE_AGENCYCERTKEY] = dataDir+"/gmagency.key.encrypt";
#else
	filePath[FILE_NODECERTKEY] = dataDir+"/node.key";
	enFilePath[FILE_NODECERTKEY] = dataDir+"/node.key.encrypt";

	filePath[FILE_NODEPRIVATE] = dataDir+"/node.private";
	enFilePath[FILE_NODEPRIVATE] = dataDir+"/node.private.encrypt";

	filePath[FILE_DATAKEY] = dataDir+"/datakey";
	enFilePath[FILE_DATAKEY] = dataDir+"/datakey.encrypt";

	filePath[FILE_CACERTKEY] = dataDir+"/ca.key";
	enFilePath[FILE_CACERTKEY] = dataDir+"/ca.key.encrypt";

	filePath[FILE_AGENCYCERTKEY] = dataDir+"/agency.key";
	enFilePath[FILE_AGENCYCERTKEY] = dataDir+"/agency.key.encrypt";
#endif
}

int EncryptFile::writer(char* data, size_t size, size_t nmemb, std::string* writer_data)
{  
	unsigned long sizes = size * nmemb;
	if (NULL == writer_data) 
	{
		return 0;  
	}
	writer_data->append(data, sizes);
	return sizes;  
}

string EncryptFile::getKeycenterResponse(const string& b64data,const string& kcUrl)
{
	CURL *curl;
	CURLcode res;
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	string strJson = "{\"reqdata\": \""+ b64data + "\", \"id\": 1, \"jsonrpc\": \"2.0\"}";
	string responseData = "";
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
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);  
		res = curl_easy_perform(curl);
		if (res != CURLE_OK)
		{
			printf("curl_easy_perform() failed:%s\n", curl_easy_strerror(res));
		} 
		curl_easy_cleanup(curl);
	}
	curl_global_cleanup();
	return responseData;
}

EncryptFile::~EncryptFile()
{

}
bool EncryptFile::fileExist(const string& filePath)
{
	if(boost::filesystem::exists(filePath) && boost::filesystem::is_regular_file(filePath))
	{
		return true;
	}
	return false;
}

bytes EncryptFile::getNodeCrtKey()
{
	if (encryptType == CRYPTO_DEFAULT)
	{
		return contents(filePath[FILE_NODECERTKEY]);
	}
	return decrypt(enFilePath[FILE_NODECERTKEY]);
}

bytes EncryptFile::getNodePrivateKey()
{
	if (encryptType == CRYPTO_DEFAULT)
	{
		return contents(filePath[FILE_NODEPRIVATE]);
	}
	return decrypt(enFilePath[FILE_NODEPRIVATE]);
}

bytes EncryptFile::getEnNodeCrtKey()
{
	if (encryptType == CRYPTO_DEFAULT)
	{
		return contents(filePath[FILE_ENNODECERTKEY]);
	}
	return decrypt(enFilePath[FILE_ENNODECERTKEY]);
}

bytes EncryptFile::getCaCrtKey()
{
	if (encryptType == CRYPTO_DEFAULT)
	{
		return contents(filePath[FILE_CACERTKEY]);
	}
	return decrypt(enFilePath[FILE_CACERTKEY]);
}

bytes EncryptFile::getAgencyCrtKey()
{
	if (encryptType == CRYPTO_DEFAULT)
	{
		return contents(filePath[FILE_AGENCYCERTKEY]);
	}
	return decrypt(enFilePath[FILE_AGENCYCERTKEY]);
}

string EncryptFile::getDataKey()
{
	return asString(decrypt(enFilePath[FILE_DATAKEY]));
}

EncryptFile& EncryptFile::getInstance(int encryptType,const string& superKey,const string& keyCenterUrl)
{
	static EncryptFile encryptFile(encryptType,superKey,keyCenterUrl);
	return encryptFile;
}