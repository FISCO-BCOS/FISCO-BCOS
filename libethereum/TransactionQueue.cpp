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

#include <libdevcore/easylog.h>
#include <libethcore/Exceptions.h>
#include <UTXO/UTXOSharedData.h>

#include "Transaction.h"
#include "TransactionQueue.h"
#include "StatLog.h"
#include "SystemContractApi.h"

using namespace std;
using namespace dev;
using namespace dev::eth;



const size_t c_maxVerificationQueueSize = 8192;

TransactionQueue::TransactionQueue(Interface* _interface, unsigned _limit, unsigned _futureLimit):
	m_current(PriorityCompare { *this }),
	m_limit(_limit),
	m_futureLimit(_futureLimit)
{
	m_interface = _interface;
	unsigned verifierThreads = std::max(thread::hardware_concurrency(), 3U) - 2U;
	for (unsigned i = 0; i < verifierThreads; ++i)
		m_verifiers.emplace_back([ = ]() {
		pthread_setThreadName("txcheck" + toString(i));
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

std::pair<ImportResult, h256> TransactionQueue::import(bytesConstRef _transactionRLP, IfDropped _ik)
{
	//LOG(TRACE) << "TransactionQueue::import ";
	// Check if we already know this transaction.
	h256 h = sha3(_transactionRLP);

	Transaction t;
	ImportResult ir;
	{
		UpgradableGuard l(m_lock);

		ir = check_WITH_LOCK(h, _ik);
		if (ir != ImportResult::Success)
			return std::make_pair(ir, h);

		try
		{
			// Check validity of _transactionRLP as a transaction. To do this we just deserialise and attempt to determine the sender.
			// If it doesn't work, the signature is bad.
			// The transaction's nonce may yet be invalid (or, it could be "valid" but we may be missing a marginally older transaction).
			//LOG(TRACE)<<"TransactionQueue::import befor check ";

			t = Transaction(_transactionRLP, CheckTransaction::Everything);
			if (t.isCNS())
			{
				t.receiveAddress();
			}

			t.setImportTime(utcTime());

			UpgradeGuard ul(l);
			LOG(TRACE) << "Importing" << t;
			ir = manageImport_WITH_LOCK(h, t);
		}
		catch (...)
		{
			LOG(WARNING) << boost::current_exception_diagnostic_information() << "\n";
			return std::make_pair(ImportResult::Malformed, h);
		}
	}
	return std::make_pair(ir, h);
}

ImportResult TransactionQueue::check_WITH_LOCK(h256 const& _h, IfDropped _ik)
{
	//LOG(TRACE) << "TransactionQueue::check_WITH_LOCK " << _h;

	if (m_known.count(_h))
		return ImportResult::AlreadyKnown;

	if (m_dropped.count(_h) && _ik == IfDropped::Ignore)
		return ImportResult::AlreadyInChain;

	return ImportResult::Success;
}

ImportResult TransactionQueue::import(Transaction const& _transaction, IfDropped _ik)
{
	// Check if we already know this transaction.
	h256 h = _transaction.sha3();
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
			//LOG(TRACE) << "TransactionQueue::topTransactions " << t->transaction.sha3() << ",nonce=" << t->transaction.randomid();
		}

	LOG(TRACE) << "TransactionQueue::topTransactions: " << ret.size() << " total: " << m_current.size();

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
	LOG(TRACE) << " TransactionQueue::manageImport_WITH_LOCK " << _h << _transaction.sha3();

	try
	{
		auto _h0 = _transaction.sha3();
		if (_h != _h0)
		{
			LOG(ERROR) << " transaction sha3 is not same ,before rlp h=" << _h
				<< " ,after rlp h0= " << _h0;
			return ImportResult::Malformed;
		}

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
		
		if( _transaction.isCreation())
		{
			
			u256 ret = m_interface->filterCheck(_transaction,FilterCheckScene::CheckDeploy);
			if( (u256)SystemContractCode::Ok != ret)
			{
				LOG(WARNING)<<"TransactionQueue::manageImport_WITH_LOCK hasDeployPermission fail! "<<_transaction.sha3();
				return ImportResult::NoDeployPermission;
			}
		} else {
			
			u256 ret = m_interface->filterCheck(_transaction,FilterCheckScene::CheckTx);
			LOG(TRACE)<<"TransactionQueue::manageImport_WITH_LOCK FilterCheckScene::CheckTx ";
			if( (u256)SystemContractCode::Ok != ret)
			{
				LOG(WARNING)<<"TransactionQueue::manageImport_WITH_LOCK hasTxPermission fail! "<<_transaction.sha3();
				return ImportResult::NoTxPermission;
			}
		}

		// check before import
		{
			try
			{
				UTXOType utxoType = _transaction.getUTXOType();
				u256 curBlockNum = UTXOModel::UTXOSharedData::getInstance()->getBlockNum();
				LOG(TRACE) << "TransactionQueue::manageImport_WITH_LOCK utxoType:" << utxoType << ",curBlock:" << curBlockNum << ",updateHeight:" << BlockHeader::updateHeight;
				if (UTXOType::InitTokens == utxoType || 
					UTXOType::SendSelectedTokens == utxoType) 
				{
					if (0 == BlockHeader::updateHeight || 
						curBlockNum <= BlockHeader::updateHeight)
					{
						UTXO_EXCEPTION_THROW("TransactionQueue::manageImport_WITH_LOCK Error:LowEthVersion", UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrLowEthVersion);
						LOG(WARNING) << "TransactionQueue::manageImport_WITH_LOCK Error:" << UTXOModel::UTXOExecuteState::LowEthVersion;
					}
					_transaction.checkUTXOTransaction(m_interface->getUTXOMgr());
				}
				else if (utxoType != UTXOType::InValid)
				{					
					UTXO_EXCEPTION_THROW("TransactionQueue::manageImport_WITH_LOCK Error:UTXOTypeInvalid", UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeUTXOTypeInvalid);
					LOG(WARNING) << "TransactionQueue::manageImport_WITH_LOCK Error:" << UTXOModel::UTXOExecuteState::UTXOTypeInvalid;
				}
			}
			catch (UTXOModel::UTXOException& e)
			{
				UTXOModel::EnumUTXOExceptionErrCode code = e.error_code();
				LOG(WARNING) << "TransactionQueue::manageImport_WITH_LOCK ErrorCode:" << (int)code;
				if (UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeUTXOTypeInvalid == code)
				{
					return ImportResult::UTXOInvalidType;
				}
				else if (UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeJsonParamError == code)
				{
					return ImportResult::UTXOJsonParamError;
				}
				else if (UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenIDInvalid == code)
				{
					return ImportResult::UTXOTokenIDInvalid;
				}
				else if (UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenUsed == code)
				{
					return ImportResult::UTXOTokenUsed;
				}
				else if (UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenOwnerShipCheckFail == code)
				{
					return ImportResult::UTXOTokenOwnerShipCheckFail;
				}
				else if (UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenLogicCheckFail == code)
				{
					return ImportResult::UTXOTokenLogicCheckFail;
				}
				else if (UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeTokenAccountingBalanceFail == code)
				{
					return ImportResult::UTXOTokenAccountingBalanceFail;
				}
				else if (UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrTokenCntOutofRange == code)
				{
					return ImportResult::UTXOTokenCntOutofRange;
				}
				else if (UTXOModel::EnumUTXOExceptionErrCode::EnumUTXOExceptionErrLowEthVersion == code)
				{
					return ImportResult::UTXOLowEthVersion;
				}
				else 
				{
					return ImportResult::UTXOTxError;
				}
			}
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
					// drop bed log
					dev::eth::TxFlowLog(dropped, "same nonce", true);
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
			//LOG(TRACE) << "Dropping out of bounds transaction" << _h;
			LOG(WARNING) << "Dropping out of bounds transaction" << _h;
			remove_WITH_LOCK(m_current.rbegin()->transaction.sha3());
			// drop oversize log
			dev::eth::TxFlowLog(m_current.rbegin()->transaction.sha3(), "oversize", true);
		}

		m_onReady();
	}
	catch (Exception const& _e)
	{
		LOG(WARNING) << "Ignoring invalid transaction: " <<  diagnostic_information(_e);
		return ImportResult::Malformed;
	}
	catch (std::exception const& _e)
	{
		LOG(WARNING) << "Ignoring invalid transaction: " << _e.what();
		return ImportResult::Malformed;
	}

	LOG(TRACE) << "Imported tx " << _h;

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
	// start tx trace log
	// TODO FLAG 1  "0x" + _p.first.hex().substr(0, 5) => _p.first.hex()
	// dev::eth::TxFlowLog(_p.first, "0x" + _p.first.hex().substr(0, 5), false, true);	
	dev::eth::TxFlowLog(_p.first, _p.first.hex(), false, true);
	LOG(TRACE) << " Hash=" << (t.sha3()) << ",Randid=" << t.randomid() << ",insert_time=" << utcTime();
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
	// bed drop log
	dev::eth::TxFlowLog(_txHash, "drop", true);
}

void TransactionQueue::dropGood(Transaction const& _t)
{
	WriteGuard l(m_lock);
	makeCurrent_WITH_LOCK(_t);
	if (!m_known.count(_t.sha3()))
		return;
	remove_WITH_LOCK(_t.sha3());

	// good drop log
	dev::eth::TxFlowLog(_t.sha3(), "onChain");
	LOG(TRACE) << "DropGood tx" << _t.sha3();
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
			t.setImportType(1); // 1 for p2p
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
