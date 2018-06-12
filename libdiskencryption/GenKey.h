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
 * @file: GenKey.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include <iostream>
#include <libdevcore/Common.h>
#include <libdevcore/OverlayDB.h>
#include <libp2p/Common.h>
using namespace std;
using namespace dev;
class GenKey
{
private:
	int m_cryptoMod;//cryptomod  0:not encrypt  1:local encrypt 2:KeyCenter encrypt
	string m_superKey1;//
	string m_kcUrl;//keycenter url
	string m_superKey;//local encrypt superkey
	string m_superKey2;//superkey subsection memory storage 
	string m_nodekeyPath;//nodekey create path
	string m_datakeyPath;//datakey create path
	string m_superKey3;//superkey subsection memory storage 
	string m_privatekeyPath;//certificate private path
	string m_enprivatekeyPath;//certificate private encrypt path

	//KeyCenter channel password
	string m_changeKey1;
	string m_changeKey2;
	string m_changeKey3;
	string m_changeKey4;
	string m_superKey4; //superkey subsection memory storage
	enum CRYPTOTYPE
	{
		CRYPTO_DEFAULT = 0,
		CRYPTO_LOCAL,
		CRYPTO_KEYCENTER
	};

	enum KEYTYPE
	{
		KEY_NODE = 1,
		KEY_ACCOUNT,
		KEY_DATA
	};

	void generateNetworkRlp(string const& sFilePath,int cryptoType,string const& keyData = "",string const& ivData = "",string const& kcUrl = "",int keyType = 1);//1:nodekey 3:datakey
	string getKeycenterData(string const& b64data,string const& kcUrl,int keyType);//keycenter response
	string getSuperKey();
	static int writer(char* data, size_t size, size_t nmemb, std::string* writer_data);

	void genEcdsaKey(string const& sFilePath,int cryptoType,string const& keyData,string const& ivData,string const& kcUrl,int keyType);
	void genGmKey(string const& sFilePath,int cryptoType,string const& keyData,string const& ivData,string const& kcUrl,int keyType);

	bytes getEcdsaKey();
	bytes getGmKey();
public:
	GenKey(void);
	~GenKey(void);
	void setKeyData();//create nodekey   datakey file
	bytes getKeyData();//get nodekey datakey plaintext
	void setCryptoMod(int cryptoMod);
	void setKcUrl(string kcUrl);
	void setSuperKey(string superKey);
	void setNodeKeyPath(string nodekeyPath);
	void setDataKeyPath(string datakeyPath);
};
