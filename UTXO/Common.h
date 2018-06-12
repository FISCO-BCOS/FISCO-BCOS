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
 * @file: Common.h
 * @author: chaychen
 * 
 * @date: 2018
 */

#ifndef __COMMON_H__
#define __COMMON_H__

#include <string>

#include <libdevcore/CommonJS.h>

using namespace std;

namespace UTXOModel
{
	// The result of UTXO transaction
	enum UTXOExecuteState {
		Success = 0,									// Success.
		TokenIDInvalid,									// Token ID is invalid.
		TxIDInvalid,									// Tx ID is invalid.
		AccountInvalid,									// Account is invalid.
		TokenUsed,										// Token has been used.
		TokenOwnerShipCheckFail,						// The ownership validation of token does not pass through.
		TokenLogicCheckFail,							// The logical validation of token does not pass through.
		TokenAccountingBalanceFail,						// The accounting equation verification of transaction does not pass through.
		AccountBalanceInsufficient,						// The balance of the account is insufficient.
		JsonParamError,									// Json parameter formatting error.
		UTXOTypeInvalid,								// UTXO transaction type error.
		AccountRegistered,								// Account has been registered.
		TokenCntOutofRange,								// The number of Token numbers used in the transaction is beyond the limit(max=1000).
		LowEthVersion,									// Please upgrade the environment for UTXO transaction.
		OtherFail,										// Other Fail.
		StateCnt										// This type must be put in the end.
	};

	enum TokenState {
		TokenStateInValid = 0,
		TokenStateInit,
		TokenStateUsed,
		TokenStateCnt					// This type must be put in the end.
	};

	// Token state transition function
	enum StateTransitionFunc {
		SendTokens = 0,					// TokenStateInit->TokenStateUsed
		StateTransitionFuncCnt			// This type must be put in the end.
	};

	// Token state transition matrix
	static TokenState g_stateTransition[TokenStateCnt][StateTransitionFuncCnt] = {
		{TokenStateInValid},
		{TokenStateUsed},
		{TokenStateInValid}
	};

	// Consistent data between nodes is stored in a DB, 
	// where different data are distinguished by different prefixes.
	const string TOKEN_BASE_KEY_PERFIX = "TB";			// TokenBase perfix
	const string TOKEN_EXT_KEY_PERFIX = "TE";			// TokenExt perfix
	const string TOKEN_NUM_KEY_PERFIX = "TN";			// TokenExtOnBlockNum perfix
	const string TX_KEY_PERFIX = "TX";					// UTXOTx perfix
	const string PERFIX = "#";
	// Obtaining key from different data
	#define GET_TOKEN_BASE_KEY(tokenKey) (TOKEN_BASE_KEY_PERFIX+PERFIX+tokenKey)
	#define GET_TOKEN_EXT_KEY(tokenKey,blockNum) (TOKEN_EXT_KEY_PERFIX+PERFIX+tokenKey+PERFIX+blockNum)
	#define GET_TOKEN_NUM_KEY(tokenKey) (TOKEN_NUM_KEY_PERFIX+PERFIX+tokenKey)
	#define GET_TX_KEY(txKey) (TX_KEY_PERFIX+PERFIX+txKey)
	// TokenBase:TokenExt:TokenExtOnBlockNum = 1:N:1
	// Inconsistent data between nodes is stored in another DB.
	// Consistency check in BlockHeader does not include this data.
	const string VAULT_KEY_PERFIX = "VT";
	#define GET_VAULT_KEY(vaultKey) (VAULT_KEY_PERFIX+PERFIX+vaultKey)
	const string VAULT_KEY_IN_EXTRA = "VK";							// Registered accounts was stored in extradb.

	const string UTXO_OWNERSHIP_CHECK_TYPE_P2PK = "P2PK";			// The type of ownership validation
	const string UTXO_OWNERSHIP_CHECK_TYPE_P2PKH = "P2PKH";			// The type of ownership validation
	const string UTXO_CONTRACT_TYPE_GENERAL = "General";			// The type of logical validation
	const string UTXO_CONTRACT_TYPE_CASEBASED  = "CaseBased";		// The type of logical validation

	const dev::u256 TokenMaxCnt = 1000;								// The maximum number of using tokens
	
	static string UTXOExecuteMsg[UTXOExecuteState::StateCnt] = { 
		"Success.",
		"Token ID is invalid.",
		"Tx ID is invalid.",
		"Account is invalid.",
		"Token has been used.",
		"The ownership validation of token does not pass through.",
		"The logical validation of token does not pass through.",
		"The accounting equation verification of transaction does not pass through.",
		"The balance of the account is insufficient.",
		"Json parameter formatting error.",
		"UTXO transaction type error.",
		"Account has been registered.",
		"The number of Token numbers used in the transaction is beyond the limit(max=1000).",
		"Please upgrade the environment for UTXO transaction.",
		"Other Fail."
	};

	string getMsgByCode(UTXOExecuteState code);
}//namespace UTXOModel

#endif//__COMMON_H__