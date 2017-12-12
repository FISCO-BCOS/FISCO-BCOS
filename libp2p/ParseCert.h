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
 * @file: ParseCert.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include "Common.h"
#include "string"
using namespace std;
class ParseCert
{
public:
	ParseCert(void);
	~ParseCert(void);
public:
	void ParseInfo(ba::ssl::verify_context& ctx);
	bool getExpire();
	string getSerialNumber();
	string getSubjectName();
	int getCertType();
private:
	int mypint( const char ** s,int n,int min,int max,int * e);
	time_t ASN1_TIME_get ( ASN1_TIME * a,int *err);
private:
	bool m_isExpire;//证书是否过期
	string m_serialNumber;//证书序列号
	int m_certType;//证书类型   CA根证书，用户证书
	string m_subjectName;//证书主题信息 0:CA证书  1:用户证书
};

