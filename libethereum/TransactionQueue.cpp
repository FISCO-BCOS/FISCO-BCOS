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
/** @file TransactionQueue.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "TransactionQueue.h"


#include <libethcore/Exceptions.h>
#include "Transaction.h"
#include <libdevcore/easylog.h>
using namespace std;
using namespace dev;
using namespace dev::eth;
#include <libdevcore/Log.h>


const size_t c_maxVerificationQueueSize = 8192;

TransactionQueue::TransactionQueue(std::shared_ptr<Interface> _interface, unsigned _limit, unsigned _futureLimit):
	m_current(PriorityCompare { *this }),
	m_limit(_limit),
	m_futureLimit(_futureLimit)
{
	m_interface = _interface;
	unsigned verifierThreads = std::max(thread::hardware_concurrency(), 3U) - 2U;
	for (unsigned i = 0; i < verifierThreads; ++i)
		m_verifiers.emplace_back([ = ]() {
		setThreadName("txcheck" + toString(i));
		this->verifierBody();
	});
}

TransactionQueue::~TransactionQueue()
{
	m_aborting = true;
	m_queueReady.notify_all();
	for (auto& i : m_verifiers)
		i.join();

	m_verifiers.clear();
}

ImportResult TransactionQueue::import(bytesConstRef _transactionRLP, IfDropped _ik)
{
	LOG(TRACE) << "TransactionQueue::import ";
	// Check if we already know this transaction.
	h256 h = sha3(_transactionRLP);

	Transaction t;
	ImportResult ir;
	{
		UpgradableGuard l(m_lock);

		ir = check_WITH_LOCK(h, _ik);
		if (ir != ImportResult::Success)
			return ir;

		try
		{
			// Check validity of _transactionRLP as a transaction. To do this we just deserialise and attempt to determine the sender.
			// If it doesn't work, the signature is bad.
			// The transaction's nonce may yet be invalid (or, it could be "valid" but we may be missing a marginally older transaction).
			

			t = Transaction(_transactionRLP, CheckTransaction::Everything);
			t.setImportTime(utcTime());


			UpgradeGuard ul(l);
			LOG(TRACE) << "Importing" << t;
			ir = manageImport_WITH_LOCK(h, t);
		}
		catch (...)
		{
			LOG(ERROR) << boost::current_exception_diagnostic_information() << endl;
			return ImportResult::Malformed;
		}
	}
	return ir;
}

ImportResult TransactionQueue::check_WITH_LOCK(h256 const& _h, IfDropped _ik)
{
	
	if (m_known.count(_h))
		return ImportResult::AlreadyKnown;

	if (m_dropped.count(_h) && _ik == IfDropped::Ignore)
		return ImportResult::AlreadyInChain;

	return ImportResult::Success;
}

ImportResult TransactionQueue::import(Transaction const& _transaction, IfDropped _ik)
{
	// Check if we already know this transaction.
	h256 h = _transaction.sha3(WithSignature);

	ImportResult ret;
	{
		UpgradableGuard l(m_lock);
		auto ir = check_WITH_LOCK(h, _ik);
		if (ir != ImportResult::Success)
			return ir;

		{
			_transaction.safeSender(); // Perform EC recovery outside of the write lock
			
			UpgradeGuard ul(l);
			ret = manageImport_WITH_LOCK(h, _transaction);
		}
	}
	return ret;
}

Transactions TransactionQueue::topTransactions(unsigned _limit, h256Hash const& _avoid) const
{
	ReadGuard l(m_lock);
	Transactions ret;
	for (auto t = m_current.begin(); ret.size() < _limit && t != m_current.end(); ++t)
		if (!_avoid.count(t->transaction.sha3()))
		{
			ret.push_back(t->transaction);
			LOG(TRACE) << "TransactionQueue::topTransactions " << t->transaction.sha3() << ",nonce=" << t->transaction.randomid();
		}

	LOG(TRACE) << "TransactionQueue::topTransactions " << ret.size();

	return ret;
}

Transactions TransactionQueue::allTransactions() const {
	ReadGuard l(m_lock);
	Transactions ret;
	for (auto t = m_current.begin(); t != m_current.end(); ++t) {
		ret.push_back(t->transaction);
	}
	return ret;
}

h256Hash TransactionQueue::knownTransactions() const
{
	ReadGuard l(m_lock);
	return m_known;
}

ImportResult TransactionQueue::manageImport_WITH_LOCK(h256 const& _h, Transaction const& _transaction)
{
	LOG(TRACE) << " TransactionQueue::manageImport_WITH_LOCK " << _transaction.sha3();

	try
	{
		assert(_h == _transaction.sha3());

		//回调client noncecheck类 检查nonce 这里判断是，nonce是否在chain上已经出现过
		if ( false == m_interface->isNonceOk(_transaction))
		{

			LOG(WARNING) << "TransactionQueue::manageImport_WITH_LOCK NonceCheck fail! " << _transaction.sha3() << "," << _transaction.randomid();
			return ImportResult::NonceCheckFail;
		}

		if ( false == m_interface->isBlockLimitOk(_transaction))
		{
			LOG(WARNING) << "TransactionQueue::manageImport_WITH_LOCK BlockLimit fail! " << _transaction.sha3() << "," << _transaction.blockLimit();
			return ImportResult::BlockLimitCheckFail;
		}
	

		// Remove any prior transaction with the same nonce but a lower gas price.
		// Bomb out if there's a prior transaction with higher gas price.
		auto cs = m_currentByAddressAndNonce.find(_transaction.from());
		if (cs != m_currentByAddressAndNonce.end())
		{
			auto t = cs->second.find(_transaction.randomid());
			if (t != cs->second.end())
			{
				if (_transaction.gasPrice() < (*t->second).transaction.gasPrice())
					return ImportResult::OverbidGasPrice;
				else
				{
					h256 dropped = (*t->second).transaction.sha3();
					remove_WITH_LOCK(dropped);
					LOG(WARNING) << _transaction.from() << "," << dropped << " Dropping Same Nonce" << _transaction.randomid();
					m_onReplaced(dropped);
				}
			}
		}
		auto fs = m_future.find(_transaction.from());
		if (fs != m_future.end())
		{
			auto t = fs->second.find(_transaction.randomid());
			if (t != fs->second.end())
			{
				if (_transaction.gasPrice() < t->second.transaction.gasPrice())
					return ImportResult::OverbidGasPrice;
				else
				{
					fs->second.erase(t);
					--m_futureSize;
					if (fs->second.empty())
						m_future.erase(fs);
				}
			}
		}
		// If valid, append to transactions.
		insertCurrent_WITH_LOCK(make_pair(_h, _transaction));
		LOG(TRACE) << "Queued vaguely legit-looking transaction" << _h;

		while (m_current.size() > m_limit)
		{
			
			LOG(WARNING) << "Dropping out of bounds transaction" << _h;
			remove_WITH_LOCK(m_current.rbegin()->transaction.sha3());
		}

		m_onReady();
	}
	catch (Exception const& _e)
	{
		LOG(ERROR) << "Ignoring invalid transaction: " <<  diagnostic_information(_e);
		return ImportResult::Malformed;
	}
	catch (std::exception const& _e)
	{
		LOG(ERROR) << "Ignoring invalid transaction: " << _e.what();
		return ImportResult::Malformed;
	}

	return ImportResult::Success;
}

u256 TransactionQueue::maxNonce(Address const& _a) const
{
	ReadGuard l(m_lock);
	return maxNonce_WITH_LOCK(_a);
}

u256 TransactionQueue::maxNonce_WITH_LOCK(Address const& _a) const
{
	u256 ret = 0;
	auto cs = m_currentByAddressAndNonce.find(_a);
	if (cs != m_currentByAddressAndNonce.end() && !cs->second.empty())
		ret = cs->second.rbegin()->first + 1;
	auto fs = m_future.find(_a);
	if (fs != m_future.end() && !fs->second.empty())
		ret = std::max(ret, fs->second.rbegin()->first + 1);
	return ret;
}

void TransactionQueue::insertCurrent_WITH_LOCK(std::pair<h256, Transaction> const& _p)
{
	LOG(TRACE) << " TransactionQueue::insertCurrent_WITH_LOCK " << _p.first;

	if (m_currentByHash.count(_p.first))
	{
		LOG(WARNING) << "Transaction hash" << _p.first << "already in current?!";
		return;
	}

	Transaction const& t = _p.second;
	// Insert into current
	auto inserted = m_currentByAddressAndNonce[t.from()].insert(std::make_pair(t.randomid(), PriorityQueue::iterator()));
	PriorityQueue::iterator handle = m_current.emplace(VerifiedTransaction(t));
	inserted.first->second = handle;
	m_currentByHash[_p.first] = handle;

	// Move following transactions from future to current
	makeCurrent_WITH_LOCK(t);
	m_known.insert(_p.first);

	
}

bool TransactionQueue::remove_WITH_LOCK(h256 const& _txHash)
{
	auto t = m_currentByHash.find(_txHash);
	if (t == m_currentByHash.end())
		return false;

	Address from = (*t->second).transaction.from();
	auto it = m_currentByAddressAndNonce.find(from);
	assert (it != m_currentByAddressAndNonce.end());
	it->second.erase((*t->second).transaction.randomid());
	m_current.erase(t->second);
	m_currentByHash.erase(t);
	if (it->second.empty())
		m_currentByAddressAndNonce.erase(it);
	m_known.erase(_txHash);

	LOG(TRACE) << "TransactionQueue::remove_WITH_LOCK " << toString(_txHash);
	return true;
}

unsigned TransactionQueue::waiting(Address const& _a) const
{
	ReadGuard l(m_lock);
	unsigned ret = 0;
	auto cs = m_currentByAddressAndNonce.find(_a);
	if (cs != m_currentByAddressAndNonce.end())
		ret = cs->second.size();
	auto fs = m_future.find(_a);
	if (fs != m_future.end())
		ret += fs->second.size();
	return ret;
}

void TransactionQueue::setFuture(h256 const& _txHash)
{
	WriteGuard l(m_lock);
	auto it = m_currentByHash.find(_txHash);
	if (it == m_currentByHash.end())
		return;

	VerifiedTransaction const& st = *(it->second);

	Address from = st.transaction.from();
	auto& queue = m_currentByAddressAndNonce[from];
	auto& target = m_future[from];
	auto cutoff = queue.lower_bound(st.transaction.randomid());
	for (auto m = cutoff; m != queue.end(); ++m)
	{
		VerifiedTransaction& t = const_cast<VerifiedTransaction&>(*(m->second)); // set has only const iterators. Since we are moving out of container that's fine
		m_currentByHash.erase(t.transaction.sha3());
		target.emplace(t.transaction.randomid(), move(t));
		m_current.erase(m->second);
		++m_futureSize;
	}
	queue.erase(cutoff, queue.end());
	if (queue.empty())
		m_currentByAddressAndNonce.erase(from);
}

void TransactionQueue::makeCurrent_WITH_LOCK(Transaction const& _t)
{

	bool newCurrent = false;
	auto fs = m_future.find(_t.from());
	if (fs != m_future.end())
	{
		u256 nonce = _t.randomid() + 1;
		auto fb = fs->second.find(nonce);
		if (fb != fs->second.end())
		{
			auto ft = fb;
			while (ft != fs->second.end() && ft->second.transaction.randomid() == nonce)
			{
				auto inserted = m_currentByAddressAndNonce[_t.from()].insert(std::make_pair(ft->second.transaction.randomid(), PriorityQueue::iterator()));
				PriorityQueue::iterator handle = m_current.emplace(move(ft->second));
				inserted.first->second = handle;
				m_currentByHash[(*handle).transaction.sha3()] = handle;
				--m_futureSize;
				++ft;
				++nonce;
				newCurrent = true;
			}
			fs->second.erase(fb, ft);
			if (fs->second.empty())
				m_future.erase(_t.from());
		}
	}

	while (m_futureSize > m_futureLimit)
	{
		// TODO: priority queue for future transactions
		// For now just drop random chain end
		--m_futureSize;
		LOG(WARNING) << "Dropping out of bounds future transaction" << m_future.begin()->second.rbegin()->second.transaction.sha3();
		m_future.begin()->second.erase(--m_future.begin()->second.end());
		if (m_future.begin()->second.empty())
			m_future.erase(m_future.begin());
	}

	if (newCurrent)
		m_onReady();
}

void TransactionQueue::drop(h256 const& _txHash)
{
	UpgradableGuard l(m_lock);

	if (!m_known.count(_txHash))
		return;

	UpgradeGuard ul(l);
	m_dropped.insert(_txHash);
	remove_WITH_LOCK(_txHash);
}

void TransactionQueue::dropGood(Transaction const& _t)
{
	WriteGuard l(m_lock);
	makeCurrent_WITH_LOCK(_t);
	if (!m_known.count(_t.sha3()))
		return;
	remove_WITH_LOCK(_t.sha3());
}

void TransactionQueue::clear()
{
	WriteGuard l(m_lock);
	m_known.clear();
	m_current.clear();
	m_currentByAddressAndNonce.clear();
	m_currentByHash.clear();
	m_future.clear();
	m_futureSize = 0;
}

void TransactionQueue::enqueue(RLP const& _data, h512 const& _nodeId)
{
	bool queued = false;
	{
		Guard l(x_queue);
		unsigned itemCount = _data.itemCount();
		for (unsigned i = 0; i < itemCount; ++i)
		{
			if (m_unverified.size() >= c_maxVerificationQueueSize)
			{
				LOG(WARNING) << "Transaction verification queue is full. Dropping" << itemCount - i << "transactions";
				break;
			}
			m_unverified.emplace_back(UnverifiedTransaction(_data[i].data(), _nodeId));
			queued = true;
		}
	}
	if (queued)
		m_queueReady.notify_all();
}

void TransactionQueue::verifierBody()
{
	while (!m_aborting)
	{
		UnverifiedTransaction work;

		{
			unique_lock<Mutex> l(x_queue);
			m_queueReady.wait(l, [&]() { return !m_unverified.empty() || m_aborting; });
			if (m_aborting)
				return;
			work = move(m_unverified.front());
			m_unverified.pop_front();
		}

		try
		{
			Transaction t(work.transaction, CheckTransaction::Everything); //Signature will be checked later 
			
			t.setImportTime(utcTime());
			ImportResult ir = import(t);
			m_onImport(ir, t.sha3(), work.nodeId);
		}
		catch (...)
		{
			// should not happen as exceptions are handled in import.
			LOG(WARNING) << "Bad transaction:" << boost::current_exception_diagnostic_information();
		}
	}
}
