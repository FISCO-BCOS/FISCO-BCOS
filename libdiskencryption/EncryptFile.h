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
 * @file: EncryptFile.h
 * @author: websterchen
 * 
 * @date: 2018
 */
#pragma once
#include<iostream>
using namespace std;
using namespace dev;
#include <libdevcore/FileSystem.h>

class EncryptFile
{
public:
	enum CRYPTOTYPE
	{
		CRYPTO_DEFAULT = 0,
		CRYPTO_LOCAL,
		CRYPTO_KEYCENTER
	};

	enum FILETYPE
	{
		FILE_NODECERTKEY = 0,//node certificate private key
		FILE_NODEPRIVATE,//node private key
		FILE_ENNODECERTKEY,//encrypt node private key
		FILE_DATAKEY,
		FILE_CACERTKEY,//ca certificate private key
		FILE_AGENCYCERTKEY//agency certificate private key
	};
	EncryptFile(int encryptType,const string& superKey,const string& keyCenterUrl);
	~EncryptFile();
	int encryptFile(int fileType);//fileType  0:caFile 1:ecdsaSignFile or gmSignFile 2:gmEnFile 3:dataFile  result 0:sussess  -1:file not exist

	bytes getNodeCrtKey();//get node signature certficate private data
	bytes getNodePrivateKey();//get node private key
	bytes getEnNodeCrtKey();//get node encrypt certificate private data
	bytes getCaCrtKey();//get ca certificate private data
	bytes getAgencyCrtKey();//get agency certificate private data
	string getDataKey();//superKey hardcode after

	static EncryptFile& getInstance(int encryptType,const string& superKey,const string& keyCenterUrl);
private:
	int encrypt(const string& _filePath,const string& _enFilePath);
	bytes decrypt(const string& _enFilePath);
	string getKeycenterResponse(const string& b64data,const string& kcUrl);
	static int writer(char* data, size_t size, size_t nmemb, std::string* writer_data);
	bool fileExist(const string& filePath);

	int encryptType;
	string superKey;
	string changeKey;
	string ivKey;
	string ivChangeKey;
	string keyCenterUrl;
	vector<string> filePath;
	vector<string> enFilePath;

	const string superKey1 = "de393b2e";
	const string superKey2 = "8310a2fd";
	const string changeKey1 = "webankit";
	const string changeKey2 = "kjsyb1!,";
	const string changeKey3 = "jcjgz2@.";
	const string superKey3 = "3b4fc15a";
	const string changeKey4 = "zlkfs3#/";
	const string superKey4 = "0dceece1";
};

