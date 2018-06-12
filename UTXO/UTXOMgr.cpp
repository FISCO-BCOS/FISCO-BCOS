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
 * @file: UTXOMgr.cpp
 * @author: chaychen
 * 
 * @date: 2018
 */

#include <cassert>

#include <libethcore/ABI.h>
#include <libethereum/Block.h>

#include "UTXOMgr.h"
#include "UTXOSharedData.h"

namespace UTXOModel
{
	UTXOMgr::UTXOMgr() 
	{
		m_UTXODBMgr = make_shared<UTXODBMgr>();
	}

	UTXOMgr::UTXOMgr(const UTXOMgr& _s):
		m_UTXODBMgr(_s.m_UTXODBMgr) 
	{
	}

	UTXOMgr& UTXOMgr::operator=(const UTXOMgr& _s)
	{
		if (&_s == this)
			return *this;

		m_UTXODBMgr = _s.m_UTXODBMgr;

		return *this;
	}

	UTXOMgr::~UTXOMgr()
	{
		m_pCurBlock = nullptr;
	}

	void UTXOMgr::registerHistoryAccount()
	{
		assert(m_UTXODBMgr);

		m_UTXODBMgr->registerHistoryAccount();
	}

	UTXOExecuteState UTXOMgr::checkInitTokens(const vector<UTXOTxOut>& txOut)
	{
		LOG(TRACE) << "UTXOMgr::checkInitTokens";

		int size = txOut.size();
		if (size > TokenMaxCnt)
		{
			UTXO_EXCEPTION_THROW("CheckInitTokens Error:TokenCntOutofRange", UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrTokenCntOutofRange);
			LOG(ERROR) << "UTXOMgr::checkInitTokens Error:" << UTXOExecuteState::TokenCntOutofRange;
			return UTXOExecuteState::TokenCntOutofRange;
		}
		for (int index = 0; index < size; index++)
   		{
			if (txOut[index].to.length() > 0 && 
				txOut[index].checkType.length() > 0 && 
				inParamsIsValid(txOut[index]))
			{
				
			}
			else
			{
				UTXO_EXCEPTION_THROW("CheckInitTokens Error:JsonParamError", UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeJsonParamError);
				LOG(ERROR) << "UTXOMgr::checkInitTokens Error:" << UTXOExecuteState::JsonParamError;
				return UTXOExecuteState::JsonParamError;
			}
		}

		return UTXOExecuteState::Success;
	}

	// Check whether the transfer address is legal
	bool UTXOMgr::accountIsValid(string checkType, string addr)
	{
		if (checkType == UTXO_OWNERSHIP_CHECK_TYPE_P2PK)
		{
			try 
			{
				jsToAddress(addr);
			}
			catch (...)
			{
				LOG(ERROR) << "UTXOMgr::accountIsValid Error 1, type:" << checkType << ",addr:" << addr;
				return false;
			}
		}
		else if (checkType == UTXO_OWNERSHIP_CHECK_TYPE_P2PKH)
		{
			if (addr.length() != 32*2+2 || 
				(addr.substr(0,2) != "0x" && addr.substr(0,2) != "0X"))
			{
				LOG(ERROR) << "UTXOMgr::accountIsValid Error 2, type:" << checkType << ",addr:" << addr;
				return false;
			}
		}

		return true;
	}

	// Only P2PKH and P2PK are used for ownership verification
	bool UTXOMgr::checkTypeIsValid(string str)
	{
		bool ret = (str == UTXO_OWNERSHIP_CHECK_TYPE_P2PKH || str == UTXO_OWNERSHIP_CHECK_TYPE_P2PK);
		if (!ret) 
		{
			LOG(ERROR) << "UTXOMgr::checkTypeIsValid Error, str:" << str << ",now only for P2PK and P2PKH";
		}
		return ret;
	}

	bool UTXOMgr::inParamsIsValid(const UTXOTxOut& txOut)
	{
		if (!accountIsValid(txOut.checkType, txOut.to) || 
			txOut.value == 0 || 
			!checkTypeIsValid(txOut.checkType)) 		
		{
			UTXO_EXCEPTION_THROW("CheckInitTokens Error:JsonParamError", UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeJsonParamError);
			LOG(ERROR) << "UTXOMgr::checkInitTokens Error:" << UTXOExecuteState::JsonParamError;
		}
		return true;
	}

	UTXOExecuteState UTXOMgr::initTokens(h256 txHash, Address sender, const vector<UTXOTxOut>& txOut)
	{
		LOG(TRACE) << "UTXOMgr::initTokens";

		assert(m_UTXODBMgr);

		// Perform pre-transaction validation
		// Throw an exception if validation fails
		UTXOExecuteState ret = checkInitTokens(txOut);
		if (UTXOExecuteState::Success != ret)
		{
			return ret;
		}

		string txKey = toJS(txHash);
		vector<string> inTokenKeys;
		vector<string> outTokenKeys;

		int size = txOut.size();
		for (int index = 0; index < size; index++)
   		{
			string strKey = txKey  + "_" + to_string(index);
			LOG(TRACE) << "UTXOMgr::initTokens new Token[" << index << "] key:" << strKey;
			outTokenKeys.push_back(strKey);

			Address initContract = txOut[index].initContract;
			string initFuncAndParams = txOut[index].initFuncAndParams;
			Address validationContract = txOut[index].validationContract;
			string contractType = "";
			if (validationContract != Address(0))
			{
				contractType = UTXO_CONTRACT_TYPE_GENERAL;
			}
			else if (initContract != Address(0) && 
					 initFuncAndParams.length() > 0)
			{
				contractType = UTXO_CONTRACT_TYPE_CASEBASED;
			}
			if (validationContract == Address(0) && 
				initContract != Address(0) && 
				initFuncAndParams.length() > 0) 
			{
				setLogic(sender, initContract, initFuncAndParams, validationContract);
			}

			TokenBase tokenBase(txHash, 
								jsToU256(to_string(index)), 
								txOut[index].value,
								txOut[index].to, 
								txOut[index].checkType, 
								validationContract,
								contractType);
			TokenExt tokenExt(TokenStateInit, txOut[index].detail);

			h256 toHash = tokenBase.getOwnerHash();
			m_UTXODBMgr->addToken(strKey, tokenBase, tokenExt, true);
			Vault vault(toHash, strKey);
			m_UTXODBMgr->addVault(vault, false);

			UTXOSharedData::getInstance()->clearTokenMapForGetVault();
			UTXOSharedData::getInstance()->clearTokenMapForSelectTokens();
		}

		UTXOTx tx(inTokenKeys, outTokenKeys);
		m_UTXODBMgr->addTx(txKey, tx);

		LOG(TRACE) << "UTXOMgr::initTokens Success";
		return UTXOExecuteState::Success;
	}

	UTXOExecuteState UTXOMgr::checkSendTokens(Address sender, const vector<UTXOTxIn>& txIn, const vector<UTXOTxOut>& txOut, bool bCurBlock)
	{
		LOG(TRACE) << "UTXOMgr::checkSendTokens";

		assert(m_UTXODBMgr);

		if (!bCurBlock)
		{
			m_UTXODBMgr->clearDBRecord();
		}

		u256 inTotalValue = 0;
		u256 outTotalValue = 0;

		int size1 = txIn.size();
		if (size1 > TokenMaxCnt)
		{
			UTXO_EXCEPTION_THROW("CheckSendTokens Error:TokenCntOutofRange", UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrTokenCntOutofRange);
			LOG(ERROR) << "UTXOMgr::checkSendTokens Error:" << UTXOExecuteState::TokenCntOutofRange;
			return UTXOExecuteState::TokenCntOutofRange;
		}
		for (int index = 0; index < size1; index++)
   		{
			string tokenKey = txIn[index].tokenKey;
			Token token;
			if (m_UTXODBMgr->getToken(tokenKey, token) == false)
			{
				UTXO_EXCEPTION_THROW("CheckSendTokens Error:TokenIDInvalid, TokenKey="+tokenKey, UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenIDInvalid);
				LOG(ERROR) << "UTXOMgr::checkSendTokens Error:" << UTXOExecuteState::TokenIDInvalid;
				return UTXOExecuteState::TokenIDInvalid;				
			}
			if (token.m_tokenExt.getTokenState() == TokenStateUsed)
			{
				UTXO_EXCEPTION_THROW("CheckSendTokens Error:TokenUsed, TokenKey="+tokenKey, UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenUsed);
				LOG(ERROR) << "UTXOMgr::checkSendTokens Error:" << UTXOExecuteState::TokenUsed;
				return UTXOExecuteState::TokenUsed;
			}

			if (!checkTokenOwnerShip(sender, token))
			{
				UTXO_EXCEPTION_THROW("CheckSendTokens Error:TokenOwnerShipCheckFail, TokenKey="+tokenKey, UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenOwnerShipCheckFail);
				LOG(ERROR) << "UTXOMgr::checkSendTokens Error:" << UTXOExecuteState::TokenOwnerShipCheckFail;
				return UTXOExecuteState::TokenOwnerShipCheckFail;
			}
			if (token.m_tokenBase.getValidationContract() != Address())
			{
				if (!checkTokenLogic(token, sender, txIn[index].callFuncAndParams, bCurBlock))
				{
					UTXO_EXCEPTION_THROW("CheckSendTokens Error:TokenLogicCheckFail, TokenKey="+tokenKey, UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenLogicCheckFail);
					LOG(ERROR) << "UTXOMgr::checkSendTokens Error:" << UTXOExecuteState::TokenLogicCheckFail;
					return UTXOExecuteState::TokenLogicCheckFail;
				}
			}
			inTotalValue += token.m_tokenBase.getValue();
		}

		int size2 = txOut.size();
		if (size2 > TokenMaxCnt)
		{
			UTXO_EXCEPTION_THROW("CheckSendTokens Error:TokenCntOutofRange", UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrTokenCntOutofRange);
			LOG(ERROR) << "UTXOMgr::checkSendTokens Error:" << UTXOExecuteState::TokenCntOutofRange;
			return UTXOExecuteState::TokenCntOutofRange;
		}
		for (int index = 0; index < size2; index++)
   		{
			if (txOut[index].to.length() > 0 && 
				txOut[index].checkType.length() > 0 && 
				inParamsIsValid(txOut[index]))
			{
				outTotalValue += txOut[index].value;
			}
			else
			{
				UTXO_EXCEPTION_THROW("CheckSendTokens Error:JsonParamError", UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeJsonParamError);
				LOG(ERROR) << "UTXOMgr::checkSendTokens Error:" << UTXOExecuteState::JsonParamError;
				return UTXOExecuteState::JsonParamError;
			}
		}

		if (inTotalValue != outTotalValue)
		{
			UTXO_EXCEPTION_THROW("CheckSendTokens Error:TokenAccountingBalanceFail, inValue="+toJS(inTotalValue)+",outValue="+toJS(outTotalValue), UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenAccountingBalanceFail);
			LOG(ERROR) << "UTXOMgr::checkSendTokens Error:" << UTXOExecuteState::TokenAccountingBalanceFail;
			return UTXOExecuteState::TokenAccountingBalanceFail;
		}
		return UTXOExecuteState::Success;
	}

	UTXOExecuteState UTXOMgr::sendSelectedTokens(h256 txHash, Address sender, const vector<UTXOTxIn>& txIn, const vector<UTXOTxOut>& txOut)
	{
		LOG(TRACE) << "UTXOMgr::sendSelectedTokens";

		// Perform pre-transaction validation
		// Throw an exception if validation fails
		Timer timerCheck;
		UTXOExecuteState ret = checkSendTokens(sender, txIn, txOut, true);
		if (UTXOExecuteState::Success != ret)
		{
			return ret;
		}

		string txKey = toJS(txHash);

		vector<string> inTokenKeys;
		vector<string> outTokenKeys;
		
		int size1 = txIn.size();
		for (int index = 0; index < size1; index++)
   		{
			string tokenKey = txIn[index].tokenKey;
			LOG(TRACE) << "UTXOMgr::sendSelectedTokens old Token[" << index << "] key:" << tokenKey;
			inTokenKeys.push_back(tokenKey);

			Token token;
			m_UTXODBMgr->getToken(tokenKey, token);
			TokenExt tokenExt = token.m_tokenExt;
			tokenExt.setTokenState(g_stateTransition[TokenStateInit][SendTokens]);
			tokenExt.setTokenDetail(txIn[index].detail);

			if (token.m_tokenExt.getTokenState() == TokenStateUsed)
			{
				UTXO_EXCEPTION_THROW("CheckSendTokens Error:TokenUsed, TokenKey="+tokenKey, UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenUsed);
				LOG(ERROR) << "UTXOMgr::checkSendTokens Error:" << UTXOExecuteState::TokenUsed;
				return UTXOExecuteState::TokenUsed;
			}

			if (token.m_tokenBase.getContractType() == UTXO_CONTRACT_TYPE_GENERAL)
			{
				Address to = token.m_tokenBase.getValidationContract();
				bytes inputBytes = jsToBytes(txIn[index].exeFuncAndParams);
	   			dev::eth::ExecutionResult res;
				execute(sender, to, inputBytes, false, res, true);
				bool result;
    			ContractABI().abiOut(bytesConstRef(&res.output), result);
    			LOG(TRACE) << "UTXOMgr::sendSelectedTokens execute result:" << result;
				if (result == false)
				{
					UTXO_EXCEPTION_THROW("CheckSendTokens Error:TokenLogicCheckFail, TokenKey="+tokenKey, UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenLogicCheckFail);
					LOG(ERROR) << "UTXOMgr::checkSendTokens Error:" << UTXOExecuteState::TokenLogicCheckFail;
					return UTXOExecuteState::TokenLogicCheckFail;
				}
			}

			m_UTXODBMgr->addToken(tokenKey, token.m_tokenBase, tokenExt, false);
			Vault vault(token.m_tokenBase.getOwnerHash(), tokenKey);
			m_UTXODBMgr->updateVault(vault, false);
		}
		
		int size2 = txOut.size();
		for (int index = 0; index < size2; index++)
   		{
			string strKey = txKey  + "_" + to_string(index);
			LOG(TRACE) << "UTXOMgr::sendSelectedTokens new Token[" << index << "] key:" << strKey;
			outTokenKeys.push_back(strKey);

			Address initContract = txOut[index].initContract;
			string initFuncAndParams = txOut[index].initFuncAndParams;
			Address validationContract = txOut[index].validationContract;
			string contractType = "";
			if (validationContract != Address(0))
			{
				contractType = UTXO_CONTRACT_TYPE_GENERAL;
			}
			else if (initContract != Address(0) && 
					 initFuncAndParams.length() > 0)
			{
				contractType = UTXO_CONTRACT_TYPE_CASEBASED;
			}
			if (validationContract == Address(0) && 
				initContract != Address(0) && 
				initFuncAndParams.length() > 0) 
			{
				setLogic(sender, initContract, initFuncAndParams, validationContract);
			}

			TokenBase tokenBase(txHash, 
								jsToU256(to_string(index)), 
								txOut[index].value,
								txOut[index].to, 
								txOut[index].checkType, 
								validationContract,
								contractType);
			TokenExt tokenExt(TokenStateInit, txOut[index].detail);

			h256 toHash = tokenBase.getOwnerHash();
			m_UTXODBMgr->addToken(strKey, tokenBase, tokenExt, true);
			Vault vault(toHash, strKey);
			m_UTXODBMgr->addVault(vault, false);
		}

		UTXOSharedData::getInstance()->clearTokenMapForGetVault();
		UTXOSharedData::getInstance()->clearTokenMapForSelectTokens();

		UTXOTx tx(inTokenKeys, outTokenKeys);
		m_UTXODBMgr->addTx(txKey, tx);

		LOG(TRACE) << "UTXOMgr::sendSelectedTokens Success";
		return UTXOExecuteState::Success;
	}

	pair<UTXOExecuteState, string> UTXOMgr::registerAccount(Address sender)
	{
		assert(m_UTXODBMgr);

		if (m_UTXODBMgr->accountIsRegistered(sender))
		{
			LOG(ERROR) << "UTXOMgr::registerAccount Error:" << UTXOExecuteState::AccountRegistered;
			return make_pair(UTXOExecuteState::AccountRegistered, UTXOModel::getMsgByCode(UTXOExecuteState::AccountRegistered));
		}
		else 
		{
			m_UTXODBMgr->registerAccount(toJS(sender));
			return make_pair(UTXOExecuteState::Success, UTXOModel::getMsgByCode(UTXOExecuteState::Success));
		}
	}

	pair<UTXOExecuteState, string> UTXOMgr::getTokenByKey(const string& tokenKey, string& ret)
	{
		assert(m_UTXODBMgr);

		m_UTXODBMgr->clearDBRecord();

		Token token;
		if (m_UTXODBMgr->getToken(tokenKey, token) == false)
		{
			LOG(ERROR) << "UTXOMgr::getTokenByKey Error:" << UTXOExecuteState::TokenIDInvalid;
			return make_pair(UTXOExecuteState::TokenIDInvalid, UTXOModel::getMsgByCode(UTXOExecuteState::TokenIDInvalid));			
		}

		ret = token.ToJsonString();
		return make_pair(UTXOExecuteState::Success, UTXOModel::getMsgByCode(UTXOExecuteState::Success));
	}

	pair<UTXOExecuteState, string> UTXOMgr::getTxByKey(const string& txKey, string& ret)
	{
		assert(m_UTXODBMgr);

		UTXOTx tx;
		if (m_UTXODBMgr->getTx(txKey, tx) == false)
		{
			LOG(ERROR) << "UTXOMgr::getTxByKey Error:" << UTXOExecuteState::TxIDInvalid;
			return make_pair(UTXOExecuteState::TxIDInvalid, UTXOModel::getMsgByCode(UTXOExecuteState::TxIDInvalid));	
		}

		ret = tx.ToJsonString();
		return make_pair(UTXOExecuteState::Success, UTXOModel::getMsgByCode(UTXOExecuteState::Success));
	}

	pair<UTXOExecuteState, string> UTXOMgr::getVaultByAccountByPart(Address sender, TokenState tokenState, vector<string>& tokenKeys, QueryUTXOParam& QueryUTXOParam)
	{
		assert(m_UTXODBMgr);

		m_UTXODBMgr->clearDBRecord();

		if (m_UTXODBMgr->accountIsRegistered(sender) == false)
		{
			LOG(ERROR) << "UTXOMgr::getVaultByAccount Error:" << UTXOExecuteState::AccountInvalid;
			return make_pair(UTXOExecuteState::AccountInvalid, UTXOModel::getMsgByCode(UTXOExecuteState::AccountInvalid));
		}

		h256 account = sha3(toJS(sender));
		LOG(TRACE) << "UTXOMgr::getVaultByAccountByPart account(" << toJS(sender) << "," << toJS(account) << ")";

		pair<h256, TokenState> key = make_pair(account, tokenState);
		vector<string> tokenList = UTXOSharedData::getInstance()->getTokenMapForGetVault(key);
		if (tokenList.size() == 0)
		{
			m_UTXODBMgr->getVaultByAccount(account, tokenState, tokenList);
			if (tokenList.size() == 0)
			{
				QueryUTXOParam.total = QueryUTXOParam.end = 0;
				return make_pair(UTXOExecuteState::Success, UTXOModel::getMsgByCode(UTXOExecuteState::Success));
			}
			UTXOSharedData::getInstance()->setTokenMapForGetVault(key, tokenList);
		}

		QueryUTXOParam.total = tokenList.size();
		if (QueryUTXOParam.start < QueryUTXOParam.total)
		{
			u256 i = QueryUTXOParam.start;
			u256 max = QueryUTXOParam.start + QueryUTXOParam.cnt;
			if (max > QueryUTXOParam.total) max = QueryUTXOParam.total;
			for (; i < max; i++)
			{
				tokenKeys.push_back(tokenList[(int)i]);
			}
			QueryUTXOParam.end = i - 1;
		}
		else
		{
			return make_pair(UTXOExecuteState::OtherFail, UTXOModel::getMsgByCode(UTXOExecuteState::OtherFail));
		}

		return make_pair(UTXOExecuteState::Success, UTXOModel::getMsgByCode(UTXOExecuteState::Success));
	}

	pair<UTXOExecuteState, string> UTXOMgr::selectTokensByPart(Address sender, u256 value, vector<string>& tokenKeys, QueryUTXOParam& QueryUTXOParam)
	{
		assert(m_UTXODBMgr);

		m_UTXODBMgr->clearDBRecord();

		if (m_UTXODBMgr->accountIsRegistered(sender) == false)
		{
			LOG(ERROR) << "UTXOMgr::SelectTokens Error:" << UTXOExecuteState::AccountInvalid;
			return make_pair(UTXOExecuteState::AccountInvalid, UTXOModel::getMsgByCode(UTXOExecuteState::AccountInvalid));
		}

		h256 account = sha3(toJS(sender));
		LOG(TRACE) << "UTXOMgr::SelectTokensInAccountByPart " << toJS(sender) << " " << toJS(account);
		
		pair<h256, u256> key = make_pair(account, value);
		pair<vector<string>, u256> tokenList = UTXOSharedData::getInstance()->getSelectTokensByKey(key);
		if (tokenList.first.size() == 0)
		{
			bool bRet = m_UTXODBMgr->selectTokensInAccount(account, value, tokenList.first, tokenList.second);
			if (!bRet)
			{
				QueryUTXOParam.total = QueryUTXOParam.end = 0;
				LOG(ERROR) << "UTXOMgr::SelectTokens Error:" << UTXOExecuteState::AccountBalanceInsufficient;
				return make_pair(UTXOExecuteState::AccountBalanceInsufficient, UTXOModel::getMsgByCode(UTXOExecuteState::AccountBalanceInsufficient));
			}
			UTXOSharedData::getInstance()->setSelectTokensByKey(key, tokenList);
		}

		QueryUTXOParam.total = tokenList.first.size();
		QueryUTXOParam.totalValue = tokenList.second;
		if (QueryUTXOParam.start < QueryUTXOParam.total)
		{
			u256 i = QueryUTXOParam.start;
			u256 max = QueryUTXOParam.start + QueryUTXOParam.cnt;
			if (max > QueryUTXOParam.total) max = QueryUTXOParam.total;
			for (; i < max; i++)
			{
				tokenKeys.push_back(tokenList.first[(int)i]);
			}
			QueryUTXOParam.end = i - 1;
		}
		else
		{
			return make_pair(UTXOExecuteState::OtherFail, UTXOModel::getMsgByCode(UTXOExecuteState::OtherFail));
		}

		return make_pair(UTXOExecuteState::Success, UTXOModel::getMsgByCode(UTXOExecuteState::Success));
	}

	pair<UTXOExecuteState, string> UTXOMgr::tokenTrackingByPart(const string& tokenKey, vector<string>& tokenKeys, QueryUTXOParam& QueryUTXOParam)
	{
		assert(m_UTXODBMgr);

		LOG(TRACE) << "UTXOMgr::tokenTrackingByPart";
		pair<UTXOModel::UTXOExecuteState, string> ret;
		if (txMapForTokenTracking[tokenKey].size() == 0)
		{
			ret = m_UTXODBMgr->tokenTracking(tokenKey, txMapForTokenTracking[tokenKey]);
			if (ret.first != UTXOExecuteState::Success)
			{
				QueryUTXOParam.total = QueryUTXOParam.end = 0;
				return ret;
			}
		}
		QueryUTXOParam.total = txMapForTokenTracking[tokenKey].size();
		if (QueryUTXOParam.start < QueryUTXOParam.total)
		{
			u256 i = QueryUTXOParam.start;
			u256 max = QueryUTXOParam.start + QueryUTXOParam.cnt;
			if (max > QueryUTXOParam.total) max = QueryUTXOParam.total;
			for (; i < max; i++)
			{
				tokenKeys.push_back(txMapForTokenTracking[tokenKey][(int)i]);
			}
			QueryUTXOParam.end = i - 1;
			return make_pair(UTXOExecuteState::Success, UTXOModel::getMsgByCode(UTXOExecuteState::Success));
		}
		else
		{
			return make_pair(UTXOExecuteState::OtherFail, UTXOModel::getMsgByCode(UTXOExecuteState::OtherFail));
		}
	}

	pair<UTXOExecuteState, string> UTXOMgr::getBalanceByAccount(Address account, u256& balance)
	{
		assert(m_UTXODBMgr);

		m_UTXODBMgr->clearDBRecord();

		if (m_UTXODBMgr->accountIsRegistered(account) == false)
		{
			LOG(ERROR) << "UTXOMgr::getVaultByAccount Error:" << UTXOExecuteState::AccountInvalid;
			return make_pair(UTXOExecuteState::AccountInvalid, UTXOModel::getMsgByCode(UTXOExecuteState::AccountInvalid));
		}

		m_UTXODBMgr->getBalanceByAccount(account, balance);
		return make_pair(UTXOExecuteState::Success, UTXOModel::getMsgByCode(UTXOExecuteState::Success));
	}

	void UTXOMgr::showAll()
	{
		assert(m_UTXODBMgr);

		m_UTXODBMgr->clearDBRecord();

		m_UTXODBMgr->showAllDB();
	}
	
	h256 UTXOMgr::getHash()
	{
		assert(m_UTXODBMgr);

		return m_UTXODBMgr->getHash();
	}

	void UTXOMgr::clearDBRecord()
	{
		if (m_UTXODBMgr)
		{
			m_UTXODBMgr->clearDBRecord();
		}
	}

	void UTXOMgr::commitDB()
	{
		assert(m_UTXODBMgr);

		m_UTXODBMgr->commitDB();
	}

	void UTXOMgr::setLogic(Address const& sender, Address const& to, const string& funcAndParams, Address& ret)
	{
		bytes inputBytes = jsToBytes(funcAndParams);
		dev::eth::ExecutionResult res;
		execute(sender, to, inputBytes, false, res, true);
		ContractABI().abiOut(bytesConstRef(&res.output), ret);
		LOG(TRACE) << "UTXOMgr::setLogic result:" << toJS(ret);
	}

	void UTXOMgr::execute(Address const& sender, Address const& _to, bytes const& _inputdata, bool bCheck, dev::eth::ExecutionResult& ret, bool bCurBlock)
	{
		try
		{
			u256 nonceTmp = UTXOSharedData::getInstance()->getNonce();
			pair<shared_ptr<Block>, LastHashes> preBlockInfo = UTXOSharedData::getInstance()->getPreBlockInfo();

			u256 gas;
			if (bCheck && preBlockInfo.first && !bCurBlock)
			{
				LOG(TRACE) << "UTXOMgr::execute using per block";
				gas = preBlockInfo.first->gasLimitRemaining();
			}
			else 
			{
				LOG(TRACE) << "UTXOMgr::execute using cur block";
				gas = m_pCurBlock->gasLimitRemaining();
			}
			u256 gasPrice = 100000000;
			Transaction t(0, gasPrice, gas, _to, _inputdata, nonceTmp);
			t.forceSender(sender);
			if (bCheck && preBlockInfo.first && !bCurBlock)
			{
				ret = preBlockInfo.first->executeByUTXO(preBlockInfo.second, t, Permanence::OnlyReceipt);
			}
			else 
			{
				ret = m_pCurBlock->executeByUTXO(m_curLH, t, Permanence::OnlyReceipt);
			}
		}
		catch (...)
		{
			// TODO: Some sort of notification of failure.
			LOG(ERROR) << boost::current_exception_diagnostic_information() << "\n";
			LOG(ERROR) << "UTXOMgr::execute Fail!" << toString(_inputdata);
		}
	}

	bool UTXOMgr::checkTokenOwnerShip(Address sender, const Token& token)
	{	
		if (sha3(toJS(sender)) == token.m_tokenBase.getOwnerHash()) 
		{
			return true;
		}
		return false;
	}

	bool UTXOMgr::checkTokenLogic(const Token& token, Address sender, const string& callFunAndParams, bool bCurBlock)
	{
		Address to = token.m_tokenBase.getValidationContract();
		bytes inputBytes = jsToBytes(callFunAndParams);
		dev::eth::ExecutionResult res;
		execute(sender, to, inputBytes, true, res, bCurBlock);
		bool result;
		ContractABI().abiOut(bytesConstRef(&res.output), result);
		LOG(TRACE) << "UTXOMgr::checkTokenLogic result:" << result;
		return result;
	}

	void UTXOMgr::setCurBlockInfo(dev::eth::Block* _tempblock, LastHashes const& _lh)
	{
		u256 blockNumber = _tempblock->info().number();
		LOG(TRACE) << "UTXOMgr::setBlockInfo mumber:" << blockNumber;
		m_pCurBlock = _tempblock;
		m_curLH = _lh;
		
		assert(m_UTXODBMgr);
		m_UTXODBMgr->setBlockNum(blockNumber);
	}
}
