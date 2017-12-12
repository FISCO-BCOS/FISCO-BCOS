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
 * @file: CryptoParam.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include <iostream>
using namespace std;
class CryptoParam
{
public:
	int m_cryptoMod;//加密模式  0:不加密  1:本地加密 2:KeyCenter加密
	string m_kcUrl;//keycenter访问路径
	string m_superKey;//本地明文存储superkey数据
	string m_nodekeyPath;//nodekey生成路径
	string m_datakeyPath;//datakey生成路径
	string m_privatekeyPath;//证书私钥明文存放路径
	string m_enprivatekeyPath;//证书私钥密文存放路径

	//访问keycenter返回id?
	int m_ID; //ID
	int m_errCode; //错误码
	string m_resData; //返回消息
public:
	CryptoParam(void);
	~CryptoParam(void);
	CryptoParam loadDataCryptoConfig(std::string const& filePath) const;
	CryptoParam loadKeyCenterReq(std::string const& _json) const;
};
