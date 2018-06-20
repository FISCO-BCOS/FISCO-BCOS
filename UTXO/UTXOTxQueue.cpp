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


#include <libdevcore/easylog.h>
#include <libethereum/Transaction.h>

#include "UTXOExp.h"
#include "UTXOTxQueue.h"

namespace UTXOModel
{
	UTXOTxQueue::UTXOTxQueue(size_t totalCnt, UTXOMgr* ptrUTXOMgr)
	{
		LOG(TRACE) << "UTXOTxQueue::UTXOTxQueue()";
		m_totalCnt = totalCnt;
		if (0 == totalCnt)
		{
			return;
		}
		m_ptrUTXOMgr = ptrUTXOMgr;
		unsigned verifierThreads = std::max(thread::hardware_concurrency(), 3U) - 2U;
		for (unsigned i = 0; i < verifierThreads; ++i)
			m_executer.emplace_back([ = ]() {
			pthread_setThreadName("executer_" + toString(i));
			this->executeUTXOTx();
		});
		LOG(TRACE) << "UTXOTxQueue::UTXOTxQueue() init thread end, ThreadCnt=" << verifierThreads << ", TxCnt:" << m_totalCnt;
	}

	UTXOTxQueue::~UTXOTxQueue()
	{
		m_aborting = true;
		m_queueReady.notify_all();
		for (auto& i : m_executer)
			i.join();

		m_executer.clear();
		m_ptrUTXOMgr = nullptr;
	}

	void UTXOTxQueue::enqueue(const Transaction& t)
	{
		{
			Guard l(x_queue);
			m_unexecuted.emplace_back(t);
		}
		m_queueReady.notify_all();
	}

	void UTXOTxQueue::executeUTXOTx()
	{
		while (!m_aborting)
		{
			Transaction work;

			{
				unique_lock<Mutex> l(x_queue);
				m_queueReady.wait(l, [&]() { return !m_unexecuted.empty() || m_aborting; });
				if (m_aborting)
					return;
				work = move(m_unexecuted.front());
				m_unexecuted.pop_front();
			}

			UTXOType utxoType = work.getUTXOType();
			if (utxoType == UTXOType::InitTokens)
			{
				try 
				{
					UTXOExecuteState ret = m_ptrUTXOMgr->initTokens(work.sha3(), work.sender(), work.getUTXOTxOut());
					{
						Guard l(x_queue);
						mapUTXOTxResult[work.sha3()] = ret;
					}
				}
				catch (UTXOException& e)
				{
					LOG(ERROR) << "InitTokens Error:" << e.what();
				}
			}
			else if (utxoType == UTXOType::SendSelectedTokens)
			{
				try
				{
					UTXOExecuteState ret = m_ptrUTXOMgr->sendSelectedTokens(work.sha3(), work.sender(), work.getUTXOTxIn(), work.getUTXOTxOut());
					{
						Guard l(x_queue);
						mapUTXOTxResult[work.sha3()] = ret;
					}
				}
				catch (UTXOException& e)
				{
					LOG(ERROR) << "SendSelectedTokens Error:" << e.what();
				}
			}

			{
				Guard l(x_queue);
				m_executedCnt++;
				LOG(TRACE) << "UTXOTxQueue::executeUTXOTx() getThreadName:" << getThreadName() << ",executedCnt:" << m_executedCnt << ",totalCnt:" << m_totalCnt;
				if (m_totalCnt == m_executedCnt)
				{
					m_onReady();
				}
			}
		}
	}

	map<h256, UTXOExecuteState> UTXOTxQueue::getTxResult()
	{
		Guard l(x_queue);
		return mapUTXOTxResult;
	}
}