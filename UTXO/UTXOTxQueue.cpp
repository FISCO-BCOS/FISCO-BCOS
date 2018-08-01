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

#include <pthread.h>
#include <sched.h>

#include <libdevcore/easylog.h>
#include <libethereum/Transaction.h>

#include "UTXOExp.h"
#include "UTXOTxQueue.h"

namespace UTXOModel
{
	u256 UTXOTxQueue::utxoVersion = 0;

	UTXOTxQueue::UTXOTxQueue()
	{
		LOG(TRACE) << "UTXOTxQueue::UTXOTxQueue() init thread begin";
		unsigned utxoParallelThreadCnt = std::max(thread::hardware_concurrency(), 3U) - 2U;
		for (unsigned i = 0; i < utxoParallelThreadCnt; ++i)
			m_executer.emplace_back([ = ]() {
			pthread_setThreadName("utxotx_" + toString(i));
			// Thread affinity setting
			cpu_set_t mask;
			CPU_ZERO(&mask);
			CPU_SET(i, &mask);
			if (pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) < 0) 
			{
				LOG(ERROR) << "UTXOTxQueue::UTXOTxQueue() set thread affinity failed";
			}
			// Thread affinity setting end
			this->executeUTXOTx();
		});
		LOG(TRACE) << "UTXOTxQueue::UTXOTxQueue() init thread end, ThreadCnt=" << utxoParallelThreadCnt;
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

	void UTXOTxQueue::setQueue(size_t totalCnt, UTXOMgr* ptrUTXOMgr)
	{
		LOG(TRACE) << "UTXOTxQueue::setQueue()";
		m_totalCnt = totalCnt;
		m_executedCnt = 0;
		m_ptrUTXOMgr = ptrUTXOMgr;
		map<string, UTXODBCache> tmp1;
		u256 tmp2 = 0;
		map<string, Token> tmp3;
		m_ptrUTXOMgr->getTxResult(tmp1, tmp2, tmp3);
		u256 blockNum = ptrUTXOMgr->getBlockNum();
		for (unsigned i = 0; i < m_executer.size(); ++i)
		{
			UTXOMgr tmp;
			tmp.setTxResult(tmp1, tmp2, tmp3);
			tmp.setBlockNum(blockNum);
			string name = "utxotx_" + toString(i);
			m_mapUTXOMgr[name] = tmp;
		}
	}

	void UTXOTxQueue::enqueue(const Transaction& t)
	{
		{
			unique_lock<Mutex> l(x_queue);
			m_unexecuted.emplace_back(t);
		}
		m_startTime = utcTime();
		m_queueReady.notify_all();
	}

	void UTXOTxQueue::enqueueBatch(const vector<Transaction>& txs)
	{
		{
			unique_lock<Mutex> l(x_queue);
			for (Transaction const& tr : txs)
			{
				m_unexecuted.emplace_back(tr);
			}
		}
		m_startTime = utcTime();
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

			UTXOMgr executor = m_mapUTXOMgr[getThreadName()];
			UTXOType utxoType = work.getUTXOType();
			if (utxoType == UTXOType::InitTokens)
			{
				try 
				{
					UTXOExecuteState ret = executor.initTokens(work.sha3(), work.sender(), work.getUTXOTxOut());
					if (UTXOExecuteState::Success != ret)
					{
						unique_lock<Mutex> l(x_data);
						errTxHash.push_back(work.sha3());
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
					UTXOExecuteState ret = executor.sendSelectedTokens(work.sha3(), work.sender(), work.getUTXOTxIn(), work.getUTXOTxOut());
					if (UTXOExecuteState::Success != ret)
					{
						unique_lock<Mutex> l(x_data);
						errTxHash.push_back(work.sha3());
					}
				}
				catch (UTXOException& e)
				{
					LOG(ERROR) << "SendSelectedTokens Error:" << e.what();
				}
			}

			{
				unique_lock<Mutex> l(x_data);
				m_executedCnt++;
				//LOG(TRACE) << "UTXOTxQueue::executeUTXOTx() getThreadName:" << getThreadName() << ",executedCnt:" << m_executedCnt << ",totalCnt:" << m_totalCnt;
				if (m_totalCnt == m_executedCnt)
				{
					uint16_t cost = utcTime() - m_startTime;
					LOG(TRACE) << "UTXOTxQueue::executeUTXOTx() execute " << m_executedCnt << " txs in " << cost << " ms,per cost:" << cost*1.0/m_executedCnt;

					// If all trades are executed, UTXOMgr in TxQueue -> UTXOMgr passed in from the outside
					map<string, UTXODBCache> cacheWritedtoDB;
					u256 dbCacheCnt = 0;
					map<string, Token> cacheToken;
					for (unsigned i = 0; i < m_executer.size(); ++i)
					{
						string name = "utxotx_" + toString(i);
						UTXOMgr tmp = m_mapUTXOMgr[name];

						map<string, UTXODBCache> tmp1;
						u256 tmp2 = 0;
						map<string, Token> tmp3;
						tmp.getTxResult(tmp1, tmp2, tmp3);
						for (map<string, UTXODBCache>::iterator it = tmp1.begin(); it != tmp1.end(); it++)
						{
							cacheWritedtoDB[it->first] = it->second;
						}
						dbCacheCnt += tmp2;
						for (map<string, Token>::iterator it = tmp3.begin(); it != tmp3.end(); it++)
						{
							cacheToken[it->first] = it->second;
						}
					}
					m_ptrUTXOMgr->addTxResult(cacheWritedtoDB, dbCacheCnt, cacheToken);
					LOG(TRACE) << "UTXOTxQueue::executeUTXOTx() setTxResult end";
					m_onReady();
				}
			}
		}
	}

	vector<h256> UTXOTxQueue::getErrTxHash()
	{
		unique_lock<Mutex> l(x_data);
		return errTxHash;
	}
}