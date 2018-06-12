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
 * @file: CryptoParam.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

 #include "CryptoParam.h"
#include <curl/curl.h>
#include <libdevcore/FileSystem.h>
#include <json_spirit/JsonSpiritHeaders.h>
#include <libdevcore/CommonIO.h>
namespace js = json_spirit;

CryptoParam::CryptoParam(void)
{
	m_cryptoMod = 0;
	m_kcUrl = "https:\\127.0.0.1";
	m_superKey = "123";
	m_nodekeyPath = "./network.rlp";
	m_datakeyPath = "./datakey";
}

CryptoParam::~CryptoParam(void)
{

}

CryptoParam CryptoParam::loadDataCryptoConfig(std::string const& filePath) const
{
	CryptoParam cp(*this);
	js::mValue val;
	string _json = dev::contentsString(filePath);
	json_spirit::read_string(_json, val);
	js::mObject obj = val.get_obj();

	//set cryptomod
	cp.m_cryptoMod = obj.count("cryptomod") ? std::stoi(obj["cryptomod"].get_str()) : 0;
	cp.m_kcUrl = obj.count("keycenterurl") ? obj["keycenterurl"].get_str() : "https://127.0.0.1";
	cp.m_nodekeyPath = obj.count("rlpcreatepath") ? obj["rlpcreatepath"].get_str() : "None";
	cp.m_superKey = obj.count("superkey") ? obj["superkey"].get_str() : "";
	cp.m_datakeyPath = obj.count("datakeycreatepath") ? obj["datakeycreatepath"].get_str() : "None";
	return cp;
}

CryptoParam CryptoParam::loadKeyCenterReq(std::string const& _json) const
{
	CryptoParam cp(*this);
	js::mValue val;
	json_spirit::read_string(_json, val);
	js::mObject obj = val.get_obj();

	//call keycenter result 
	cp.m_ID = obj.count("id") ? obj["id"].get_int() : 0;
	cp.m_errCode = obj.count("errcode") ? obj["errcode"].get_int() : 0;
	cp.m_resData = obj.count("resdata") ? obj["resdata"].get_str() : "";
	return cp;
}