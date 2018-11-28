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
/** @file EthereumHost.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "EthereumHost.h"

#include <chrono>
#include <thread>

#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libp2p/Host.h>
#include <libp2p/Session.h>
#include <libethcore/Exceptions.h>
#include "BlockChain.h"
#include "TransactionQueue.h"
#include "BlockQueue.h"
#include "EthereumPeer.h"
#include "BlockChainSync.h"
#include "NodeConnParamsManagerApi.h"
#include "StatLog.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace p2p;

unsigned const EthereumHost::c_oldProtocolVersion = 62; //TODO: remove this once v63+ is common
static unsigned const c_maxSendTransactions = 100;

char const* const EthereumHost::s_stateNames[static_cast<int>(SyncState::Size)] = {"NotSynced", "Idle", "Waiting", "Blocks", "State", "NewBlocks" };



namespace
{
class EthereumPeerObserver: public EthereumPeerObserverFace
{
public:
	EthereumPeerObserver(BlockChainSync& _sync, RecursiveMutex& _syncMutex, TransactionQueue& _tq, Web3Observer::Ptr _observer = Web3Observer::Ptr()):
		m_sync(_sync), m_syncMutex(_syncMutex), m_tq(_tq), m_channelMessageObserver(_observer) {}

	void onPeerStatus(std::shared_ptr<EthereumPeer> _peer) override
	{
		RecursiveGuard l(m_syncMutex);
		try
		{
			m_sync.onPeerStatus(_peer);
		}
		catch (FailedInvariant const&)
		{
			// "fix" for https://github.com/ethereum/webthree-umbrella/issues/300
			LOG(WARNING) << "Failed invariant during sync, restarting sync";
			m_sync.restartSync();
		}
	}

	void onPeerTransactions(std::shared_ptr<EthereumPeer> _peer, RLP const& _r) override
	{
		unsigned itemCount = _r.itemCount();
		LOG(TRACE) << "Transactions (" << dec << itemCount << "entries)";
		m_tq.enqueue(_r, _peer->id());
	}

	void onPeerAborting() override
	{
		RecursiveGuard l(m_syncMutex);
		try
		{
			m_sync.onPeerAborting();
		}
		catch (Exception&)
		{
			LOG(WARNING) << "Exception on peer destruciton: " << boost::current_exception_diagnostic_information();
		}
	}

	void onPeerBlockHeaders(std::shared_ptr<EthereumPeer> _peer, RLP const& _headers) override
	{
		RecursiveGuard l(m_syncMutex);
		try
		{
			m_sync.onPeerBlockHeaders(_peer, _headers);
		}
		catch (FailedInvariant const&)
		{
			// "fix" for https://github.com/ethereum/webthree-umbrella/issues/300
			LOG(WARNING) << "Failed invariant during sync, restarting sync";
			m_sync.restartSync();
		}
	}

	void onPeerBlockBodies(std::shared_ptr<EthereumPeer> _peer, RLP const& _r) override
	{
		RecursiveGuard l(m_syncMutex);
		try
		{
			m_sync.onPeerBlockBodies(_peer, _r);
		}
		catch (FailedInvariant const&)
		{
			// "fix" for https://github.com/ethereum/webthree-umbrella/issues/300
			LOG(WARNING) << "Failed invariant during sync, restarting sync";
			m_sync.restartSync();
		}
	}

	void onPeerNewHashes(std::shared_ptr<EthereumPeer> _peer, RLP const& _r) override
	{
		RecursiveGuard l(m_syncMutex);
		try
		{
			m_sync.onPeerNewHashes(_peer, _r);
		}
		catch (FailedInvariant const&)
		{
			// "fix" for https://github.com/ethereum/webthree-umbrella/issues/300
			LOG(WARNING) << "Failed invariant during sync, restarting sync";
			m_sync.restartSync();
		}
	}

	void onPeerNewBlock(std::shared_ptr<EthereumPeer> _peer, RLP const& _r) override
	{
		RecursiveGuard l(m_syncMutex);
		try
		{
			m_sync.onPeerNewBlock(_peer, _r);
		}
		catch (FailedInvariant const&)
		{
			// "fix" for https://github.com/ethereum/webthree-umbrella/issues/300
			LOG(WARNING) << "Failed invariant during sync, restarting sync";
			m_sync.restartSync();
		}
	}

	void onPeerNodeData(std::shared_ptr<EthereumPeer> /* _peer */, RLP const& _r) override
	{
		unsigned itemCount = _r.itemCount();
		LOG(TRACE) << "Node Data (" << dec << itemCount << "entries)";
	}

	void onPeerReceipts(std::shared_ptr<EthereumPeer> /* _peer */, RLP const& _r) override
	{
		unsigned itemCount = _r.itemCount();
		LOG(TRACE) << "Receipts (" << dec << itemCount << "entries)";
	}

	void onCustomMessage(std::shared_ptr<EthereumPeer> _peer, std::shared_ptr<dev::bytes> data) override {
		if (m_channelMessageObserver.get() != NULL) {
			m_channelMessageObserver->onReceiveChannelMessage(_peer->id(), data);
		}
	}

private:
	BlockChainSync& m_sync;
	RecursiveMutex& m_syncMutex;
	TransactionQueue& m_tq;

	Web3Observer::Ptr m_channelMessageObserver;

};

class EthereumHostData: public EthereumHostDataFace
{
public:
	EthereumHostData(BlockChain const& _chain, OverlayDB const& _db): m_chain(_chain), m_db(_db) {}

	pair<bytes, unsigned> blockHeaders(RLP const& _blockId, unsigned _maxHeaders, u256 _skip, bool _reverse) const override
	{
		auto numHeadersToSend = _maxHeaders;

		auto step = static_cast<unsigned>(_skip) + 1;
		assert(step > 0 && "step must not be 0");

		h256 blockHash;
		if (_blockId.size() == 32) // block id is a hash
		{
			blockHash = _blockId.toHash<h256>();
			//blockNumber = host()->chain().number(blockHash);
			LOG(DEBUG) << "GetBlockHeaders (block (hash): " << blockHash
			          << ", maxHeaders: " << _maxHeaders
			          << ", skip: " << _skip << ", reverse: " << _reverse << ")";

			if (!_reverse)
			{
				auto n = m_chain.number(blockHash);
				if (numHeadersToSend == 0)
					blockHash = {};
				else if (n != 0 || blockHash == m_chain.genesisHash())
				{
					auto top = n + uint64_t(step) * numHeadersToSend - 1;
					auto lastBlock = m_chain.number();
					if (top > lastBlock)
					{
						numHeadersToSend = (lastBlock - n) / step + 1;
						top = n + step * (numHeadersToSend - 1);
					}
					assert(top <= lastBlock && "invalid top block calculated");
					blockHash = m_chain.numberHash(static_cast<unsigned>(top)); // override start block hash with the hash of the top block we have
				}
				else
					blockHash = {};
			}
			else if (!m_chain.isKnown(blockHash))
				blockHash = {};
		}
		else // block id is a number
		{
			auto n = _blockId.toInt<bigint>();
			LOG(DEBUG) << "GetBlockHeaders (" << n
			          << "max: " << _maxHeaders
			          << "skip: " << _skip << (_reverse ? "reverse" : "") << ")";

			if (!_reverse)
			{
				auto lastBlock = m_chain.number();
				if (n > lastBlock || numHeadersToSend == 0)
					blockHash = {};
				else
				{
					bigint top = n + uint64_t(step) * (numHeadersToSend - 1);
					if (top > lastBlock)
					{
						numHeadersToSend = (lastBlock - static_cast<unsigned>(n)) / step + 1;
						top = n + step * (numHeadersToSend - 1);
					}
					assert(top <= lastBlock && "invalid top block calculated");
					blockHash = m_chain.numberHash(static_cast<unsigned>(top)); // override start block hash with the hash of the top block we have
				}
			}
			else if (n <= std::numeric_limits<unsigned>::max())
				blockHash = m_chain.numberHash(static_cast<unsigned>(n));
			else
				blockHash = {};
		}

		auto nextHash = [this](h256 _h, unsigned _step)
		{
			static const unsigned c_blockNumberUsageLimit = 1000;

			const auto lastBlock = m_chain.number();
			const auto limitBlock = lastBlock > c_blockNumberUsageLimit ? lastBlock - c_blockNumberUsageLimit : 0; // find the number of the block below which we don't expect BC changes.

			while (_step) // parent hash traversal
			{
				auto details = m_chain.details(_h);
				if (details.number < limitBlock)
					break; // stop using parent hash traversal, fallback to using block numbers
				_h = details.parent;
				--_step;
			}

			if (_step) // still need lower block
			{
				auto n = m_chain.number(_h);
				if (n >= _step)
					_h = m_chain.numberHash(n - _step);
				else
					_h = {};
			}


			return _h;
		};

		bytes rlp;
		unsigned itemCount = 0;
		vector<h256> hashes;
		for (unsigned i = 0; i != numHeadersToSend; ++i)
		{
			if (!blockHash || !m_chain.isKnown(blockHash))
				break;

			hashes.push_back(blockHash);
			++itemCount;

			blockHash = nextHash(blockHash, step);
		}

		for (unsigned i = 0; i < hashes.size() && rlp.size() < c_maxPayload; ++i)
			rlp += m_chain.headerData(hashes[_reverse ? i : hashes.size() - 1 - i]);

		return make_pair(rlp, itemCount);
	}

	pair<bytes, unsigned> blockBodies(RLP const& _blockHashes) const override
	{
		unsigned const count = static_cast<unsigned>(_blockHashes.itemCount());

		bytes rlp;
		unsigned n = 0;
		auto numBodiesToSend = std::min(count, c_maxBlocks);
		for (unsigned i = 0; i < numBodiesToSend && rlp.size() < c_maxPayload; ++i)
		{
			auto h = _blockHashes[i].toHash<h256>();
			if (m_chain.isKnown(h))
			{
				bytes blockBytes = m_chain.block(h);
				RLP block{blockBytes};
				RLPStream body;
				body.appendList(4);
				body.appendRaw(block[1].data()); // transactions
				body.appendRaw(block[2].data()); // uncles
				body.appendRaw(block[3].data()); // block hash
				body.appendRaw(block[4].data()); // sign_list
				auto bodyBytes = body.out();
				rlp.insert(rlp.end(), bodyBytes.begin(), bodyBytes.end());
				++n;
			}
		}
		if (count > 20 && n == 0)
			LOG(WARNING) << "all" << count << "unknown blocks requested; peer on different chain?";
		else
			LOG(INFO) << n << "blocks known and returned;" << (numBodiesToSend - n) << "blocks unknown;" << (count > c_maxBlocks ? count - c_maxBlocks : 0) << "blocks ignored";

		return make_pair(rlp, n);
	}

	strings nodeData(RLP const& _dataHashes) const override
	{
		unsigned const count = static_cast<unsigned>(_dataHashes.itemCount());

		strings data;
		size_t payloadSize = 0;
		auto numItemsToSend = std::min(count, c_maxNodes);
		for (unsigned i = 0; i < numItemsToSend && payloadSize < c_maxPayload; ++i)
		{
			auto h = _dataHashes[i].toHash<h256>();
			auto node = m_db.lookup(h);
			if (!node.empty())
			{
				payloadSize += node.length();
				data.push_back(move(node));
			}
		}
		LOG(INFO) << data.size() << " nodes known and returned;" << (numItemsToSend - data.size()) << " unknown;" << (count > c_maxNodes ? count - c_maxNodes : 0) << " ignored";

		return data;
	}

	pair<bytes, unsigned> receipts(RLP const& _blockHashes) const override
	{
		unsigned const count = static_cast<unsigned>(_blockHashes.itemCount());

		bytes rlp;
		unsigned n = 0;
		auto numItemsToSend = std::min(count, c_maxReceipts);
		for (unsigned i = 0; i < numItemsToSend && rlp.size() < c_maxPayload; ++i)
		{
			auto h = _blockHashes[i].toHash<h256>();
			if (m_chain.isKnown(h))
			{
				auto const receipts = m_chain.receipts(h);
				auto receiptsRlpList = receipts.rlp();
				rlp.insert(rlp.end(), receiptsRlpList.begin(), receiptsRlpList.end());
				++n;
			}
		}
		LOG(INFO) << n << " receipt lists known and returned;" << (numItemsToSend - n) << " unknown;" << (count > c_maxReceipts ? count - c_maxReceipts : 0) << " ignored";

		return make_pair(rlp, n);
	}

private:
	BlockChain const& m_chain;
	OverlayDB const& m_db;
};

class ChannelMessageObserver: public ChannelMessageObserverFace {
public:
	ChannelMessageObserver(Web3Observer::Ptr web3Observer, EthereumHost *host): _web3Observer(web3Observer), _host(host) {};

	virtual ~ChannelMessageObserver() {}

	virtual void onChannelMessage(std::shared_ptr<EthereumPeer> peer, std::shared_ptr<bytes> data) {
		if (_web3Observer.get() != NULL) {
			_web3Observer->onReceiveChannelMessage(peer->id(), data);
		}
		else {
			LOG(ERROR) << "Not Set web3Observer";
		}
	}

	virtual void onTopicMessage(std::shared_ptr<EthereumPeer> peer, int type, int seq, std::shared_ptr<std::vector<std::string> > topics) {

		LOG(TRACE) <<" Recv topic type:" << type << " seq:" << seq << " topics:" << topics->size();
		switch (type) {
		case 0: 
		{
			if (peer->getTopicsSeq() != seq) {
				LOG(TRACE) << "peer seq:" << seq << " local seq:" << peer->getTopicsSeq() << "request lastest topics";
				_host->sendTopicsMessage(peer->id(), 1, 0, std::make_shared<std::set<std::string> >());
			}
			break;
		}
		case 1: 
		{
			LOG(TRACE) << "send topics data";
			_host->sendTopicsMessage(peer->id(), 2, _host->getTopicsSeq(), _host->getTopics());

			break;
		}
		case 2: 
		{
			LOG(TRACE) << "Recv peer:" << peer->id().abridged() << "  topics sum:" << topics->size();
			for (auto it : *topics) {
				LOG(TRACE) << "topic:" << it;
			}

			peer->setTopicsSeq(seq);
			peer->setTopics(topics);

			break;
		}
		}
	}

private:
	Web3Observer::Ptr _web3Observer;
	EthereumHost *_host;
};

}

EthereumHost::EthereumHost(BlockChain const& _ch, OverlayDB const& _db, TransactionQueue& _tq, BlockQueue& _bq, u256 _networkId):
	HostCapability<EthereumPeer>(),
	Worker		("ethsync"),
	m_chain		(_ch),
	m_db(_db),
	m_tq		(_tq),
	m_bq		(_bq),
	m_networkId	(_networkId),
	m_hostData(make_shared<EthereumHostData>(m_chain, m_db))
{
	// TODO: Composition would be better. Left like that to avoid initialization
	//       issues as BlockChainSync accesses other EthereumHost members.
	m_sync.reset(new BlockChainSync(*this));
	m_peerObserver = make_shared<EthereumPeerObserver>(*m_sync, x_sync, m_tq);

	NodeConnManagerSingleton::GetInstance().setEthereumHost(this);
	m_latestBlockSent = _ch.currentHash();
	m_tq.onImport([this](ImportResult _ir, h256 const & _h, h512 const & _nodeId) { onTransactionImported(_ir, _h, _nodeId); });

	_topics = std::make_shared<std::set<std::string> >();
}

EthereumHost::~EthereumHost()
{
	stopWorking();
}

bool EthereumHost::ensureInitialised()
{
	if (!m_latestBlockSent)
	{
		// First time - just initialise.
		m_latestBlockSent = m_chain.currentHash();
		LOG(TRACE) << "Initialising: latest=" << m_latestBlockSent;

		Guard l(x_transactions);
		m_transactionsSent = m_tq.knownTransactions();
		return true;
	}
	return false;
}

void EthereumHost::reset()
{
	RecursiveGuard l(x_sync);
	m_sync->abortSync();

	m_latestBlockSent = h256();
	Guard tl(x_transactions);
	m_transactionsSent.clear();
}

void EthereumHost::doWork()
{
	bool netChange = ensureInitialised();
	auto h = m_chain.currentHash();
	// If we've finished our initial sync (including getting all the blocks into the chain so as to reduce invalid transactions), start trading transactions & blocks
	//LOG(TRACE) << "EthereumHost::doWork";
	//maintainTransactions();
	if (!isSyncing() && m_chain.isKnown(m_latestBlockSent))
	{
		if (m_newTransactions)
		{
			m_newTransactions = false;
			maintainTransactions();
		}
		if (m_newBlocks)
		{
			m_newBlocks = false;
			maintainBlocks(h);
		}
	}

	time_t  now = std::chrono::system_clock::to_time_t(chrono::system_clock::now());
	if (now - m_lastTick >= 1)
	{
		m_lastTick = now;
		foreachPeer([](std::shared_ptr<EthereumPeer> _p) { _p->tick(); return true; });
	}

//	return netChange;
	// TODO: Figure out what to do with netChange.
	(void)netChange;
}


void EthereumHost::maintainTransactions()
{
	// Send any new transactions.
	unordered_map<std::shared_ptr<EthereumPeer>, std::vector<size_t>> peerTransactions;

	/*
	auto ts = m_tq.topTransactions(c_maxSendTransactions);
	{
		Guard l(x_transactions);
		for (size_t i = 0; i < ts.size(); ++i)
		{
			auto const& t = ts[i];
			bool unsent = !m_transactionsSent.count(t.sha3());
			auto peers = get<1>(randomSelection(0, [&](EthereumPeer * p) { return p->m_requireTransactions || (unsent && !p->m_knownTransactions.count(t.sha3())); }));
			for (auto const& p : peers)
				peerTransactions[p].push_back(i);
		}
		for (auto const& t : ts)
			m_transactionsSent.insert(t.sha3());
	}
	*/

	//auto ts = m_tq.allTransactions();
	auto ts = m_tq.topTransactions(c_maxSendTransactions, m_transactionsSent);
	{
		Guard l(x_transactions);
		for (size_t i = 0; i < ts.size(); ++i) {
			auto const& t = ts[i];
			bool unsent = !m_transactionsSent.count(t.sha3());
			vector<shared_ptr<EthereumPeer>> peers;
			if (t.importType() == 0) {
				peers = get<1>(randomSelection(0, [&](EthereumPeer * p) {
					DEV_GUARDED(p->x_knownTransactions)
					return p->m_requireTransactions || (unsent && !p->m_knownTransactions.count(t.sha3()));
					return false;
				}));
			} else {
				peers = get<0>(randomSelection(25, [&](EthereumPeer * p) {
					DEV_GUARDED(p->x_knownTransactions)
					return p->m_requireTransactions || (unsent && !p->m_knownTransactions.count(t.sha3()));
					return false;
				}));
			}
			for (auto const& p : peers) {
				peerTransactions[p].push_back(i);
			}

			if (unsent) {
				m_transactionsSent.insert(t.sha3());
			}
		}
	}

	foreachPeer([&](shared_ptr<EthereumPeer> _p)
	{
		bytes b;
		unsigned n = 0;

		DEV_GUARDED(_p->x_knownTransactions)
		for (auto const& i : peerTransactions[_p])
		{
			if (_p->m_knownTransactions.size() > EthereumPeer::kKnownTranscationsSize) {
				_p->m_knownTransactions.pop();
			}
			_p->m_knownTransactions.insert(ts[i].sha3());
			b += ts[i].rlp();
			++n;
			// LOG(DEBUG) << "Sent transactions to " << _p->session()->id() << " " << ts[i].sha3() ;
		}

		//_p->clearKnownTransactions();

		if (n || _p->m_requireTransactions)
		{
			RLPStream ts;
			_p->prep(ts, TransactionsPacket, n).appendRaw(b, n);

			auto s = _p->session();
			if(s) {
				BroadcastTxSizeLog(s->id(), ts.out().size());

				_p->sealAndSend(ts);
				LOG(TRACE) << "Sent" << n << "transactions to " << s->info().clientVersion;
			}
		}
		_p->m_requireTransactions = false;
		return true;
	});
}

void EthereumHost::foreachPeer(std::function<bool(std::shared_ptr<EthereumPeer>)> const & _f) const
{
	//order peers by protocol, rating, connection age

	auto sessions = peerSessions();


	auto sessionLess = [](std::pair<std::shared_ptr<SessionFace>, std::shared_ptr<Peer>> const & _left, std::pair<std::shared_ptr<SessionFace>, std::shared_ptr<Peer>> const & _right)
	{ return _left.first->rating() == _right.first->rating() ? _left.first->connectionTime() < _right.first->connectionTime() : _left.first->rating() > _right.first->rating(); };

	std::sort(sessions.begin(), sessions.end(), sessionLess);
	for (auto s : sessions)
		if (!_f(capabilityFromSession<EthereumPeer>(*s.first)))
			return;

	sessions = peerSessions(c_oldProtocolVersion); //TODO: remove once v61+ is common
	std::sort(sessions.begin(), sessions.end(), sessionLess);
	for (auto s : sessions)
		if (!_f(capabilityFromSession<EthereumPeer>(*s.first, c_oldProtocolVersion)))
			return;
}

tuple<vector<shared_ptr<EthereumPeer>>, vector<shared_ptr<EthereumPeer>>, vector<shared_ptr<SessionFace>>> EthereumHost::randomSelection(unsigned _percent, std::function<bool(EthereumPeer*)> const & _allow)
{
	vector<shared_ptr<EthereumPeer>> chosen;
	vector<shared_ptr<EthereumPeer>> allowed;
	vector<shared_ptr<SessionFace>> sessions;

	size_t peerCount = 0;
	foreachPeer([&](std::shared_ptr<EthereumPeer> _p)
	{
		if (_allow(_p.get()))
		{
			allowed.push_back(_p);
			sessions.push_back(_p->session());
		}
		++peerCount;
		return true;
	});

	size_t chosenSize = (peerCount * _percent + 99) / 100;
	chosen.reserve(chosenSize);
	for (unsigned i = chosenSize; i && allowed.size(); i--)
	{
		unsigned n = rand() % allowed.size();
		chosen.push_back(std::move(allowed[n]));
		allowed.erase(allowed.begin() + n);
	}
	return make_tuple(move(chosen), move(allowed), move(sessions));
}

void EthereumHost::maintainBlocks(h256 const & _currentHash)
{
	// Send any new blocks.
	auto detailsFrom = m_chain.details(m_latestBlockSent);
	auto detailsTo = m_chain.details(_currentHash);
	if (detailsFrom.totalDifficulty < detailsTo.totalDifficulty)
	{
		if (diff(detailsFrom.number, detailsTo.number) < 20)
		{
			// don't be sending more than 20 "new" blocks. if there are any more we were probably waaaay behind.
			LOG(TRACE) << "Sending a new block (current is" << _currentHash << ", was" << m_latestBlockSent << ")";

			h256s blocks = get<0>(m_chain.treeRoute(m_latestBlockSent, _currentHash, false, false, true));
			//先发送NewBlockHash
			auto s = randomSelection(0, [&](EthereumPeer * p) {
				DEV_GUARDED(p->x_knownBlocks)
				return !p->m_knownBlocks.count(_currentHash);
				return false;
			});
			for (shared_ptr<EthereumPeer> const& p : get<1>(s))
			{
				RLPStream ts;
				p->prep(ts, NewBlockHashesPacket, blocks.size());
				for (auto const& b : blocks)
				{
					ts.appendList(2);
					ts.append(b);
					ts.append(m_chain.number(b));
				}

				//Guard l(p->x_knownBlocks);
				p->sealAndSend(ts);
				//p->m_knownBlocks.clear();
			}
			
			std::this_thread::sleep_for(chrono::milliseconds(100));
			
			auto s2 = randomSelection(25, [&](EthereumPeer * p) {
				DEV_GUARDED(p->x_knownBlocks)
				return !p->m_knownBlocks.count(_currentHash);
				return false;
			});
			for (shared_ptr<EthereumPeer> const& p : get<0>(s2))
				for (auto const& b : blocks)
				{
					RLPStream ts;
					p->prep(ts, NewBlockPacket, 2).appendRaw(m_chain.block(b), 1).append(m_chain.details(b).totalDifficulty);
					
					auto s = p->session();
					if(s) {
						BroadcastBlockSizeLog(s->id(), ts.out().size());
						//Guard l(p->x_knownBlocks);
						p->sealAndSend(ts);
						//p->m_knownBlocks.clear();
						DEV_GUARDED(p->x_knownBlocks)
						{
							if (p->m_knownBlocks.size() > EthereumPeer::kKnownBlockSize) {
								p->m_knownBlocks.pop();
							}
							p->m_knownBlocks.insert(b);
						}
					}
				}
		}
		m_latestBlockSent = _currentHash;
	}
}

bool EthereumHost::isSyncing() const
{
	return m_sync->isSyncing();
}

SyncStatus EthereumHost::status() const
{
	RecursiveGuard l(x_sync);
	return m_sync->status();
}

void EthereumHost::onTransactionImported(ImportResult _ir, h256 const & _h, h512 const & _nodeId)
{
	auto session = host()->peerSession(_nodeId);
	if (!session)
		return;

	std::shared_ptr<EthereumPeer> peer = capabilityFromSession<EthereumPeer>(*session);
	if (!peer)
		peer = capabilityFromSession<EthereumPeer>(*session, c_oldProtocolVersion);
	if (!peer)
		return;

	Guard l(peer->x_knownTransactions);
	peer->m_knownTransactions.insert(_h);
	switch (_ir)
	{
	case ImportResult::Malformed:
		peer->addRating(-100);
		break;
	case ImportResult::AlreadyKnown:
		// if we already had the transaction, then don't bother sending it on.
		DEV_GUARDED(x_transactions)
		m_transactionsSent.insert(_h);
		peer->addRating(0);
		break;
	case ImportResult::Success:
		peer->addRating(100);
		break;
	default:;
	}
}

shared_ptr<Capability> EthereumHost::newPeerCapability(shared_ptr<SessionFace> const & _s, unsigned _idOffset, p2p::CapDesc const & _cap, uint16_t _capID)
{
	auto ret = HostCapability<EthereumPeer>::newPeerCapability(_s, _idOffset, _cap, _capID);

	auto cap = capabilityFromSession<EthereumPeer>(*_s, _cap.second);
	assert(cap);
	LOG(TRACE) << "EthereumHost::newPeerCapability totalDifficulty=" << m_chain.details().totalDifficulty << ",number=" << m_chain.info().number();

	cap->init(
	    protocolVersion(),
	    m_networkId,
	    m_chain.details().totalDifficulty,
	    m_chain.currentHash(),
	    m_chain.genesisHash(),
	    m_chain.info().number(),
	    m_hostData,
	    m_peerObserver,
	    _channelObserver
	);

	return ret;
}

void EthereumHost::addNodeConnParam(std::vector<NodeConnParams> const & vParams)
{
	foreachPeer([&](std::shared_ptr<EthereumPeer> _p)
	{
		RLPStream ts;
		_p->prep(ts, NodeInfoSync, vParams.size());
		for (auto const& param : vParams)
		{
			ts.appendList(6);
			ts.append(param._sNodeId);
			ts.append(param._sAgencyInfo);
			ts.append(param._sAgencyDesc);
			ts.append(param._iIdentityType);
			ts.append(param._sIP);
			ts.append(param._iPort);
		}
		LOG(INFO) << "EthereumHost::addNodeConnParam sealandsend " << vParams.size() << "\n";
		_p->sealAndSend(ts);
		return true;
	});
}


void EthereumHost::delNodeConnParam(std::string const & sParams)
{
	if (sParams == "")
	{
		return;
	}

	foreachPeer([&](std::shared_ptr<EthereumPeer> _p)
	{
		RLPStream ts;
		_p->prep(ts, DelNodeInfoSync, 1) << sParams;
		LOG(INFO) << "EthereumHost::delNodeConnParam sealandsend " << sParams << "\n";
		_p->sealAndSend(ts);
		return true;
	});
}

void EthereumHost::setWeb3Observer(Web3Observer::Ptr _observer) {
	m_peerObserver = make_shared<EthereumPeerObserver>(*m_sync, x_sync, m_tq, _observer);
	_channelObserver = make_shared<ChannelMessageObserver>(_observer, this);
}

void EthereumHost::sendCustomMessage(NodeID nodeID, std::shared_ptr<bytes> message) {
	LOG(TRACE) << "Search Node:" << nodeID.abridged();
	bool sended = false;

	foreachPeer([&](std::shared_ptr<EthereumPeer> peer) {
		if (peer->id() == nodeID) {
			auto session = peer->session();

			if(session) {
				LOG(TRACE) << "Send Message To " << nodeID.abridged()
						   << "@"
						   << session->peer()->endpoint.address.to_string()
						   << ":"
						   << session->peer()->endpoint.tcpPort;

				RLPStream s;
				peer->prep(s, CustomMessage, 1)	<< *message;

				peer->sealAndSend(s);
				sended = true;

				return false;
			}
		}

		return true;
	});

	if (sended == false) {
		LOG(ERROR) << "Send Fail, no available node :" << nodeID.abridged();
		throw Exception("No Reach Node");
	}
}

std::vector<p2p::NodeID> EthereumHost::getPeersByTopic(std::string topic) {
	std::vector<p2p::NodeID> availablePeers;
	
	foreachPeer([&](std::shared_ptr<EthereumPeer> peer) {
		for (auto peerTopic : * (peer->getTopics())) {
			if (peerTopic == topic) {
				auto s = peer->session();
				if(s) {
					LOG(DEBUG) << "Node:" << peer->id().abridged() << "@"
							   << s->peer()->endpoint.address.to_string()
							   << ":"
							   << s->peer()->endpoint.tcpPort << " Subscribe  topic:" << topic;
				}

				availablePeers.push_back(peer->id());
				break;
			}
		}

		return true;
	});

	return availablePeers;
}

void EthereumHost::sendTopicsMessage(p2p::NodeID nodeID, int type, int seq, std::shared_ptr<std::set<std::string> > topics) {
	LOG(TRACE) <<" Send topic Req:" << seq << " type:" << type;
	int peerSended = 0;
	
	if (nodeID == p2p::NodeID()) {
		foreachPeer([&](std::shared_ptr<EthereumPeer> peer) {
			auto session = peer->session();

			if(session) {
				LOG(TRACE) << "topics Send to" << peer->id().abridged()
						   << "@"
						   << session->peer()->endpoint.address.to_string()
						   << ":"
						   << session->peer()->endpoint.tcpPort;
			}

			RLPStream s;
			peer->prep(s, Topics, 3 + topics->size());

			s.append(type);
			s.append(seq);
			s.append(topics->size());

			if (!topics->empty()) {
				for (auto topic : *topics) {
					s.append(topic);
				}
			}

			peer->sealAndSend(s);
			++peerSended;

			return true;
		});
	}
	else {
		foreachPeer([&](std::shared_ptr<EthereumPeer> peer) {
			if (peer->id() == nodeID) {
				auto session = peer->session();
				if(session) {
					LOG(TRACE) << "topics Send to" << peer->id().abridged()
							   << "@"
							   << session->peer()->endpoint.address.to_string()
							   << ":"
							   << session->peer()->endpoint.tcpPort;
				}

				RLPStream s;
				peer->prep(s, Topics, 3 + topics->size());

				s.append(type);
				s.append(seq);
				s.append(topics->size());

				if (!topics->empty()) {
					for (auto topic : *topics) {
						s.append(topic);
					}
				}

				peer->sealAndSend(s);
				++peerSended;

				return false;
			}

			return true;
		});
	}

	LOG(TRACE) << "topics Send to:" << peerSended << " nodes";
	if (peerSended == 0) {
		LOG(WARNING) << "Send Fail, no available node";
	}
}

void EthereumHost::setTopics(std::shared_ptr<std::set<std::string> > topics) {
	_topics = topics;
	++_topicsSeq;

	LOG(TRACE) << "Update topic seq:" << _topicsSeq;
}

void EthereumHost::getPeersHeight(std::map<h512, u256>& mp)
{
	foreachPeer([&](std::shared_ptr<EthereumPeer> _p)
	{
		mp[_p->id()] = _p->height();
		return true;
	});
}
