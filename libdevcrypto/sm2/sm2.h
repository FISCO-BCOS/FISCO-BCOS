/*
	This file is part of fisco-bcos.

	fisco-bcos is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	fisco-bcos is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with fisco-bcos.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: sm2.h
 * @author: websterchen
 * 
 * @date: 2018
 */
#pragma once
#include <string>
#include <iostream>
using namespace std;
#include <openssl/sm2.h>
#include <openssl/sm3.h>
class SM2{
public:
	bool genKey();
	string getPublicKey();
	string getPrivateKey();
	bool sign(const char* originalData, int originalDataLen, const string& privateKey, string& r, string& s);
	int verify(const string& signData, int signDataLen, const char* originalData, int originalDataLen, const string& publicKey);
	string priToPub(const string& privateKey);
	char* strlower(char* s);
	string ascii2hex(const char* chs,int len);
	static SM2& getInstance();
private:
	string publicKey;
	string privateKey;
};