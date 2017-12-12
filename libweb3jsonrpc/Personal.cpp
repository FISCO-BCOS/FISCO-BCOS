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
 * @file: Personal.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <jsonrpccpp/common/exception.h>
#include <libethcore/KeyManager.h>
#include <libweb3jsonrpc/AccountHolder.h>
#include <libethcore/CommonJS.h>
#include <libweb3jsonrpc/JsonHelper.h>
#include <libethereum/Client.h>

#include "Personal.h"

using namespace std;
using namespace dev;
using namespace dev::rpc;
using namespace dev::eth;
using namespace jsonrpc;

Personal::Personal(KeyManager& _keyManager, AccountHolder& _accountHolder, eth::Interface& _eth):
	m_keyManager(_keyManager),
	m_accountHolder(_accountHolder),
	m_eth(_eth)
{
}

std::string Personal::personal_newAccount(std::string const& _password)
{
	KeyPair p = KeyManager::newKeyPair(KeyManager::NewKeyType::NoVanity);
	m_keyManager.import(p.secret(), std::string(), _password, std::string());
	return toJS(p.address());
}

string Personal::personal_sendTransaction(Json::Value const& _transaction, string const& _password)
{
	TransactionSkeleton t;
	try
	{
		t = toTransactionSkeleton(_transaction);
	}
	catch (...)
	{
		BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
		return string();
	}

	if (Secret s = m_keyManager.secret(t.from, [&](){ return _password; }, false))
	{
		// return the tx hash
		return toJS(m_eth.submitTransaction(t, s).first);
	}
	else
		BOOST_THROW_EXCEPTION(JsonRpcException("Invalid password or account."));
	return string();
}

string Personal::personal_signAndSendTransaction(Json::Value const& _transaction, string const& _password)
{
	return personal_sendTransaction(_transaction, _password);
}

bool Personal::personal_unlockAccount(std::string const& _address, std::string const& _password, int _duration)
{
	return m_accountHolder.unlockAccount(Address(fromHex(_address, WhenError::Throw)), _password, _duration);
}

Json::Value Personal::personal_listAccounts()
{
	return toJson(m_keyManager.accounts());
}
