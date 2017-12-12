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
using namespace dev::p2p;
class GenKey
{
private:
	int m_cryptoMod;//加密模式  0:不加密  1:本地加密 2:keycenter加密
	string m_superKey1;//superkey内存分段存储
	string m_kcUrl;//keycenter访问路径
	string m_superKey;//本地明文存储superkey数据
	string m_superKey2;//superkey内存分段存储
	string m_nodekeyPath;//nodekey生成路径
	string m_datakeyPath;//datakey生成路径
	string m_superKey3;//superkey内存分段存储
	string m_privatekeyPath;//证书privatekey明文存放路径
	string m_enprivatekeyPath;//证书privatekey密文存放路径

	//与KeyCenter数据交互通信密码
	string m_changeKey1;
	string m_changeKey2;
	string m_changeKey3;
	string m_changeKey4;
	string m_superKey4; //superkey内存分段存储
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

	void generateNetworkRlp(string const& sFilePath,int cryptoType,string const& keyData = "",string const& ivData = "",string const& kcUrl = "",int keyType = 1);//1:nodekey 3:datakey   鐢熸垚nodekey datakey鏂囦欢
	string getKeycenterData(string const& b64data,string const& kcUrl,int keyType);//keycenter浜や簰
	string getSuperKey();
	static int writer(char* data, size_t size, size_t nmemb, std::string* writer_data);
public:
	GenKey(void);
	~GenKey(void);
	void setKeyData();//生成nodekey   datakey 文件
	bytes getKeyData();//获取明文 nodekey datakey 文件
	void setPrivateKey();//私钥使用KeyCenter加密处理
	string getPrivateKey();//获取私钥明文数据
	string getPublicKey();
	string getCaPublicKey();
	void setCryptoMod(int cryptoMod);
	void setKcUrl(string kcUrl);
	void setSuperKey(string superKey);
	void setNodeKeyPath(string nodekeyPath);
	void setDataKeyPath(string datakeyPath);
	void setPrivateKeyPath(string privatekeyPath);
	void setEnPrivateKeyPath(string enprivatekeyPath);
};
