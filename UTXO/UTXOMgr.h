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
 * @file: UTXOMgr.h
 * @author: chaychen
 * 
 * @date: 2017
 */

#ifndef __UTXOMGR_H__
#define __UTXOMGR_H__

#include <libethcore/CommonJS.h>
#include <libethcore/Common.h>
#include <libevm/ExtVMFace.h>

#include "UTXODBMgr.h"
#include "UTXOExp.h"

using namespace dev;
using namespace dev::eth;
using namespace std;

namespace dev 
{
	namespace eth 
	{
		class Block;
		struct ExecutionResult;
	}
}

namespace UTXOModel
{
	class UTXOMgr
	{
	public:
		UTXOMgr();
		UTXOMgr(const UTXOMgr& _s);
		UTXOMgr& operator=(const UTXOMgr& _s);
		~UTXOMgr();
		
		// Load registered accounts and build caching when the program starts.
		void registerHistoryAccount();

		// Verify transactions and execute transactions.
		UTXOExecuteState checkInitTokens(const vector<UTXOTxOut>& txOut);
		UTXOExecuteState initTokens(h256 txHash, Address sender, const vector<UTXOTxOut>& txOut);
		UTXOExecuteState checkSendTokens(Address sender, const vector<UTXOTxIn>& txIn, const vector<UTXOTxOut>& txOut, bool bCurBlock = false);
		UTXOExecuteState sendSelectedTokens(h256 txHash, Address sender, const vector<UTXOTxIn>& txIn, const vector<UTXOTxOut>& txOut);

		// Query transactions
		pair<UTXOExecuteState, string> getTokenByKey(const string& tokenKey, string& ret);
		pair<UTXOExecuteState, string> getTxByKey(const string& txKey, string& ret);
		pair<UTXOExecuteState, string> getVaultByAccountByPart(Address account, TokenState tokenState, vector<string>& tokenKeys, QueryUTXOParam& QueryUTXOParam);
		pair<UTXOExecuteState, string> registerAccount(Address sender);
		pair<UTXOExecuteState, string> selectTokensByPart(Address account, u256 value, vector<string>& tokenKeys, QueryUTXOParam& QueryUTXOParam);
		pair<UTXOExecuteState, string> tokenTrackingByPart(const string& tokenKey, vector<string>& tokenKeys, QueryUTXOParam& QueryUTXOParam);
		pair<UTXOExecuteState, string> getBalanceByAccount(Address account, u256& balance);

		// Used for test
		void showAll();

		// Calculate the hash of the data used for consistency checking.
		h256 getHash();
		// Clear the hash of the data used for consistency checking.
		void clearDBRecord();
		
		void commitDB();
		
		void setCurBlockInfo(dev::eth::Block* _tempblock, LastHashes const& _lh);

	private:
		shared_ptr<UTXODBMgr> m_UTXODBMgr;

		// Execute the constructed transaction.
		void execute(Address const& sender, Address const& _to, bytes const& _inputdata, bool bCheck, dev::eth::ExecutionResult& ret, bool bCurBlock = false);

		// Set up the validation logic.
		void setLogic(Address const& sender, Address const& to, const string& funcAndParams, Address& ret);
		
		// Ownership check
		bool checkTokenOwnerShip(Address sender, const Token& token);
		// logic check
		bool checkTokenLogic(const Token& token, Address sender, const string& callFunAndParams, bool bCurBlock = false);

		bool inParamsIsValid(const UTXOTxOut& txOut);
		bool accountIsValid(string checkType, string addr);
		bool checkTypeIsValid(string str);

		map<string, vector<string>> txMapForTokenTracking;		// The cache of TokenTracking interface

		// Current block information
		dev::eth::Block* m_pCurBlock;
		LastHashes m_curLH;
	};
}

#endif // __UTXOMGR_H__
