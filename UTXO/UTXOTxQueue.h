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
 * @file: UTXOTxQueue.cpp
 * @author: chaychen
 * 
 * @date: 2018
 */

#ifndef __UTXOTXQUENE_H__
#define __UTXOTXQUENE_H__

#include <condition_variable>
#include <thread>
#include <deque>

#include <libdevcore/Common.h>
#include <libdevcore/Guards.h>
#include <libethcore/Common.h>

#include "UTXOData.h"

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace UTXOModel
{
	class UTXOMgr;
	
	class UTXOTxQueue
	{
	public:
		UTXOTxQueue(size_t totalCnt, UTXOMgr* ptrUTXOMgr);
		~UTXOTxQueue();
		void enqueue(const Transaction& t);
		void executeUTXOTx();
		map<h256, UTXOExecuteState> getTxResult();

		template <class T> Handler<> onReady(T const& _t) { return m_onReady.add(_t); }
	private:
		condition_variable m_queueReady;
		vector<thread> m_executer;
		deque<Transaction> m_unexecuted;						// A list of parallel transactions to be executed
		mutable Mutex x_queue;
		atomic<bool> m_aborting = {false};
		size_t m_executedCnt = {0};								// The number of parallel transactions executed in the same block
		map<h256, UTXOExecuteState> mapUTXOTxResult;			// Execution results of parallel transactions
		size_t m_totalCnt;										// The number of transactions that are expected to be parallel in the same block

		Signal<> m_onReady;
		UTXOMgr* m_ptrUTXOMgr;
	};
}

#endif//__UTXODATA_H__