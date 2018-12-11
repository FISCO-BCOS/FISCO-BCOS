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
/** @file BlockChainSync.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "BlockChainSync.h"

#include <chrono>
#include <libdevcore/Common.h>
#include <libdevcore/TrieHash.h>
#include <libp2p/Host.h>
#include <libp2p/Session.h>
#include <libethcore/Exceptions.h>
#include "BlockChain.h"
#include "BlockQueue.h"
#include "EthereumPeer.h"
#include "EthereumHost.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace p2p;

unsigned const c_maxPeerUknownNewBlocks = 1024; /// Max number of unknown new blocks peer can give us
unsigned const c_maxRequestHeaders = 1024;
unsigned const c_maxRequestBodies = 1024;


std::ostream& dev::eth::operator<<(std::ostream& _out, SyncStatus const& _sync)
{
	_out << "protocol: " << _sync.protocolVersion << "\n";
	_out << "state: " << EthereumHost::stateName(_sync.state) << " ";
	if (_sync.state == SyncState::Blocks || _sync.state == SyncState::NewBlocks)
		_out << _sync.currentBlockNumber << "/" << _sync.highestBlockNumber;
	return _out;
}

namespace  // Helper functions.
{

template<typename T> bool haveItem(std::map<unsigned, T>& _container, unsigned _number)
{
	if (_container.empty())
		return false;
	auto lower = _container.lower_bound(_number);
	if (lower != _container.end() && lower->first == _number)
		return true;
	if (lower ==  _container.begin())
		return false;
	--lower;
	return lower->first <= _number && (lower->first + lower->second.size()) > _number;
}


template<typename T> T const* findItem(std::map<unsigned, std::vector<T>>& _container, unsigned _number)
{
	if (_container.empty())
		return nullptr;
	auto lower = _container.lower_bound(_number);
	if (lower != _container.end() && lower->first == _number)
		return &(*lower->second.begin());
	if (lower ==  _container.begin())
		return nullptr;
	--lower;
	if (lower->first <= _number && (lower->first + lower->second.size()) > _number)
		return &lower->second.at(_number - lower->first);
	return nullptr;
}

template<typename T> void removeItem(std::map<unsigned, std::vector<T>>& _container, unsigned _number)
{
	if (_container.empty())
		return;
	auto lower = _container.lower_bound(_number);
	if (lower != _container.end() && lower->first == _number)
	{
		auto new_list = std::move(lower->second);
		new_list.erase(new_list.begin());
		auto new_key = _number + 1;
		_container.erase(lower);
		if (new_list.size() > 0) {
			_container.insert(std::make_pair(new_key, new_list));
		}
		return;
	}
	if (lower ==  _container.begin()) {
		return;
	}
	--lower;
	if (lower->first <= _number && (lower->first + lower->second.size()) > _number) {
		auto new_list = std::move(lower->second);
		new_list.erase(new_list.begin(), new_list.begin() + (_number - lower->first + 1));
		auto new_key = _number + 1;
		lower->second.erase(lower->second.begin() + (_number - lower->first), lower->second.end());
		if (new_list.size() > 0) {
			_container.insert(std::make_pair(new_key, new_list));
		}
	}
}

template<typename T> void removeAllStartingWith(std::map<unsigned, std::vector<T>>& _container, unsigned _number)
{
	if (_container.empty())
		return;
	auto lower = _container.lower_bound(_number);
	if (lower != _container.end() && lower->first == _number)
	{
		_container.erase(lower, _container.end());
		return;
	}
	if (lower == _container.begin())
	{
		_container.clear();
		return;
	}
	--lower;
	if (lower->first <= _number && (lower->first + lower->second.size()) > _number)
		lower->second.erase(lower->second.begin() + (_number - lower->first), lower->second.end());
	_container.erase(++lower, _container.end());
}

template<typename T> void removeAllSmallerThan(std::map<unsigned, std::vector<T>>& _container, unsigned _number)
{
	if (_container.empty())
		return;
	auto lower = _container.lower_bound(_number);
	if (lower != _container.end() && lower->first == _number)
	{
		auto new_list = std::move(lower->second);
		new_list.erase(new_list.begin());
		auto new_key = _number + 1;
		_container.erase(_container.begin(), ++lower);
		if (new_list.size() > 0) {
			_container.insert(std::make_pair(new_key, new_list));
		}
		return;
	}
	if (lower == _container.begin())
	{
		return;
	}
	--lower;
	if (lower->first <= _number && (lower->first + lower->second.size()) > _number) {
		auto new_list = std::move(lower->second);
		new_list.erase(new_list.begin(), new_list.begin() + (_number - lower->first + 1));
		auto new_key = _number + 1;
		_container.erase(_container.begin(), ++lower);
		if (new_list.size() > 0) {
			_container.insert(std::make_pair(new_key, new_list));
		}
	} else {
		_container.erase(_container.begin(), ++lower);
	}
}

template<typename T> void mergeInto(std::map<unsigned, std::vector<T>>& _container, unsigned _number, T&& _data)
{
	assert(!haveItem(_container, _number));
	auto lower = _container.lower_bound(_number);
	if (!_container.empty() && lower != _container.begin())
		--lower;
	if (lower != _container.end() && (lower->first + lower->second.size() == _number))
	{
		// extend existing chunk
		lower->second.emplace_back(_data);

		auto next = lower;
		++next;
		if (next != _container.end() && (lower->first + lower->second.size() == next->first))
		{
			// merge with the next chunk
			std::move(next->second.begin(), next->second.end(), std::back_inserter(lower->second));
			_container.erase(next);
		}
	}
	else
	{
		// insert a new chunk
		auto inserted = _container.insert(lower, std::make_pair(_number, std::vector<T> { _data }));
		auto next = inserted;
		++next;
		if (next != _container.end() && next->first == _number + 1)
		{
			std::move(next->second.begin(), next->second.end(), std::back_inserter(inserted->second));
			_container.erase(next);
		}
	}
}

}  // Anonymous namespace -- helper functions.

//debug
void BlockChainSync::printInfo()
{
	return;
	for (std::map<unsigned, std::vector<Header>>::const_iterator it = m_headers.begin(); it != m_headers.end(); it++)
	{
		std::ostringstream stream;
		for ( std::vector<Header>::const_iterator y = it->second.begin(); y != it->second.end(); y++ )
			stream << "," << toString(y->hash);
		LOG(TRACE) << "printHeaders :" << it->first << ",size=" << it->second.size() << "," << stream.str();
	}
	std::map<unsigned, std::vector<bytes>> m_bodies;
	for ( std::map<unsigned, std::vector<bytes>>::const_iterator it = m_bodies.begin(); it != m_bodies.end(); it++)
	{
		LOG(TRACE) << "printBodys :" << it->first << "," << it->second.size();
	}
}

BlockChainSync::BlockChainSync(EthereumHost& _host):
	m_host(_host),
	m_startingBlock(_host.chain().number()),
	m_lastImportedBlock(m_startingBlock),
	m_lastImportedBlockHash(_host.chain().currentHash())
{
	m_bqRoomAvailable = host().bq().onRoomAvailable([this]()
	{
		RecursiveGuard l(x_sync);
		LOG(TRACE) << "onRoomAvailable set state=SyncState::Blocks, before=" << EthereumHost::stateName(m_state);
		auto raw_state = m_state;
		m_state = SyncState::Blocks;
		bool ret = continueSync();
		if (!ret) {
			m_state = raw_state;
		}
	});
}

BlockChainSync::~BlockChainSync()
{
	RecursiveGuard l(x_sync);
	abortSync();
}

void BlockChainSync::onForceSync() {
	RecursiveGuard l(x_sync);
	if (m_state != SyncState::Blocks) {
		LOG(TRACE) << "onForceSync set state=SyncState::Blocks, before=" << EthereumHost::stateName(m_state);
		auto raw_state = m_state;
		m_state = SyncState::Blocks;
		bool ret = continueSync();
		if (!ret) {
			m_state = raw_state;
		}
	}
}

void BlockChainSync::onBlockImported(BlockHeader const& _info)
{
	//if a block has been added via mining or other block import function
	//through RPC, then we should count it as a last imported block
	RecursiveGuard l(x_sync);
	if (_info.number() > m_lastImportedBlock)
	{
		m_lastImportedBlock = static_cast<unsigned>(_info.number());
		m_lastImportedBlockHash = _info.hash();
		m_highestBlock = max(m_lastImportedBlock, m_highestBlock);

		auto head = findItem(m_headers, m_lastImportedBlock);
		if (head && head->hash == m_lastImportedBlockHash) { 
			LOG(TRACE) << "BlockChainSync::onBlockImported remove header&body blk=" << m_lastImportedBlock << ",hash=" << m_lastImportedBlockHash;
			removeItem(m_headers, m_lastImportedBlock);
			removeItem(m_bodies, m_lastImportedBlock);
		}
		LOG(INFO) << "BlockChainSync::onBlockImported remove downloading blk=" << m_lastImportedBlock << ",hash=" << m_lastImportedBlockHash;
		m_headerIdToNumber.erase(m_lastImportedBlockHash);
		m_downloadingBodies.erase(m_lastImportedBlock);
		m_downloadingHeaders.erase(m_lastImportedBlock);
		if (m_headers.empty()) {
			if (!m_bodies.empty()) {
				LOG(TRACE) << "Block headers map is empty, but block bodies map is not. Force-clearing.";
				m_bodies.clear();
			}
			completeSync();
			LOG(TRACE) << "BlockChainSync::onBlockImported completeSync";
		}
	}
}

void BlockChainSync::abortSync()
{
	resetSync();
	host().foreachPeer([&](std::shared_ptr<EthereumPeer> _p)
	{
		_p->abortSync();
		return true;
	});
}

void BlockChainSync::onPeerStatus(std::shared_ptr<EthereumPeer> _peer)
{
	RecursiveGuard l(x_sync);
	DEV_INVARIANT_CHECK;
	std::shared_ptr<SessionFace> session = _peer->session();
	if (!session)
		return; // Expired
	if (_peer->m_genesisHash != host().chain().genesisHash())
		_peer->disable("Invalid genesis hash");
	else if (_peer->m_protocolVersion != host().protocolVersion() && _peer->m_protocolVersion != EthereumHost::c_oldProtocolVersion)
		_peer->disable("Invalid protocol version.");
	else if (_peer->m_networkId != host().networkId())
		_peer->disable("Invalid network identifier.");
	else if (session->info().clientVersion.find("/v0.7.0/") != string::npos)
		_peer->disable("Blacklisted client version.");
	else if (host().isBanned(session->id()))
		_peer->disable("Peer banned for previous bad behaviour.");
	else if (_peer->m_asking != Asking::State && _peer->m_asking != Asking::Nothing)
		_peer->disable("Peer banned for unexpected status message.");
	else {
		LOG(INFO) << "onPeerStatus call syncPeer";
		syncPeer(_peer, false);
	}
}

bool BlockChainSync::syncPeer(std::shared_ptr<EthereumPeer> _peer, bool _force)
{
	printInfo();

	LOG(TRACE) << "syncPeer: _force=" << _force << ",m_state=" << EthereumHost::stateName(m_state) << ",asking=" << EthereumPeer::toString(_peer->m_asking) << ",peer=" << _peer->id().abridged();

	if (_peer->m_asking != Asking::Nothing)
	{
		LOG(TRACE) << "Can't sync with this peer - outstanding asks.";
		return false;
	}

	if (m_state == SyncState::Waiting) { 
		LOG(TRACE) << "Can't sync with this peer - waiting.";
		return false;
	}

	u256 td = host().chain().details().totalDifficulty;
	if (host().bq().isActive())
		td += host().bq().difficulty();

	u256 syncingDifficulty = std::max(m_syncingTotalDifficulty, td);

	LOG(TRACE) << "syncPeer: _force=" << _force  << ",td=" << _peer->m_totalDifficulty << syncingDifficulty << m_syncingTotalDifficulty;

	if (_force || _peer->m_totalDifficulty > syncingDifficulty)
	{
		// start sync
		m_syncingTotalDifficulty = _peer->m_totalDifficulty;

		if (m_state == SyncState::Idle || m_state == SyncState::NotSynced) {
			LOG(TRACE) << "syncPeer set state=SyncState::Blocks, before=" << EthereumHost::stateName(m_state);
			m_state = SyncState::Blocks;
		}

		m_downloadingHeaders.insert(static_cast<unsigned>(_peer->m_height));
		m_headerSyncPeers[_peer].push_back(static_cast<unsigned>(_peer->m_height));
		_peer->requestBlockHeaders(_peer->m_latestHash, 1, 0, false);
		LOG(INFO) << "syncPeer:requestBlockHeaders, blk=" << _peer->m_height << ",peer=" << _peer->id().abridged();
		_peer->m_requireTransactions = true;
		return true;
	}

	if (m_state == SyncState::Blocks)
	{
		LOG(INFO) << "syncPeer:requestBlocks, peer=" << _peer->id().abridged();
		return requestBlocks(_peer);
	}

	return false;
}

bool BlockChainSync::continueSync()
{
	bool ret = false;
	host().foreachPeer([&](std::shared_ptr<EthereumPeer> _p)
	{
		ret = this->syncPeer(_p, false) || ret; 
		return true;
	});
	return ret;
}

bool BlockChainSync::requestBlocks(std::shared_ptr<EthereumPeer> _peer)
{
	printInfo();
	//LOG(TRACE)<<"BlockChainSync::requestBlocks m_haveCommonHeader="<<m_haveCommonHeader<<",peer=" << _peer->id();

	clearPeerDownload(_peer); 
	if (host().bq().knownFull())
	{
		LOG(INFO) << "Waiting for block queue before downloading blocks";
		pauseSync();
		return false;
	}
	// check to see if we need to download any block bodies first
	auto header = m_headers.begin();
	h256s neededBodies;
	vector<unsigned> neededNumbers;
	unsigned index = 0;
	if (m_haveCommonHeader && !m_headers.empty() && m_headers.begin()->first == m_lastImportedBlock + 1)
	{
		while (header != m_headers.end() && neededBodies.size() < c_maxRequestBodies && index < header->second.size())
		{
			unsigned block = header->first + index;
			if (m_downloadingBodies.count(block) == 0 && !haveItem(m_bodies, block) && _peer->m_height >= block)
			{
				neededBodies.push_back(header->second[index].hash);
				neededNumbers.push_back(block);
				m_downloadingBodies.insert(block);
			}

			++index;
			if (index >= header->second.size())
				break; // Download bodies only for validated header chain
		}
	}

	if (neededBodies.size() > 0)
	{
		m_bodySyncPeers[_peer] = neededNumbers;
		_peer->requestBlockBodies(neededBodies);
		LOG(INFO) << "requestBlockBodies, size=" << neededBodies.size() << ",peer=" << _peer->id().abridged();
		return true;
	}
	else
	{
		// check if need to download headers
		unsigned start = 0;
		if (!m_haveCommonHeader)
		{	
			// download backwards until common block is found 1 header at a time
			start = m_lastImportedBlock;
			if (!m_headers.empty())
				start = std::min(start, m_headers.begin()->first - 1);
			m_lastImportedBlock = start;
			m_lastImportedBlockHash = host().chain().numberHash(start);

			if (start == 0){
				m_haveCommonHeader = ( true); //reached genesis
				return requestBlocks(_peer);
			}
		}

		if (m_haveCommonHeader)
		{
			start = m_lastImportedBlock + 1;
			auto next = m_headers.begin();
			unsigned count = 0;
			if (!m_headers.empty() && start >= m_headers.begin()->first)
			{
				start = m_headers.begin()->first + m_headers.begin()->second.size();
				++next;
			}

			while (count == 0 && next != m_headers.end())
			{
				count = std::min(c_maxRequestHeaders, next->first - start);
				while (count > 0 && (m_downloadingHeaders.count(start) != 0 || _peer->m_height < start))
				{
					start++;
					count--;
				}

				std::vector<unsigned> headers;
				for (unsigned block = start; block < start + count; block++) {
					//if (m_downloadingHeaders.count(block) == 0)
					
					headers.push_back(block);
					m_downloadingHeaders.insert(block);
					
				}
				count = headers.size();


				if (count > 0)
				{
					m_headerSyncPeers[_peer] = headers;
					assert(!haveItem(m_headers, start));
					_peer->requestBlockHeaders(start, count, 0, false);
					LOG(INFO) << "requestBlockHeaders, start=" << start << ",count=" << count << ",peer=" << _peer->id().abridged();
					return true;
				}
				else if (start >= next->first)
				{
					start = next->first + next->second.size();
					++next;
				}
			}
		}
		else {
			m_downloadingHeaders.insert(start);
			m_headerSyncPeers[_peer].push_back(start);
			_peer->requestBlockHeaders(start, 1, 0, false);
			LOG(INFO) << "requestBlockHeaders, start=" << start << ",count=1,peer=" << _peer->id().abridged();
			return true;
		}
	}

	return false;
}

void BlockChainSync::clearPeerDownload(std::shared_ptr<EthereumPeer> _peer)
{
	ostringstream oss1, oss2;
	auto syncPeer = m_headerSyncPeers.find(_peer);
	if (syncPeer != m_headerSyncPeers.end())
	{
		for (unsigned block : syncPeer->second) {
			m_downloadingHeaders.erase(block);
			oss1 << block << ",";
		}
		m_headerSyncPeers.erase(syncPeer);
	}
	syncPeer = m_bodySyncPeers.find(_peer);
	if (syncPeer != m_bodySyncPeers.end())
	{
		for (unsigned block : syncPeer->second) {
			m_downloadingBodies.erase(block);
			oss2 << block << ",";
		}
		m_bodySyncPeers.erase(syncPeer);
	}

	LOG(TRACE) << "clearPeerDownload: peer=" << _peer->id().abridged() << ", delete_header=" << oss1.str() << "delete_body=" << oss2.str();
}

void BlockChainSync::clearPeerDownloadHeaders(std::shared_ptr<EthereumPeer> _peer)
{
	ostringstream oss1;
	auto syncPeer = m_headerSyncPeers.find(_peer);
	if (syncPeer != m_headerSyncPeers.end())
	{
		for (unsigned block : syncPeer->second) {
			m_downloadingHeaders.erase(block);
			oss1 << block << ",";
		}
		m_headerSyncPeers.erase(syncPeer);
	}

	LOG(TRACE) << "clearPeerDownloadHeaders: peer=" << _peer->id().abridged() << ", delete_header=" << oss1.str();
}

void BlockChainSync::clearPeerDownloadBodies(std::shared_ptr<EthereumPeer> _peer)
{
	ostringstream oss2;
	auto syncPeer = m_bodySyncPeers.find(_peer);
	if (syncPeer != m_bodySyncPeers.end())
	{
		for (unsigned block : syncPeer->second) {
			m_downloadingBodies.erase(block);
			oss2 << block << ",";
		}
		m_bodySyncPeers.erase(syncPeer);
	}

	LOG(TRACE) << "clearPeerDownloadBodies: peer=" << _peer->id().abridged() << "delete_body=" << oss2.str();
}

void BlockChainSync::clearPeerDownload()
{
	for (auto s = m_headerSyncPeers.begin(); s != m_headerSyncPeers.end();)
	{
		if (s->first.expired())
		{
			for (unsigned block : s->second)
				m_downloadingHeaders.erase(block);
			m_headerSyncPeers.erase(s++);
		}
		else
			++s;
	}
	for (auto s = m_bodySyncPeers.begin(); s != m_bodySyncPeers.end();)
	{
		if (s->first.expired())
		{
			for (unsigned block : s->second)
				m_downloadingBodies.erase(block);
			m_bodySyncPeers.erase(s++);
		}
		else
			++s;
	}
}

void BlockChainSync::logNewBlock(h256 const& _h)
{
	if (m_state == SyncState::NewBlocks)
		LOG(INFO) << "NewBlock: " << _h;
	m_knownNewHashes.erase(_h);
}

void BlockChainSync::onPeerBlockHeaders(std::shared_ptr<EthereumPeer> _peer, RLP const& _r)
{
	//LOG(TRACE)<<"BlockChainSync::onPeerBlockHeaders "<<m_haveCommonHeader<<",peer=" << _peer->id();;
	printInfo();

	RecursiveGuard l(x_sync);
	DEV_INVARIANT_CHECK;
	size_t itemCount = _r.itemCount();
	LOG(INFO) << "BlocksHeaders (" << dec << itemCount << "entries)" << (itemCount ? "" : ": NoMoreHeaders") << ",from=" << _peer->id().abridged();
	//clearPeerDownloadHeaders(_peer);
	clearPeerDownload(_peer);
	if (m_state != SyncState::Blocks && m_state != SyncState::NewBlocks && m_state != SyncState::Waiting)
	{
		LOG(INFO) << "Ignoring unexpected blocks" << EthereumHost::stateName(m_state);
		return;
	}
	if (m_state == SyncState::Waiting)
	{
		LOG(INFO) << "Ignored blocks while waiting";
		return;
	}
	if (itemCount == 0)
	{
		LOG(INFO) << "Peer does not have the blocks requested";
		_peer->addRating(-1);
	}
	for (unsigned i = 0; i < itemCount; i++)
	{
		BlockHeader info(_r[i].data(), HeaderData);
		unsigned blockNumber = static_cast<unsigned>(info.number());

		if (haveItem(m_headers, blockNumber))
		{
			LOG(INFO) << "block in m_headers, Skipping header " << blockNumber;
			continue;
		}
		if (blockNumber <= m_lastImportedBlock && m_haveCommonHeader )
		{
			LOG(INFO) << "block small than m_lastImportedBlock, Skipping header " << blockNumber;
			continue;
		}
		if (blockNumber > m_highestBlock)
			m_highestBlock = blockNumber;

		auto status = host().bq().blockStatus(info.hash());

		LOG(TRACE) << "BlockChainSync::onPeerBlockHeaders status=" << (unsigned)status << ",isKown=" << toString(host().chain().isKnown(info.hash()));
		if (status == QueueStatus::Importing || status == QueueStatus::Ready || host().chain().isKnown(info.hash()))
		{
			m_haveCommonHeader = ( true);
			m_lastImportedBlock = blockNumber;
			m_lastImportedBlockHash = info.hash();
		}
		else
		{
			Header hdr { _r[i].data().toBytes(), info.hash(), info.parentHash() };

			// validate chain
			//HeaderId headerId { info.transactionsRoot(), info.sha3Uncles() };
			if (m_haveCommonHeader)
			{
				
				Header const* prevBlock = findItem(m_headers, blockNumber - 1);


				if ((prevBlock && prevBlock->hash != info.parentHash()) || (blockNumber == m_lastImportedBlock + 1 && info.parentHash() != m_lastImportedBlockHash))
				{
					//LOG(TRACE)<<"BlockChainSync::onPeerBlockHeaders parentHash="<<toString(info.parentHash())<<",blockNumber="<<blockNumber<<",m_lastImportedBlock="<<m_lastImportedBlock<<",m_lastImportedBlockHash="<<toString(m_lastImportedBlockHash);
					//if( prevBlock )
					//	LOG(TRACE)<<",prevBlock->hash="<<toString(prevBlock->hash);

					// mismatching parent id, delete the previous block and don't add this one
					LOG(INFO) << "Unknown block header " << blockNumber << " " << info.hash() << " (Restart syncing)";
					_peer->addRating(-1);
					restartSync(); 
					return ;
				}

				Header const* nextBlock = findItem(m_headers, blockNumber + 1);

				if (nextBlock && nextBlock->parent != info.hash())
				{
					LOG(INFO) << "Unknown block header " << blockNumber + 1 << " " << nextBlock->hash;
					// clear following headers
					unsigned n = blockNumber + 1;
					auto headers = m_headers.at(n);
					for (auto const& h : headers)
					{
						//BlockHeader deletingInfo(h.data, HeaderData);
						//HeaderId header_id = { deletingInfo.transactionsRoot(), deletingInfo.sha3Uncles() };
						m_headerIdToNumber.erase(h.hash);
						m_downloadingBodies.erase(n);
						m_downloadingHeaders.erase(n);
						++n;
					}
					removeAllStartingWith(m_headers, blockNumber + 1);
					removeAllStartingWith(m_bodies, blockNumber + 1);
				}
			}

			//LOG(TRACE)<<"BlockChainSync::handlePeerBlockHeaders "<<m_haveCommonHeader;
			//printInfo();
			mergeInto(m_headers, blockNumber, std::move(hdr));
			
			m_headerIdToNumber[info.hash()] = blockNumber;
			
		}
	}
	collectBlocks();
	continueSync();
}

void BlockChainSync::onPeerBlockBodies(std::shared_ptr<EthereumPeer> _peer, RLP const& _r)
{
	//LOG(TRACE)<<"BlockChainSync::onPeerBlockBodies "<<m_haveCommonHeader<<",peer=" << _peer->id();;
	printInfo();

	RecursiveGuard l(x_sync);
	DEV_INVARIANT_CHECK;
	size_t itemCount = _r.itemCount();
	LOG(INFO) << "BlocksBodies (" << dec << itemCount << "entries)" << (itemCount ? "" : ": NoMoreBodies") << ",from=" << _peer->id().abridged();
	//clearPeerDownloadBodies(_peer);
	clearPeerDownload(_peer);
	if (m_state != SyncState::Blocks && m_state != SyncState::NewBlocks && m_state != SyncState::Waiting) {
		LOG(INFO) << "Ignoring unexpected blocks" << EthereumHost::stateName(m_state);
		return;
	}
	if (m_state == SyncState::Waiting)
	{
		LOG(INFO) << "Ignored blocks while waiting";
		return;
	}
	if (itemCount == 0)
	{
		LOG(INFO) << "Peer does not have the blocks requested";
		_peer->addRating(-1);
	}
	for (unsigned i = 0; i < itemCount; i++)
	{
		RLP body(_r[i]);

		auto id = body[2].toHash<h256>();
		auto iter = m_headerIdToNumber.find(id);
		if (iter == m_headerIdToNumber.end())
		{
			LOG(INFO) << "Ignored unknown block body, not in m_headerIdToNumber";
			continue;
		}

		unsigned blockNumber = iter->second;
		Header const* header = findItem(m_headers, blockNumber);
		if (header == nullptr) {
			LOG(INFO) << "Ignored unknown block body " << blockNumber;
			continue;
		}

		if (haveItem(m_bodies, blockNumber))
		{
			LOG(INFO) << "Skipping already downloaded block body " << blockNumber;
			continue;
		}
		auto txList = body[0];
		h256 transactionRoot = trieRootOver(txList.itemCount(), [&](unsigned i) { return rlp(i); }, [&](unsigned i) { return txList[i].data().toBytes(); });
		h256 uncles = sha3(body[1].data());
		//HeaderId id { transactionRoot, uncles };
		auto bi = BlockHeader(header->data, HeaderData);
		if (transactionRoot != bi.transactionsRoot() || uncles != bi.sha3Uncles()) {
			LOG(INFO) << "Ignored illegal downloaded block body " << blockNumber;
			continue;
		}

		m_headerIdToNumber.erase(id);
		mergeInto(m_bodies, blockNumber, body.data().toBytes());
	}
	collectBlocks();
	continueSync();
}

void BlockChainSync::collectBlocks()
{
	printInfo();
	
	if (m_haveCommonHeader && m_headers.empty() && m_downloadingHeaders.empty() && m_downloadingBodies.empty()) {
		if (!m_bodies.empty())
		{
			LOG(INFO) << "Block headers map is empty, but block bodies map is not. Force-clearing.";
			m_bodies.clear();
		}
		completeSync();
		LOG(INFO) << "collectBlocks: (0) completeSync";
		return;
	}

	if (!m_haveCommonHeader || m_headers.empty() || m_bodies.empty()) {
		LOG(INFO) << "collectBlocks: (1)" << m_haveCommonHeader << m_headers.empty() << m_bodies.empty();
		return;
	}

	// merge headers and bodies
	auto& headers = *m_headers.begin();
	auto& bodies = *m_bodies.begin();
	if (headers.first != bodies.first || headers.first != m_lastImportedBlock + 1) {
		LOG(INFO) << "collectBlocks: (2)" << headers.first << ", " << bodies.first << ", " << m_lastImportedBlock;
		return;
	}

	unsigned success = 0;
	unsigned future = 0;
	unsigned got = 0;
	unsigned unknown = 0;
	size_t i = 0;
	for (; i < headers.second.size() && i < bodies.second.size(); i++)
	{
		RLPStream blockStream(5);
		blockStream.appendRaw(headers.second[i].data); // header
		RLP body(bodies.second[i]);
		blockStream.appendRaw(body[0].data()); // tx
		blockStream.appendRaw(body[1].data()); // uncles
		blockStream.appendRaw(body[2].data()); // hash
		blockStream.appendRaw(body[3].data()); // sign_list
		bytes block;
		blockStream.swapOut(block);
		auto block_number = headers.first + (unsigned)i;
		switch (host().bq().import(&block))
		{
		case ImportResult::Success:
			success++;
			if (block_number > m_lastImportedBlock)
			{
				m_lastImportedBlock = block_number;
				m_lastImportedBlockHash = headers.second[i].hash;
			}
			m_highestBlock = max(m_lastImportedBlock, m_highestBlock);
			m_downloadingBodies.erase(block_number);
			m_downloadingHeaders.erase(block_number);
			if (block_number % 100 == 0) {
				LOG(INFO) << "BLOCK_TIMESTAMP_STAT:[" << toString(headers.second[i].hash) <<"][" << block_number << "][" << utcTime() << "][" << "BlockChainSync::collectBlocks" << "]";
			}
			break;
		case ImportResult::Malformed:
		case ImportResult::BadChain:
			LOG(INFO) << "collectBlocks BadChain";
			restartSync();
			return;

		case ImportResult::FutureTimeKnown:
			future++;
			break;
		case ImportResult::AlreadyInChain:
		case ImportResult::AlreadyKnown:
			++got;
			break;
		case ImportResult::FutureTimeUnknown:
		case ImportResult::UnknownParent:
			++unknown;
			if (headers.first + i > m_lastImportedBlock)
			{
				resetSync();
				LOG(INFO) << "collectBlocks (3) set m_haveCommonHeader=false block_number=" << block_number;
				m_haveCommonHeader = false; // fork detected, search for common header again
			}
			return;

		default:;
		}
	}

	LOG(INFO) << dec << success << "imported OK," << unknown << "with unknown parents," << future << "with future timestamps," << got << " already known received.";

	if (host().bq().unknownFull())
	{
		LOG(WARNING) << "Too many unknown blocks, restarting sync";
		restartSync();
		return;
	}

	auto newHeaders = std::move(headers.second);
	newHeaders.erase(newHeaders.begin(), newHeaders.begin() + i);
	unsigned newHeaderHead = headers.first + i;
	auto newBodies = std::move(bodies.second);
	newBodies.erase(newBodies.begin(), newBodies.begin() + i);
	unsigned newBodiesHead = bodies.first + i;
	m_headers.erase(m_headers.begin());
	m_bodies.erase(m_bodies.begin());
	if (!newHeaders.empty())
		m_headers[newHeaderHead] = newHeaders;
	if (!newBodies.empty())
		m_bodies[newBodiesHead] = newBodies;

	if (m_headers.empty())
	{
		LOG(INFO) << "collectBlocks completeSync";
		assert(m_bodies.empty());
		completeSync();
	}
	DEV_INVARIANT_CHECK_HERE;
}

void BlockChainSync::onPeerNewBlock(std::shared_ptr<EthereumPeer> _peer, RLP const& _r)
{
	RecursiveGuard l(x_sync);
	DEV_INVARIANT_CHECK;

	if (_r.itemCount() != 2)
	{
		_peer->disable("NewBlock without 2 data fields.");
		return;
	}
	BlockHeader info(_r[0][0].data(), HeaderData);
	auto h = info.hash();
	LOG(INFO) << "onPeerNewBlock: blk=" << info.number() << ",hash=" << h;
	DEV_GUARDED(_peer->x_knownBlocks)
	{
		if (_peer->m_knownBlocks.size() > EthereumPeer::kKnownBlockSize) {
			_peer->m_knownBlocks.pop();
		}
		_peer->m_knownBlocks.insert(h);
	}
	if (info.number() > _peer->m_height) {
		_peer->m_height = info.number();
		_peer->m_latestHash = h;
	}
	u256 totalDifficulty = _r[1].toInt<u256>();
	u256 rawPeerTd = _peer->m_totalDifficulty;
	if (totalDifficulty > _peer->m_totalDifficulty) {
		_peer->m_totalDifficulty = totalDifficulty;
	}

	unsigned blockNumber = static_cast<unsigned>(info.number());
	if (blockNumber > (m_lastImportedBlock + 1))
	{
		LOG(INFO) << "Received unknown new block, height=" << blockNumber << ",hash=" << h << "m_lastImportedBlock=" << m_lastImportedBlock;
		syncPeer(_peer, true);
		return;
	}

	switch (host().bq().import(_r[0].data()))
	{
	case ImportResult::Success:
		_peer->addRating(100);
		logNewBlock(h);
		if (blockNumber > m_lastImportedBlock)
		{
			m_lastImportedBlock = max(m_lastImportedBlock, blockNumber);
			m_lastImportedBlockHash = h;
		}
		m_highestBlock = max(m_lastImportedBlock, m_highestBlock);
		m_downloadingBodies.erase(blockNumber);
		m_downloadingHeaders.erase(blockNumber);
		removeItem(m_headers, blockNumber);
		removeItem(m_bodies, blockNumber);
		if (m_headers.empty())
		{
			if (!m_bodies.empty())
			{
				LOG(INFO) << "Block headers map is empty, but block bodies map is not. Force-clearing.";
				m_bodies.clear();
			}
			completeSync();
		}
		break;
	case ImportResult::FutureTimeKnown:
		//TODO: Rating dependent on how far in future it is.
		break;

	case ImportResult::Malformed:
	case ImportResult::BadChain:
		logNewBlock(h);
		_peer->disable("Malformed block received.");
		return;

	case ImportResult::AlreadyInChain:
	case ImportResult::AlreadyKnown:
		break;

	case ImportResult::FutureTimeUnknown:
	case ImportResult::UnknownParent:
	{
		_peer->m_unknownNewBlocks++;
		if (_peer->m_unknownNewBlocks > c_maxPeerUknownNewBlocks)
		{
			_peer->disable("Too many uknown new blocks");
			restartSync();
		}
		logNewBlock(h);
		//u256 totalDifficulty = _r[1].toInt<u256>();
		if (totalDifficulty > rawPeerTd)
		{
			LOG(INFO) << "Received block with no known parent. Peer needs syncing...";
			syncPeer(_peer, true);
		}
		break;
	}
	default:;
	}
}

SyncStatus BlockChainSync::status() const
{
	RecursiveGuard l(x_sync);
	SyncStatus res;
	res.state = m_state;
	res.protocolVersion = 62;
	res.startBlockNumber = m_startingBlock;
	res.currentBlockNumber = host().chain().number();
	res.highestBlockNumber = m_highestBlock;
	return res;
}

void BlockChainSync::resetSync()
{
	m_downloadingHeaders.clear();
	m_downloadingBodies.clear();
	m_headers.clear();
	m_bodies.clear();
	m_headerSyncPeers.clear();
	m_bodySyncPeers.clear();
	m_headerIdToNumber.clear();
	m_syncingTotalDifficulty = 0;
	m_state = SyncState::NotSynced;
}

void BlockChainSync::restartSync()
{
	RecursiveGuard l(x_sync);
	resetSync();
	m_highestBlock = 0;
	m_haveCommonHeader = false;
	host().bq().clear();
	m_startingBlock = host().chain().number();
	m_lastImportedBlock = m_startingBlock;
	m_lastImportedBlockHash = host().chain().currentHash();
}

void BlockChainSync::completeSync()
{
	resetSync();
	m_state = SyncState::Idle;
}

void BlockChainSync::pauseSync()
{
	m_state = SyncState::Waiting;
}

bool BlockChainSync::isSyncing() const
{
	return m_state != SyncState::Idle;
}

void BlockChainSync::onPeerNewHashes(std::shared_ptr<EthereumPeer> _peer, RLP const& _r)
{
	RecursiveGuard l(x_sync);
	unsigned itemCount = _r.itemCount();
	std::vector<std::pair<h256, u256>> hashes(itemCount);
	for (unsigned i = 0; i < itemCount; ++i)
		hashes[i] = std::make_pair(_r[i][0].toHash<h256>(), _r[i][1].toInt<u256>());

	DEV_INVARIANT_CHECK;
	if (_peer->isConversing())
	{
		LOG(INFO) << "Ignoring new hashes since we're already downloading." << EthereumPeer::toString(_peer->m_asking);
		return;
	}

	unsigned knowns = 0;
	unsigned unknowns = 0;
	//unsigned maxHeight = 0;
	for (auto const& p : hashes)
	{
		h256 const& h = p.first;
		_peer->addRating(1);
		DEV_GUARDED(_peer->x_knownBlocks)
		{
			if (_peer->m_knownBlocks.size() > EthereumPeer::kKnownBlockSize) {
				_peer->m_knownBlocks.pop();
			}
			_peer->m_knownBlocks.insert(h);
		}
		if (p.second > _peer->m_height)
		{
			//maxHeight = (unsigned)p.second;
			_peer->m_height = p.second;
			_peer->m_latestHash = h;
		}

		auto status = host().bq().blockStatus(h);
		if (status == QueueStatus::Importing || status == QueueStatus::Ready || host().chain().isKnown(h))
			knowns++;
		else if (status == QueueStatus::Bad)
		{
			LOG(WARNING) << "block hash bad!" << h << ". Bailing...";
			return;
		}
		else if (status == QueueStatus::Unknown)
		{
			LOG(INFO) << "recv unknown BlockHash, height=" << p.second << ",hash=" << h;
			unknowns++;
			//if (p.second > maxHeight)
			/*if (p.second > _peer->m_height)
			{
				//maxHeight = (unsigned)p.second;
				_peer->m_height = p.second;
				_peer->m_latestHash = h;
			}*/
		}
		else
			knowns++;
	}
	LOG(INFO) << "onPeerNewHashes:" << itemCount << " newHashes," << knowns << " knowns," << unknowns << " unknowns" << ",peer=" << _peer->id().abridged();
	if (unknowns > 0)
	{
		LOG(INFO) << "Not syncing and new block hash discovered: syncing." << _peer->id().abridged();
		syncPeer(_peer, true);
	}
}

void BlockChainSync::onPeerAborting()
{
	RecursiveGuard l(x_sync);
	// Can't check invariants here since the peers is already removed from the list and the state is not updated yet.
	clearPeerDownload();
	LOG(INFO) << "onPeerAborting call continueSync";
	continueSync();
	DEV_INVARIANT_CHECK_HERE;
}

bool BlockChainSync::invariants() const
{	/*
	if (!isSyncing() && !m_headers.empty())
		BOOST_THROW_EXCEPTION(FailedInvariant() << errinfo_comment("Got headers while not syncing"));
	if (!isSyncing() && !m_bodies.empty())
		BOOST_THROW_EXCEPTION(FailedInvariant() << errinfo_comment("Got bodies while not syncing"));
	if (isSyncing() && m_host.chain().number() > 0 && m_haveCommonHeader && m_lastImportedBlock == 0)
		BOOST_THROW_EXCEPTION(FailedInvariant() << errinfo_comment("Common block not found"));
	if (isSyncing() && !m_headers.empty() &&  m_lastImportedBlock >= m_headers.begin()->first)
		BOOST_THROW_EXCEPTION(FailedInvariant() << errinfo_comment("Header is too old"));
	if (m_headerSyncPeers.empty() != m_downloadingHeaders.empty())
		BOOST_THROW_EXCEPTION(FailedInvariant() << errinfo_comment("Header download map mismatch"));
	if (m_bodySyncPeers.empty() != m_downloadingBodies.empty() && m_downloadingBodies.size() <= m_headerIdToNumber.size())
		BOOST_THROW_EXCEPTION(FailedInvariant() << errinfo_comment("Body download map mismatch"));
	*/
	return true;
}
