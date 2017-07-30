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


#include <libethcore/ChainOperationParams.h>
#include <libethcore/CommonJS.h>
#include <libethereum/Interface.h>
#include <libethereum/BlockChain.h>
#include <libethereum/EthereumHost.h>
#include <libethereum/NodeConnParamsManagerApi.h>
#include <libdevcrypto/Common.h>
#include "PBFT.h"
#include <libdevcore/easylog.h>
using namespace std;
using namespace dev;
using namespace eth;

static const unsigned kCollectInterval = 60; // second

void PBFT::init()
{
	ETH_REGISTER_SEAL_ENGINE(PBFT);
}

PBFT::PBFT()
{
}

void PBFT::initEnv(std::weak_ptr<PBFTHost> _host, BlockChain* _bc, OverlayDB* _db, BlockQueue *bq, KeyPair const& _key_pair, unsigned _view_timeout)
{
	Guard l(m_mutex);

	m_host = _host;
	m_bc.reset(_bc);
	m_stateDB.reset(_db);
	m_bq.reset(bq);

	m_bc->setSignChecker([this](BlockHeader const & _header, std::vector<std::pair<u256, Signature>> _sign_list) {
		return checkBlockSign(_header, _sign_list);
	});

	m_key_pair = _key_pair;

	resetConfig();

	m_view_timeout = _view_timeout;
	m_consensus_block_number = 0;
	m_last_consensus_time =  utcTime(); //std::chrono::system_clock::now();
	m_change_cycle = 0;
	m_to_view = 0;
	m_leader_failed = false;

	m_last_sign_time = 0;

	m_last_collect_time = std::chrono::system_clock::now();

	m_future_prepare_cache = std::make_pair(Invalid256, PrepareReq());

	LOG(INFO) << "PBFT initEnv success";
}

void PBFT::resetConfig() {
	if (!NodeConnManagerSingleton::GetInstance().getAccountType(m_key_pair.pub(), m_account_type)) {
		LOG(ERROR) << "resetConfig: can't find myself id, stop sealing";
		m_cfg_err = true;
		return;
	}

	auto node_num = NodeConnManagerSingleton::GetInstance().getMinerNum();
	if (node_num == 0) {
		LOG(ERROR) << "resetConfig: miner_num = 0, stop sealing";
		m_cfg_err = true;
		return;
	}

	u256 node_idx;
	if (!NodeConnManagerSingleton::GetInstance().getIdx(m_key_pair.pub(), node_idx)) {
		//BOOST_THROW_EXCEPTION(PbftInitFailed() << errinfo_comment("NodeID not in cfg"));
		LOG(ERROR) << "resetConfig: can't find myself id, stop sealing";
		m_cfg_err = true;
		return;
	}

	if (node_num != m_node_num || node_idx != m_node_idx) {
		m_node_num = node_num;
		m_node_idx = node_idx;
		m_f = (m_node_num - 1 ) / 3;

		m_prepare_cache.clear();
		m_sign_cache.clear();
		m_recv_view_change_req.clear();

		if (!getMinerList(-1, m_miner_list)) {
			LOG(ERROR) << "resetConfig: getMinerList return false";
			m_cfg_err = true;
			return;
		}

		if (m_miner_list.size() != m_node_num) {
			LOG(ERROR) << "resetConfig: m_miner_list.size=" << m_miner_list.size() << ",m_node_num=" << m_node_num;
			m_cfg_err = true;
			return;
		}
		LOG(INFO) << "resetConfig: m_node_idx=" << m_node_idx << ", m_node_num=" << m_node_num;
	}

	m_cfg_err = false;
}

StringHashMap PBFT::jsInfo(BlockHeader const& _bi) const
{
	return { { "number", toJS(_bi.number()) }, { "timestamp", toJS(_bi.timestamp()) } };
}

void PBFT::generateSeal(BlockHeader const& _bi, bytes const& _block_data)
{
	Timer t;
	Guard l(m_mutex);
	if (!broadcastPrepareReq(_bi, _block_data)) {
		LOG(ERROR) << "broadcastPrepareReq failed, " << _bi.number() << _bi.hash(WithoutSeal);
		return;
	}

	LOG(DEBUG) << "generateSeal, blk=" << _bi.number() << ", timecost=" << 1000 * t.elapsed();
}

bool PBFT::shouldSeal(Interface*)
{
	Guard l(m_mutex);

	if (m_cfg_err || m_account_type != EN_ACCOUNT_TYPE_MINER) { // 配置中找不到自己或非记账节点就不出块,
		return false;
	}

	std::pair<bool, u256> ret = getLeader();

	if (!ret.first) {
		return ret.first;
	}
	if (ret.second != m_node_idx) {
		if (auto h = m_host.lock()) {
			h512 node_id = h512(0);
			if (NodeConnManagerSingleton::GetInstance().getPublicKey(ret.second, node_id) && !h->isConnected(node_id)) {
				LOG(ERROR) << "getLeader ret:<" << ret.first << "," << ret.second << ">" << ", need viewchange for disconnected";
				m_last_consensus_time = 0;
				m_signalled.notify_all();
			}
		}
		return false;
	}
	return true;
}

std::pair<bool, u256> PBFT::getLeader() const {
	if (m_leader_failed || m_highest_block.number() == Invalid256) {
		return std::make_pair(false, Invalid256);
	}

	return std::make_pair(true, (m_view + m_highest_block.number()) % m_node_num);
}

void PBFT::reportBlock(BlockHeader const & _b, u256 const &) {
	Guard l(m_mutex);

	m_highest_block = _b;

	if (m_highest_block.number() >= m_consensus_block_number) {
		m_view = m_to_view = m_change_cycle = 0;
		m_leader_failed = false;
		m_last_consensus_time = utcTime();
		m_consensus_block_number = m_highest_block.number() + 1;
		m_recv_view_change_req.clear();
	}

	resetConfig();

	LOG(INFO) << "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ Report: blk=" << m_highest_block.number() << ",hash=" << _b.hash(WithoutSeal).abridged() << ",idx=" << m_highest_block.genIndex() << ", Next: blk=" << m_consensus_block_number;
}

void PBFT::onPBFTMsg(unsigned _id, std::shared_ptr<p2p::Capability> _peer, RLP const & _r) {
	if (_id <= ViewChangeReqPacket) {
		//LOG(INFO) << "onPBFTMsg: id=" << _id;
		u256 idx = u256(0);
		if (!NodeConnManagerSingleton::GetInstance().getIdx(_peer->session()->id(), idx)) {
			LOG(ERROR) << "Recv an pbft msg from unknown peer id=" << _id;
			return;
		}
		handleMsg(_id, idx, _peer->session()->id(), _r[0]);
	} else {
		LOG(ERROR) << "Recv an illegal msg, id=" << _id;
	}
}

void PBFT::workLoop() {
	while (isWorking()) {
		try
		{
			checkTimeout();
			handleFutureBlock();
			collectGarbage();

			std::unique_lock<std::mutex> l(x_signalled);
			m_signalled.wait_for(l, chrono::milliseconds(10));
		} catch (Exception &_e) {
			LOG(ERROR) << _e.what();
		}
	}
}

void PBFT::handleMsg(unsigned _id, u256 const& _from, h512 const& _node, RLP const& _r) {
	Guard l(m_mutex);

	auto now_time = utcTime();
	std::string key;
	PBFTMsg pbft_msg;
	switch (_id) {
	case PrepareReqPacket: {
		PrepareReq req;
		req.populate(_r);
		handlePrepareMsg(_from, req);
		key = req.block_hash.hex();
		pbft_msg = req;
		break;
	}
	case SignReqPacket:	{
		SignReq req;
		req.populate(_r);
		handleSignMsg(_from, req);
		key = req.sig.hex();
		pbft_msg = req;
		break;
	}
	case ViewChangeReqPacket: {
		ViewChangeReq req;
		req.populate(_r);
		handleViewChangeMsg(_from, req);
		key = req.sig.hex() + toJS(req.view);
		pbft_msg = req;
		break;
	}
	default: {
		LOG(ERROR) << "Recv error msg, id=" << _id;
		return;
	}
	}

	bool time_flag = (pbft_msg.timestamp >= now_time) || (now_time - pbft_msg.timestamp < m_view_timeout);
	bool height_flag = (pbft_msg.height > m_highest_block.number()) || (m_highest_block.number() - pbft_msg.height < 10);
	LOG(TRACE) << "key=" << key << ",time_flag=" << time_flag << ",height_flag=" << height_flag;
	if (key.size() > 0 && time_flag && height_flag) {
		std::unordered_set<h512> filter;
		filter.insert(_node);
		h512 gen_node_id = h512(0);
		if (NodeConnManagerSingleton::GetInstance().getPublicKey(pbft_msg.idx, gen_node_id)) {
			filter.insert(gen_node_id);
		}
		broadcastMsg(key, _id, _r.toBytes(), filter);
	}
}

void PBFT::checkTimeout() {
	Timer t;
	bool flag = false;
	{
		Guard l(m_mutex);

		auto now_time = utcTime();
		auto last_time = std::max(m_last_consensus_time, m_last_sign_time);
		unsigned interval = m_view_timeout * std::pow(1.5, m_change_cycle);
		if (now_time - last_time >= interval) {
			m_leader_failed = true;
			m_to_view += 1;
			m_change_cycle += 1;
			m_last_consensus_time = now_time;
			flag = true;
			// 曾经收到的viewchange消息中跟我当前要的不一致就清空
			for (auto iter = m_recv_view_change_req[m_to_view].begin(); iter != m_recv_view_change_req[m_to_view].end();) {
				if (iter->second.height < m_highest_block.number()) {
					iter = m_recv_view_change_req[m_to_view].erase(iter);
				} else {
					++iter;
				}
			}
			if (!broadcastViewChangeReq()) {
				LOG(ERROR) << "broadcastViewChangeReq failed";
				return;
			}
			checkAndChangeView();
			LOG(TRACE) << "checkTimeout timecost=" << t.elapsed();
		}
	}

	if (flag && m_onViewChange) {
		m_onViewChange();
	}
}

void PBFT::handleFutureBlock() {
	Guard l(m_mutex);

	if (m_future_prepare_cache.second.height == m_consensus_block_number) {
		auto bi = BlockHeader(m_future_prepare_cache.second.block);
		if (bi.parentHash() == m_highest_block.hash(WithoutSeal)) {
			LOG(INFO) << "handleFurtureBlock, blk=" << m_future_prepare_cache.second.height;
			handlePrepareMsg(m_future_prepare_cache.first, m_future_prepare_cache.second);
		}
		m_future_prepare_cache = std::make_pair(Invalid256, PrepareReq());
	}
}

void PBFT::recvFutureBlock(u256 const& _from, PrepareReq const& _req) {
	if (m_future_prepare_cache.second.block_hash != _req.block_hash) {
		auto bi = BlockHeader(_req.block);
		auto status = m_bq->blockStatus(bi.parentHash());
		bool waitting = bi.parentHash() == m_prepare_cache.block_hash || m_sign_cache.find(bi.parentHash()) != m_sign_cache.end();
		LOG(DEBUG) << "status=" << (unsigned)status << ",waitting=" << waitting;
		if (status == QueueStatus::Importing || status == QueueStatus::Ready || waitting) {
			m_future_prepare_cache = std::make_pair(_from, _req);
			LOG(INFO) << "Recv an future block prepare, cache it, blk=" << _req.height << ",hash=" << _req.block_hash << ",idx=" << _req.idx;
		}
	}
}

Signature PBFT::signBlock(h256 const & _hash) {
	return dev::sign(m_key_pair.sec(), _hash);
}

bool PBFT::checkSign(u256 const & _idx, h256 const & _hash, Signature const & _sig) {
	Public pub_id;
	if (!NodeConnManagerSingleton::GetInstance().getPublicKey(_idx, pub_id)) {
		LOG(ERROR) << "Can't find node, idx=" << _idx;
		return false;
	}
	return dev::verify(pub_id, _sig, _hash);
}

bool PBFT::broadcastViewChangeReq() {
	if (m_account_type != EN_ACCOUNT_TYPE_MINER) {
		LOG(INFO) << "Ready to broadcastViewChangeReq, blk=" << m_highest_block.number() << ",to_view=" << m_to_view << ", give up for not miner";
		return true;
	}
	ViewChangeReq req;
	req.height = m_highest_block.number();
	req.view = m_to_view;
	req.idx = m_node_idx;
	req.timestamp = u256(utcTime());
	req.block_hash = m_highest_block.hash(WithoutSeal);
	req.sig = signBlock(req.block_hash);

	RLPStream ts;
	req.streamRLPFields(ts);
	bool ret = broadcastMsg(req.sig.hex() + toJS(req.view), ViewChangeReqPacket, ts.out());
	return ret;
}

bool PBFT::broadcastSignReq(PrepareReq const & _req) {
	SignReq sign_req;
	sign_req.height = _req.height;
	sign_req.view = _req.view;
	sign_req.idx = m_node_idx;
	sign_req.timestamp = u256(utcTime());
	sign_req.block_hash = _req.block_hash;
	sign_req.sig = signBlock(sign_req.block_hash);
	DEV_BLOCK_STAT_LOG(_req.block_hash, _req.height, utcTime(), "broadcastSignReq");
	RLPStream ts;
	sign_req.streamRLPFields(ts);
	if (broadcastMsg(sign_req.sig.hex(), SignReqPacket, ts.out())) {
		addSignReq(sign_req);
		// 重置倒计时，给出足够的时间收集签名
		m_last_sign_time = utcTime();
		return true;
	}
	return false;
}

bool PBFT::broadcastPrepareReq(BlockHeader const & _bi, bytes const & _block_data) {
	PrepareReq req;
	req.height = _bi.number();
	req.view = m_view;
	req.idx = m_node_idx;
	req.timestamp = u256(utcTime());
	req.block_hash = _bi.hash(WithoutSeal);
	req.sig = signBlock(req.block_hash);
	req.block = _block_data;
	DEV_BLOCK_STAT_LOG(req.block_hash, req.height, utcTime(), "broadcastPrepareReq");
	RLPStream ts;
	req.streamRLPFields(ts);
	if (broadcastMsg(req.block_hash.hex(), PrepareReqPacket, ts.out()) && addPrepareReq(req)) {
		m_last_exec_finish_time = static_cast<uint64_t>(req.timestamp);
		checkAndCommit(); // 支持单节点可出块
		return true;
	}
	return false;
}

bool PBFT::broadcastMsg(std::string const & _key, unsigned _id, bytes const & _data, std::unordered_set<h512> const & _filter) {

	if (auto h = m_host.lock()) {
		h->foreachPeer([&](shared_ptr<PBFTPeer> _p)
		{
			unsigned account_type = 0;
			if (!NodeConnManagerSingleton::GetInstance().getAccountType(_p->session()->id(), account_type)) {
				LOG(ERROR) << "Cannot get account type for peer" << _p->session()->id();
				return true;
			}
			if (account_type != EN_ACCOUNT_TYPE_MINER && !m_bc->chainParams().broadcastToNormalNode) {
				return true;
			}

			if (_filter.count(_p->session()->id())) {  // 转发广播
				this->broadcastMark(_key, _id, _p);
				return true;
			}
			if (this->broadcastFilter(_key, _id, _p)) {
				return true;
			}

			RLPStream ts;
			_p->prep(ts, _id, 1).append(_data);
			_p->sealAndSend(ts);
			this->broadcastMark(_key, _id, _p);
			return true;
		});
		return true;
	}
	return false;
}

bool PBFT::broadcastFilter(std::string const & _key, unsigned _id, shared_ptr<PBFTPeer> _p) {
	if (_id == PrepareReqPacket) {
		DEV_GUARDED(_p->x_knownPrepare)
		return _p->m_knownPrepare.exist(_key);
	} else if (_id == SignReqPacket) {
		DEV_GUARDED(_p->x_knownSign)
		return _p->m_knownSign.exist(_key);
	} else if (_id == ViewChangeReqPacket) {
		DEV_GUARDED(_p->x_knownViewChange)
		return _p->m_knownViewChange.exist(_key);
	} else {
		return true;
	}
	return true;
}

void PBFT::broadcastMark(std::string const & _key, unsigned _id, shared_ptr<PBFTPeer> _p) {
	if (_id == PrepareReqPacket) {
		DEV_GUARDED(_p->x_knownPrepare)
		{
			if (_p->m_knownPrepare.size() > kKnownPrepare) {
				_p->m_knownPrepare.pop();
			}
			_p->m_knownPrepare.push(_key);
		}
	} else if (_id == SignReqPacket) {
		DEV_GUARDED(_p->x_knownSign)
		{
			if (_p->m_knownSign.size() > kKnownSign) {
				_p->m_knownSign.pop();
			}
			_p->m_knownSign.push(_key);
		}
	} else if (_id == ViewChangeReqPacket) {
		DEV_GUARDED(_p->x_knownViewChange)
		{
			if (_p->m_knownViewChange.size() > kKnownViewChange) {
				_p->m_knownViewChange.pop();
			}
			_p->m_knownViewChange.push(_key);
		}
	} else {
		// do nothing
	}
}

bool PBFT::isExistPrepare(PrepareReq const & _req) {
	return m_prepare_cache.block_hash == _req.block_hash;
}

bool PBFT::isExistSign(SignReq const & _req) {
	auto iter = m_sign_cache.find(_req.block_hash);
	if (iter == m_sign_cache.end()) {
		return false;
	}
	return iter->second.find(_req.sig.hex()) != iter->second.end();
}

bool PBFT::isExistViewChange(ViewChangeReq const & _req) {
	auto iter = m_recv_view_change_req.find(_req.view);
	if (iter == m_recv_view_change_req.end()) {
		return false;
	}
	return iter->second.find(_req.idx) != iter->second.end();
}

void PBFT::handlePrepareMsg(u256 const & _from, PrepareReq const & _req) {
	u256 t2 = u256(utcTime()) - _req.timestamp;
	Timer t;
	ostringstream oss;
	oss << "handlePrepareMsg: idx=" << _req.idx << ",view=" << _req.view << ",blk=" << _req.height << ",hash=" << _req.block_hash.abridged() << ",from=" << _from;
	LOG(TRACE) << oss.str();

	if (isExistPrepare(_req)) {
		LOG(TRACE) << oss.str()  << "Discard an illegal prepare, duplicated";
		return;
	}

	if (_req.idx == m_node_idx) {
		LOG(ERROR) << oss.str() << "Discard an illegal prepare, your own req";
		return;
	}

	if (_req.view != m_view) {
		LOG(DEBUG) << oss.str()  << "Recv an illegal prepare, m_view=" << m_view << ",m_to_view=" << m_to_view;
		if (_req.view == m_to_view) {
			recvFutureBlock(_from, _req);
			LOG(DEBUG) << oss.str() << " wait to be handled later";
		}
		return;
	}

	auto leader = getLeader();
	if (!leader.first || _req.idx != leader.second) {
		LOG(ERROR) << oss.str()  << "Recv an illegal prepare, err leader";
		return;
	}

	if (_req.height != m_consensus_block_number) {
		LOG(DEBUG) << oss.str() << "Discard an illegal prepare req, need_height=" << m_consensus_block_number;
	}


	if (!checkSign(_req.idx, _req.block_hash, _req.sig)) {
		LOG(ERROR) << oss.str()  << "CheckSign failed";
		return;
	}

	try {
		m_bc->checkBlockValid(_req.block_hash, _req.block, *m_stateDB);
	}
	catch (dev::eth::InvalidParentHash)
	{
		recvFutureBlock(_from, _req);
		return;
	}
	catch (dev::eth::UnknownParent)
	{
		recvFutureBlock(_from, _req);
		return;
	}
	catch (Exception &ex) {
		LOG(ERROR) << oss.str()  << "CheckBlockValid failed" << ex.what();
		return;
	}

	if (!addPrepareReq(_req)) {
		LOG(ERROR) << oss.str()  << "addPrepare failed";
		return;
	}

	if (m_account_type == EN_ACCOUNT_TYPE_MINER && !broadcastSignReq(_req)) {
		LOG(ERROR) << oss.str()  << "broadcastSignReq failed";
		return;
	}

	// TODO: 保存交易

	LOG(INFO) << oss.str() << ", success";
	m_last_exec_finish_time = static_cast<uint64_t>(_req.timestamp);
	checkAndCommit();

	LOG(TRACE) << "handlePrepareMsg, timecost=" << 1000 * t.elapsed() << ",net-time=" << t2;
	return;
}

void PBFT::handleSignMsg(u256 const & _from, SignReq const & _req) {
	u256 t2 = u256(utcTime()) - _req.timestamp;
	Timer t;
	ostringstream oss;
	oss << "handleSignMsg: idx=" << _req.idx << ",view=" << _req.view << ",blk=" << _req.height << ",hash=" <<  _req.block_hash.abridged() << ", from=" << _from;
	LOG(TRACE) << oss.str();

	if (isExistSign(_req)) {
		LOG(TRACE) << oss.str() << "Discard an illegal sign, duplicated";
		return;
	}

	if (_req.idx == m_node_idx) {
		LOG(ERROR) << oss.str() << "Discard an illegal sign, your own req";
		return;
	}

	if (m_prepare_cache.block_hash != _req.block_hash) {
		LOG(DEBUG) << oss.str()  << "Recv a sign_req for block which not in prepareCache, preq=" << m_prepare_cache.block_hash.abridged();

		if (_req.height == m_consensus_block_number && checkSign(_req.idx, _req.block_hash, _req.sig)) {
			addSignReq(_req);
			LOG(DEBUG) << oss.str()  << "Cache this sign_req";
		}
		return;
	}

	if (m_prepare_cache.view != _req.view) {
		LOG(INFO) << oss.str() << "Discard a sign_req which view is not equal, preq.v=" << m_prepare_cache.view;
		return;
	}

	if (!checkSign(_req.idx, _req.block_hash, _req.sig)) {
		LOG(ERROR) << oss.str()  << "CheckSign failed";
		return;
	}

	LOG(INFO) << oss.str() << ", success";

	addSignReq(_req);

	checkAndCommit();

	LOG(TRACE) << "handleSignMsg, timecost=" << 1000 * t.elapsed() << ",net-time=" << t2;
	return;
}

void PBFT::handleViewChangeMsg(u256 const & _from, ViewChangeReq const & _req) {
	u256 t2 = u256(utcTime()) - _req.timestamp;
	Timer t;
	ostringstream oss;
	oss << "handleViewChangeMsg: idx=" << _req.idx << ",view=" << _req.view  << ",blk=" << _req.height << ",hash=" << _req.block_hash.abridged() << ",from=" << _from;
	LOG(TRACE) << oss.str();

	if (isExistViewChange(_req)) {
		LOG(TRACE) << oss.str() << "Discard an illegal viewchange, duplicated";
		return;
	}


	if (_req.idx == m_node_idx) {
		LOG(ERROR) << oss.str() << "Discard an illegal viewchange, your own req";
		return;
	}

	if (_req.height < m_highest_block.number() || _req.view <= m_view) {
		LOG(TRACE) << oss.str() << "Discard an illegal viewchange, m_highest_block=" << m_highest_block.number() << ",m_view=" << m_view;
		return;
	}
	if (_req.height == m_highest_block.number() && _req.block_hash != m_highest_block.hash(WithoutSeal) && m_bc->block(_req.block_hash).size() == 0) {
		LOG(DEBUG) << oss.str() << "Discard an illegal viewchange, same height but not hash, chain has been forked, my=" << m_highest_block.hash(WithoutSeal) << ",req=" << _req.block_hash;
		return;
	}

	if (!checkSign(_req.idx, _req.block_hash, _req.sig)) {
		LOG(ERROR) << oss.str() << "CheckSign failed";
		return;
	}

	LOG(INFO) << oss.str() << ", success";

	m_recv_view_change_req[_req.view][_req.idx] = _req;

	if (_req.view == m_to_view) {
		checkAndChangeView();
	} else  {
		u256 count = u256(0);
		u256 min_view = Invalid256;
		for (auto iter = m_recv_view_change_req.begin(); iter != m_recv_view_change_req.end(); ++iter) {
			if (iter->first > m_to_view) {
				count += iter->second.size();
				if (min_view > iter->first) {
					min_view = iter->first;
				}
			}
		}

		if (count > m_f + 1) {
			LOG(INFO) << "Fast start viewchange, m_to_view=" << m_to_view << ",req.view=" << _req.view;
			m_last_consensus_time = 0;
			m_to_view = min_view - 1; // it will be setted equal to min_view when viewchange happened.
			m_signalled.notify_all();
		}
	}

	LOG(TRACE) << "handleViewChangeMsg, timecost=" << 1000 * t.elapsed() << ",net-time=" << t2;
	return;
}

void PBFT::checkAndCommit() {
	u256 need_sign = m_node_num - m_f;
	u256 have_sign = m_sign_cache[m_prepare_cache.block_hash].size() + 1;
	if (have_sign >= need_sign) {
		LOG(INFO) << "######### Reach enough sign for block=" << m_prepare_cache.height << ",hash=" << m_prepare_cache.block_hash.abridged() << ",have_sign=" << have_sign << ",need_sign=" << need_sign;
		if (m_prepare_cache.view != m_view) {
			LOG(INFO) << "view has changed, discard this block, preq.view=" << m_prepare_cache.view << ",m_view=" << m_view;
			return;
		}
		if (m_prepare_cache.height > m_highest_block.number()) {
			// 把签名加上
			std::vector<std::pair<u256, Signature>> sig_list;
			sig_list.reserve(static_cast<unsigned>(have_sign));
			sig_list.push_back(std::make_pair(m_prepare_cache.idx, m_prepare_cache.sig));
			for (auto item : m_sign_cache[m_prepare_cache.block_hash]) {
				sig_list.push_back(std::make_pair(item.second.idx, Signature(item.first.c_str())));
			}
			RLP r(m_prepare_cache.block);
			RLPStream rs;
			rs.appendList(5);
			rs.appendRaw(r[0].data()); // header
			rs.appendRaw(r[1].data()); // tx
			rs.appendRaw(r[2].data()); // uncles
			rs.appendRaw(r[3].data()); // hash
			rs.appendVector(sig_list); // sign_list

			m_onSealGenerated(rs.out(), m_prepare_cache.idx == m_node_idx);
			//m_commited_block = BlockHeader(m_prepare_cache.block);
			delCache(m_prepare_cache.block_hash);
		} else {
			LOG(INFO) << "Discard this block, blk_no=" << m_prepare_cache.height << ",highest_block=" << m_highest_block.number();
			delCache(m_prepare_cache.block_hash);
		}
	}
}

void PBFT::checkAndChangeView() {
	u256 count = m_recv_view_change_req[m_to_view].size();
	if (count >= m_node_num - m_f - 1) {
		LOG(INFO) << "######### Reach consensus, to_view=" << m_to_view;

		m_leader_failed = false;
		m_view = m_to_view;

		for (auto iter = m_recv_view_change_req.begin(); iter != m_recv_view_change_req.end();) {
			if (iter->first <= m_view) {
				iter = m_recv_view_change_req.erase(iter);
			} else {
				++iter;
			}
		}
	}
}

bool PBFT::addPrepareReq(PrepareReq const & _req) {
	if (m_prepare_cache.block_hash == _req.block_hash) {
		LOG(INFO) << "Discard duplicated prepare block";
		return false;
	}

	if (m_prepare_cache.height == _req.height) {
		if (m_prepare_cache.view < _req.view) {
			LOG(INFO) << "Replace prepare block which has same H but bigger V, req.view=" << _req.view << ",cache.view=" << m_prepare_cache.view;
			m_prepare_cache = _req;
			return true;
		} else {
			LOG(INFO) << "Discard prepare block, req.view=" << _req.view << ",cache.view=" << m_prepare_cache.view;
			return false;
		}
	} else {
		m_prepare_cache = _req;
		return true;
	}

	return false; // can't be here
}

void PBFT::addSignReq(SignReq const & _req) {
	m_sign_cache[_req.block_hash][_req.sig.hex()] = _req;
}

void PBFT::delCache(h256 const & _block_hash) {
	auto iter2 = m_sign_cache.find(_block_hash);
	if (iter2 == m_sign_cache.end()) {
		LOG(INFO) << "Try to delete not-exist, hash=" << _block_hash;
		//BOOST_THROW_EXCEPTION(UnexpectError());
	} else {
		m_sign_cache.erase(iter2);
	}

	m_prepare_cache.clear();
}

void PBFT::collectGarbage() {
	Timer t;
	Guard l(m_mutex);
	if (!m_highest_block) return;

	std::chrono::system_clock::time_point now_time = std::chrono::system_clock::now();
	if (now_time - m_last_collect_time >= std::chrono::seconds(kCollectInterval)) {
		for (auto iter = m_sign_cache.begin(); iter != m_sign_cache.end();) {
			for (auto iter2 = iter->second.begin(); iter2 != iter->second.end();) {
				if (iter2->second.height < m_highest_block.number()) {
					iter2 = iter->second.erase(iter2);
				} else {
					++iter2;
				}
			}
			if (iter->second.size() == 0) {
				iter = m_sign_cache.erase(iter);
			} else {
				++iter;
			}
		}
		m_last_collect_time = now_time;

		LOG(INFO) << "collectGarbage timecost(ms)=" << 1000 * t.elapsed();
	}
}


bool PBFT::getMinerList(int _blk_no, h512s &_miner_list) const {
	std::map<std::string, NodeConnParams> all_node;
	NodeConnManagerSingleton::GetInstance().getAllNodeConnInfo(_blk_no, all_node);

	unsigned miner_num = 0;
	for (auto iter = all_node.begin(); iter != all_node.end(); ++iter) {
		if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) {
			++miner_num;
		}
	}
	_miner_list.resize(miner_num);
	for (auto iter = all_node.begin(); iter != all_node.end(); ++iter) {
		if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) {
			auto idx = static_cast<unsigned>(iter->second._iIdx);
			if (idx >= miner_num) {
				LOG(ERROR) << "getMinerList return false cause for idx=" << idx << ",miner_num=" << miner_num;
				return false;
			}
			_miner_list[idx] = jsToPublic(toJS(iter->second._sNodeId));
		}
	}

	return true;

}

bool PBFT::checkBlockSign(BlockHeader const& _header, std::vector<std::pair<u256, Signature>> _sign_list) {
	Timer t;

	LOG(TRACE) << "PBFT::checkBlockSign " << _header.number();


	h512s miner_list;
	if (!getMinerList(static_cast<int>(_header.number() - 1), miner_list)) {
		LOG(ERROR) << "checkBlockSign failed for getMinerList return false, blk=" <<  _header.number() - 1;
		return false;
	}

	LOG(DEBUG) << "checkBlockSign call getAllNodeConnInfo: blk=" << _header.number() - 1 << ", miner_num=" << miner_list.size();

	// 检查公钥列表
	if (_header.nodeList() != miner_list) {
		ostringstream oss;
		for (size_t i = 0; i < miner_list.size(); ++i) {
			oss << miner_list[i] << ",";
		}
		LOG(ERROR) << "checkBlockSign failed, chain_block=" << _header.number() << ",miner_list size=" << miner_list.size() << ",value=" << oss.str();
		oss.clear();
		for (size_t i = 0; i < _header.nodeList().size(); ++i) {
			oss << _header.nodeList()[i] << ",";
		}
		LOG(ERROR) << "checkBlockSign failed, down_block=" << _header.number() << ",miner_list size=" << _header.nodeList().size() << ",value=" << oss.str();
		return false;
	}

	// 检查签名数量
	if (_sign_list.size() < (miner_list.size() - (miner_list.size() - 1) / 3)) {
		LOG(ERROR) << "checkBlockSign failed, blk=" << _header.number() << " not enough sign, sign_num=" << _sign_list.size() << ",miner_num" << miner_list.size();
		return false;
	}

	// 检查签名是否有效
	for (auto item : _sign_list) {
		if (item.first >= miner_list.size()) {
			LOG(ERROR) << "checkBlockSign failed, block=" << _header.number() << "sig idx=" << item.first << ", out of bound, miner_list size=" << miner_list.size();
			return false;
		}

		if (!dev::verify(miner_list[static_cast<int>(item.first)], item.second, _header.hash(WithoutSeal))) {
			LOG(ERROR) << "checkBlockSign failed, verify false, blk=" << _header.number() << ",hash=" << _header.hash(WithoutSeal);
			return false;
		}
	}

	LOG(INFO) << "checkBlockSign success, blk=" << _header.number() << ",hash=" << _header.hash(WithoutSeal) << ",timecost=" << t.elapsed() / 1000 << "ms";

	return true;
}
