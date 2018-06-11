/*
	This file is part of FISCO BCOS.

	FISCO BCOS is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FISCO BCOS is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FISCO BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: UTXOData.cpp
 * @author: chaychen
 * 
 * @date: 2018
 */

#include <json/json.h>
#include <sstream>

#include <libdevcore/easylog.h>
#include <libdevcore/Exceptions.h>
#include <libethcore/Exceptions.h>

#include "UTXOData.h"

using namespace dev;
using namespace std;

namespace UTXOModel
{
	TokenBase& TokenBase::operator=(const TokenBase& _s)
	{
		if (&_s == this)
			return *this;

   		m_transactionHash = _s.m_transactionHash;
		m_idxInUTXOTx = _s.m_idxInUTXOTx;
		m_value = _s.m_value;
		m_owner = _s.m_owner;
		m_checkType = _s.m_checkType;
		m_validationContract = _s.m_validationContract;
		m_contractType = _s.m_contractType;
		return *this;
	}
	
	TokenBase::TokenBase(const bytes &_rlp)
	{
		RLP rlp(_rlp);
		try
		{
			int field = 0;
			int index = 0;
			m_transactionHash = rlp[field = (index++)].toHash<h256>();
			m_idxInUTXOTx = rlp[field = (index++)].toInt<u256>();
			m_value = rlp[field = (index++)].toInt<u256>();
			m_owner = rlp[field = (index++)].toString();
			m_checkType = rlp[field = (index++)].toString();
			auto tempRLP = rlp[field = (index++)];
			m_validationContract = tempRLP.isEmpty() ? Address() : tempRLP.toHash<Address>(RLP::VeryStrict);
			m_contractType = rlp[field = (index++)].toString();
		}	
		catch (Exception& _e)
		{
			_e << eth::errinfo_name("invalid token_base format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
			throw;
		}
	}
	
	void TokenBase::streamRLP(RLPStream& _s) const 
	{
		_s.appendList(7);
		_s << m_transactionHash << m_idxInUTXOTx << m_value << m_owner << m_checkType << m_validationContract << m_contractType;
	}
		
	bytes TokenBase::rlp() const 
	{ 
		RLPStream s; 
		streamRLP(s); 
		return s.out(); 
	}

	void TokenBase::Print() const 
	{
		stringstream str;
		str << "TokenBase------";
		str << "TxHash:" << toJS(m_transactionHash);
		str << ",index:" << m_idxInUTXOTx;
		str << ",value:" << m_value;
		str << ",owner:" << m_owner;
		str << ",checkType:" << m_checkType;
		str << ",validationContract:" << toJS(m_validationContract);
		str << ",contractType:" << m_contractType;
		LOG(TRACE) << str.str();
	}

	string TokenBase::ToJsonString() const
	{
		Json::Value root;
		root["TxHash"] = toJS(m_transactionHash);
		root["index"] = toJS(m_idxInUTXOTx);
		root["value"] = toJS(m_value);
		root["owner"] = m_owner;
		root["checkType"] = m_checkType;
		root["validationContract"] = toJS(m_validationContract);
		root["contractType"] = m_contractType;

		Json::FastWriter writer;  
		return writer.write(root);  
	}
	
	TokenExt& TokenExt::operator=(const TokenExt& _s)
	{
		if (&_s == this)
			return *this;

   		m_tokenState = _s.m_tokenState;
		m_tokenDetail = _s.m_tokenDetail;
		return *this;
	}

	TokenExt::TokenExt(const bytes &_rlp)
	{
		RLP rlp(_rlp);
		try
		{
			int field = 0;
			int index = 0;
			m_tokenState = (TokenState)rlp[field = (index++)].toInt();
			m_tokenDetail = rlp[field = (index++)].toString();
		}
		catch (Exception& _e)
		{
			_e << eth::errinfo_name("invalid token_ext format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
			throw;
		}
	}

	void TokenExt::streamRLP(RLPStream& _s) const 
	{
		_s.appendList(2);
		_s << (u256)m_tokenState << m_tokenDetail;
	}
		
	bytes TokenExt::rlp() const 
	{ 
		RLPStream s; 
		streamRLP(s); 
		return s.out(); 
	}

	void TokenExt::Print() const 
	{
		stringstream str;
		str << "TokenExt------";
		str << ",tokenState:" << toJS(m_tokenState);
		str << ",tokenDetail:" << m_tokenDetail;
		LOG(TRACE) << str.str();
	}

	string TokenExt::ToJsonString() const
	{
		Json::Value root;
		root["state"] = toJS(m_tokenState);
		root["detail"] = m_tokenDetail;

		Json::FastWriter writer;  
		return writer.write(root);  
	}

	TokenExtOnBlockNum& TokenExtOnBlockNum::operator=(const TokenExtOnBlockNum& _s)
	{
		if (&_s == this)
			return *this;

   		m_updatedBlockNum = _s.m_updatedBlockNum;
		return *this;
	}

	TokenExtOnBlockNum::TokenExtOnBlockNum(const bytes &_rlp)
	{
		RLP rlp(_rlp);
		try
		{
			int field = 0;
			int index = 0;
			m_updatedBlockNum = rlp[field = (index++)].toVector<u256>();
		}
		catch (Exception& _e)
		{
			_e << eth::errinfo_name("invalid token_num format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
			throw;
		}
	}

	void TokenExtOnBlockNum::streamRLP(RLPStream& _s) const 
	{
		_s.appendList(1) << m_updatedBlockNum;
	}
		
	bytes TokenExtOnBlockNum::rlp() const 
	{ 
		RLPStream s; 
		streamRLP(s); 
		return s.out(); 
	}

	void TokenExtOnBlockNum::Print() const
	{
		string str;
		for (size_t i = 0; i < m_updatedBlockNum.size(); i++)
		{
			str += toJS(m_updatedBlockNum[i]);
			str += ",";
		}
		LOG(TRACE) << "TokenExtOnBlockNum------List Size:" << m_updatedBlockNum.size() << ", content:" << str;
	}
	
	Token& Token::operator=(const Token& _s)
	{
		if (&_s == this)
        	return *this;

   		m_tokenBase = _s.m_tokenBase;
		m_tokenExt = _s.m_tokenExt;
		return *this;
	}

	string Token::ToJsonString() const
	{
		Json::Value root;
		root["TxHash"] = toJS(m_tokenBase.getTransactionHash());
		root["index"] = (int)m_tokenBase.getIdxInUTXOTx();
		root["value"] = (int)m_tokenBase.getValue();
		root["owner"] = m_tokenBase.getOwner();
		root["checkType"] = m_tokenBase.getCheckType();
		root["validationContract"] = toJS(m_tokenBase.getValidationContract());
		root["state"] = (int)m_tokenExt.getTokenState();
		root["detail"] = m_tokenExt.getTokenDetail();
		root["contractType"] = m_tokenBase.getContractType();
		Json::FastWriter writer;  
		return writer.write(root);  
	}

	UTXOTx::UTXOTx(const bytes &_rlp) 
	{
		RLP rlp(_rlp);
		try
		{
			int field = 0;
			int index = 0;
			m_inTokenKey = rlp[field = (index++)].toVector<string>();
			m_outTokenKey = rlp[field = (index++)].toVector<string>();
		}
		catch (Exception& _e)
		{
			_e << eth::errinfo_name("invalid tx format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
			throw;
		}
	}

	void UTXOTx::streamRLP(RLPStream& _s) const 
	{
		_s.appendList(2);
		_s.appendVector(m_inTokenKey);
		_s.appendVector(m_outTokenKey);
	}
		
	bytes UTXOTx::rlp() const 
	{ 
		RLPStream s; 
		streamRLP(s); 
		return s.out(); 
	}

	void UTXOTx::Print() const
	{
		stringstream str;
		str << "UTXOTx------";
		str << ",InToken List Size:" << m_inTokenKey.size();
		for (const string& key: m_inTokenKey) { str << key << ","; }
		str << ",OutToken List Size:" << m_outTokenKey.size();
		for (const string& key: m_outTokenKey) { str << key << ","; }
		LOG(TRACE) << str.str();
	}

	string UTXOTx::ToJsonString() const
	{
		Json::Value root;
		for (const string& key: m_inTokenKey) { root["in"].append(key); }
		for (const string& key: m_outTokenKey) { root["out"].append(key); }

		Json::FastWriter writer;  
		return writer.write(root);  
	}
	
	Vault::Vault(const bytes &_rlp) 
	{
		RLP rlp(_rlp);
		try
		{
			int field = 0;
			int index = 0;
			m_ownerHash = rlp[field = (index++)].toHash<h256>();
			m_tokenKey = rlp[field = (index++)].toString();
		}
		catch (Exception& _e)
		{
			_e << eth::errinfo_name("invalid vault format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
			throw;
		}
	}

	void Vault::streamRLP(RLPStream& _s) const 
	{
		_s.appendList(2);
		_s << m_ownerHash << m_tokenKey;
	}
		
	bytes Vault::rlp() const 
	{ 
		RLPStream s; 
		streamRLP(s); 
		return s.out(); 
	}

	void Vault::Print() const 
	{
		stringstream str;
		str << "Vault------" << toJS(m_ownerHash) << "," << m_tokenKey;
		LOG(TRACE) << str.str();
	}

	string Vault::ToJsonString() const
	{
		Json::Value root;
		root["ownerHash"] = toJS(m_ownerHash);
		root["tokenKey"] = m_tokenKey;
		
		Json::FastWriter writer;  
		return writer.write(root);  
	}

	AccountRecord& AccountRecord::operator=(const AccountRecord& _s)
	{
		if (&_s == this)
			return *this;

   		m_accountList = _s.m_accountList;
		return *this;
	}

	AccountRecord::AccountRecord(const bytes &_rlp)
	{
		RLP rlp(_rlp);
		try
		{
			int field = 0;
			int index = 0;
			m_accountList = rlp[field = (index++)].toVector<h256>();
		}
		catch (Exception& _e)
		{
			_e << eth::errinfo_name("invalid account_list format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
			throw;
		}
	}

	void AccountRecord::streamRLP(RLPStream& _s) const 
	{
		_s.appendList(1) << m_accountList;
	}
		
	bytes AccountRecord::rlp() const 
	{ 
		RLPStream s; 
		streamRLP(s); 
		return s.out(); 
	}

	void AccountRecord::Print() const
	{
		string str;
		for (size_t i = 0; i < m_accountList.size(); i++)
		{
			str += toJS(m_accountList[i]);
			str += ",";
		}
		LOG(TRACE) << "AccountRecord------List Size:" << m_accountList.size() << ", content:" << str;
	}
}//namespace UTXOModel
