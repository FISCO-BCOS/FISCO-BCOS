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
/**
 * @file: Raft.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <cstdlib>
#include <ctime>
#include <libethcore/ChainOperationParams.h>
#include <libethcore/CommonJS.h>
#include <libethereum/Interface.h>
#include <libethereum/BlockChain.h>
#include <libethereum/EthereumHost.h>
#include <libethereum/NodeConnParamsManagerApi.h>
#include <libdevcrypto/Common.h>
#include "Raft.h"
#include <libdevcore/easylog.h>
using namespace std;
using namespace dev;
using namespace eth;


void Raft::init()
{
	ETH_REGISTER_SEAL_ENGINE(Raft);
}

Raft::Raft()
{
}

void Raft::initEnv(std::weak_ptr<RaftHost> _host, KeyPair const& _key_pair, unsigned _min_elect_time, unsigned _max_elect_time)
{
	RecursiveGuard l(m_mutex);

	m_host = _host;

	m_key_pair = _key_pair;

	resetConfig();

	m_min_elect_timeout = _min_elect_time;
	m_max_elect_timeout = _max_elect_time;

	m_min_elect_timeout_init = _min_elect_time;
	m_max_elect_timeout_init = _max_elect_time;

	m_min_elect_timeout_boundary = m_min_elect_timeout;
	m_max_elect_timeout_boundary = m_max_elect_timeout + (m_max_elect_timeout - m_min_elect_timeout) / 2;

	m_increase_time =  (m_max_elect_timeout - m_min_elect_timeout) / 4;

	m_last_elect_time = m_last_hb_time = std::chrono::system_clock::now();
	resetElectTimeout();

	std::srand(static_cast<unsigned>(utcTime()));

	// 一个超时周期 kHeartBeatIntervalRatio 次心跳
	// kHeartBeatIntervalRatio times heartbeat within one timeout period
	m_heartbeat_timeout = m_min_elect_timeout;
	m_heartbeat_interval = m_heartbeat_timeout / Raft::kHeartBeatIntervalRatio;

	LOG(INFO) << "initEnv success";
}

StringHashMap Raft::jsInfo(BlockHeader const& _bi) const
{
	return { { "number", toJS(_bi.number()) }, { "timestamp", toJS(_bi.timestamp()) } };
}

void Raft::resetConfig() {
	if (! NodeConnManagerSingleton::GetInstance().getAccountType(m_key_pair.pub(), m_account_type)) {
		LOG(WARNING) << "resetConfig: can't find myself id, stop sealing";
		m_cfg_err = true;
		return;
	}

	auto node_num =   NodeConnManagerSingleton::GetInstance().getMinerNum();
	if (node_num == 0) {
		LOG(WARNING) << "resetConfig: miner_num = 0, stop sealing";
		m_cfg_err = true;
		return;
	}

	u256 node_idx = 0;
	if (! NodeConnManagerSingleton::GetInstance().getIdx(m_key_pair.pub(), node_idx)) {
		LOG(WARNING) << "resetConfig: can't find myself id, stop sealing";
		m_cfg_err = true;
		return;
	}

	if (node_num != m_node_num || node_idx != m_node_idx) {
		m_node_num = node_num;
		m_node_idx = node_idx;
		m_f = (m_node_num - 1) / 2;

		switchToFollower(Invalid256);
		resetElectTimeout();
	}

	m_cfg_err = false;
}

bool Raft::shouldSeal(Interface*)
{
	RecursiveGuard l(m_mutex);
	if (m_cfg_err || m_account_type != EN_ACCOUNT_TYPE_MINER || m_state != EN_STATE_LEADER) {
		return false;
	}

	u256 count = 1;
	u256 curr_height = m_highest_block.number();
	for (auto iter = m_member_block.begin(); iter != m_member_block.end(); ++iter) {
		if (iter->second.height > curr_height) {
			LOG(INFO) << "Wait to download block, shouldSeal return false";
			return false;
		}
		if (iter->second.height == curr_height) {
			++count;
		}
	}

	if (count < m_node_num - m_f) {
		LOG(INFO) << "Wait somebody to sync block, shouldSeal return false, count=" << count << ",m_node_num=" << m_node_num << ",m_f=" << m_f << ", m_member_block.size=" << m_member_block.size();
		return false;
	}

	return true;
}

void Raft::reportBlock(BlockHeader const & _b) {
	RecursiveGuard l(m_mutex);
	m_highest_block = _b;

	resetConfig();

	LOG(INFO) << "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ Report: blk=" << m_highest_block.number() << ",idx=" << m_highest_block.genIndex() << ",hash=" << _b.hash(WithoutSeal);
}

void Raft::onRaftMsg(unsigned _id, std::shared_ptr<p2p::Capability> _peer, RLP const & _r) {
	if (_id <= RaftHeartBeatRespPacket) {
		NodeID nodeid;
		auto session = _peer->session();
		if (session && (nodeid = session->id()))
		{
			u256 idx = u256(0);
			if (!NodeConnManagerSingleton::GetInstance().getIdx(nodeid, idx)) {
				LOG(WARNING) << "Recv an raft msg from unknown peer id=" << _id;
				return;
			}
			LOG(INFO) << "onRaftMsg: id=" << _id << ",from=" << idx;
			m_msg_queue.push(RaftMsgPacket(idx, nodeid, _id, _r[0].data()));
		}
		else
		{
			LOG(WARNING) << "onRaftMsg: session id error!";
		}

	} else {
		LOG(WARNING) << "Recv an illegal msg, id=" << _id;
	}
}

void Raft::workLoop() {
	while (isWorking())
	{
		if (m_cfg_err || m_account_type != EN_ACCOUNT_TYPE_MINER) {
			this_thread::sleep_for(std::chrono::milliseconds(1000));
			continue;
		}
		switch (state()) {
		case EN_STATE_LEADER: {
			runAsLeader();
			break;
		}
		case EN_STATE_FOLLOWER: {
			runAsFollower();
			break;
		}
		case EN_STATE_CANDIDATE: {
			runAsCandidate();
			break;
		}
		default: {
			LOG(WARNING) << "Error state=" << m_state;
			break;
		}
		}
	}
}

void Raft::runAsLeader() {
	m_first_vote = Invalid256;
	m_last_leader_term = m_term;
	m_heartbeat_last_reset = m_last_hb_time = std::chrono::system_clock::now();
	std::unordered_map<h512, unsigned> member_hb_log;
	while (true) {
		if (m_state != EN_STATE_LEADER) {
			break;
		}

		// heartbeat timeout, change role to candidate (心跳超时转 candidate)
		// m_node_num > 1 is for single node 
		if (m_node_num > 1 && checkHeartBeatTimeout()) {
			LOG(INFO) << "HeartBeat Timeout! current node_num:" << m_node_num;
			for (auto& i : member_hb_log) {
				LOG(INFO) << "======= node:" << i.first.hex().substr(0, 5) << " , hb log:" << i.second;
			}
			switchToCandidate();
			return;
		}

		broadcastHeartBeat();

		std::pair<bool, RaftMsgPacket> ret = m_msg_queue.tryPop(5);
		if (!ret.first) {
			continue;
		} else {
			switch (ret.second.packet_id) {
			case RaftVoteReqPacket: {
				RaftVoteReq req;
				req.populate(RLP(ret.second.data));
				if (handleVoteRequest(ret.second.node_idx, ret.second.node_id, req)) {
					switchToFollower(Invalid256);
					return;
				}
				break;
			}
			case RaftVoteRespPacket: {
				// do nothing
				break;
			}
			case RaftHeartBeatPacket: {
				RaftHeartBeat hb;
				hb.populate(RLP(ret.second.data));
				if (handleHeartBeat(ret.second.node_idx, ret.second.node_id, hb)) {
					switchToFollower(hb.leader);
					return;
				}
				break;
			}
			case RaftHeartBeatRespPacket: {
				RaftHeartBeatResp resp;
				LOG(INFO) << "receive HeaderBeat ack from=" << ret.second.node_idx << ", node=" << ret.second.node_id.hex().substr(0, 5);
				resp.populate(RLP(ret.second.data));
				{
					RecursiveGuard l(m_mutex);
					m_member_block[ret.second.node_id] = BlockRef(resp.height, resp.block_hash);
				}

				// receive large term
				if (resp.term > m_term)
					break;

				auto it = member_hb_log.find(ret.second.node_id);
				if (it == member_hb_log.end()) {
					member_hb_log.insert(make_pair(ret.second.node_id, 1));
				} else {
					it->second++;
				}
				int count = count_if(member_hb_log.begin(), member_hb_log.end(), [](pair<const h512, unsigned>& item) {
					if (item.second > 0) return true; else return false;
				});
				// add myself 加上自己
				if (count + 1 >= m_node_num - m_f) { // collect heartbeat exceed half 收集到了超过半数的心跳
					// reset heartbeat timeout 重置心跳超时时间
					m_heartbeat_last_reset = std::chrono::system_clock::now();
					for_each(member_hb_log.begin(), member_hb_log.end(), [](pair<const h512, unsigned>& item) {
						if (item.second > 0) --item.second;
					});
					LOG(INFO) << "Reset HeaderBeat timeout";
				}
				break;
			}
			default: {
				break;
			}
			}
		}
	}
}

void Raft::runAsFollower() {
	while (true) {
		if (m_state != EN_STATE_FOLLOWER) {
			break;
		}

		if (checkElectTimeout()) {
			switchToCandidate();
			return;
		}

		std::pair<bool, RaftMsgPacket> ret = m_msg_queue.tryPop(5);
		if (!ret.first) {
			continue;
		} else {
			switch (ret.second.packet_id) {
			case RaftVoteReqPacket: {
				RaftVoteReq req;
				req.populate(RLP(ret.second.data));
				if (handleVoteRequest(ret.second.node_idx, ret.second.node_id, req)) {
					return;
				}
				break;
			}
			case RaftVoteRespPacket: {
				// do nothing
				break;
			}
			case RaftHeartBeatPacket: {
				RaftHeartBeat hb;
				hb.populate(RLP(ret.second.data));
				if (m_leader == Invalid256) {
					setLeader(hb.leader);
				}
				if (handleHeartBeat(ret.second.node_idx, ret.second.node_id, hb)) {
					setLeader(hb.leader);
				}
				break;
			}
			default: {
				break;
			}
			}
		}
	}
}


void Raft::runAsCandidate() {
	if (m_state != EN_STATE_CANDIDATE) {
		return;
	}

	broadcastVoteReq();
	VoteState voteState;
	voteState.vote += 1;  // vote self
	setVote(m_node_idx);
	m_first_vote = m_node_idx;

	if (wonElection(voteState.vote)) {
		switchToLeader();
		return;
	}

	while (true) {
		if (checkElectTimeout()) {
			LOG(INFO) << "========== VoteState: vote=" << voteState.vote << ", unVote=" << voteState.unVote
			          << ", lastTermErr=" << voteState.lastTermErr << ", firstVote=" << voteState.firstVote
			          << ", discardedVote=" << voteState.discardedVote;
			if (isMajorityVote(voteState.totalVoteCount())) {
				LOG(INFO) << "candidate campaign leader time out!";
				switchToCandidate();
			} else {
				// not receive enough vote!
				LOG(INFO) << "not receive enough vote! recover m_term from" << m_term << "to" << m_term - 1 << "switch to Follower!";
				increaseElectTime();
				m_term--; // recover to previous term
				switchToFollower(Invalid256);
			}
			return;
		}
		std::pair<bool, RaftMsgPacket> ret = m_msg_queue.tryPop(5);
		if (!ret.first) {
			continue;
		} else {
			switch (ret.second.packet_id) {
			case RaftVoteReqPacket: {
				RaftVoteReq req;
				req.populate(RLP(ret.second.data));
				if (handleVoteRequest(ret.second.node_idx, ret.second.node_id, req)) {
					switchToFollower(Invalid256);
					return;
				}
				break;
			}
			case RaftVoteRespPacket: {
				RaftVoteResp resp;
				resp.populate(RLP(ret.second.data));
				LOG(INFO) << "Recv response, term=" << resp.term << ",vote_flag=" << resp.vote_flag << "from=" << ret.second.node_idx << ",node=" << ret.second.node_id.hex().substr(0, 5);
				HandleVoteResult handle_ret = handleVoteResponse(ret.second.node_idx, ret.second.node_id, resp, voteState);
				if (handle_ret == TO_LEADER) {
					switchToLeader();
					return;
				}
				else if (handle_ret == TO_FOLLOWER) {
					switchToFollower(Invalid256);
					return;
				}
				break;
			}
			case RaftHeartBeatPacket: {
				RaftHeartBeat hb;
				hb.populate(RLP(ret.second.data));
				if (handleHeartBeat(ret.second.node_idx, ret.second.node_id, hb)) {
					switchToFollower(hb.leader);
					return;
				}
				break;
			}
			default: {
				break;
			}
			}
		}
	}
}

bool Raft::checkHeartBeatTimeout() {
	std::chrono::system_clock::time_point now_time = std::chrono::system_clock::now();
	return now_time - m_heartbeat_last_reset >= std::chrono::milliseconds(m_heartbeat_timeout);
}

bool Raft::checkElectTimeout() {
	std::chrono::system_clock::time_point now_time = std::chrono::system_clock::now();
	return now_time - m_last_elect_time >= std::chrono::milliseconds(m_elect_timeout);
}

void Raft::resetElectTimeout() {
	LOG(INFO) << "resetElectTimeout.";
	m_elect_timeout = m_min_elect_timeout + std::rand() % 100 * (m_max_elect_timeout - m_min_elect_timeout) / 100;
	m_last_elect_time = std::chrono::system_clock::now();
}

void Raft::switchToFollower(u256 const & _leader) {
	RecursiveGuard l(m_mutex);
	m_leader = _leader;
	m_state = EN_STATE_FOLLOWER;
	m_heartbeat_count = 0;
	LOG(INFO) << "switchToFollower............... current term=" << m_term;
	resetElectTimeout();
}

void Raft::switchToCandidate() {
	RecursiveGuard l(m_mutex);
	m_term++;
	m_leader = Invalid256;
	m_state = EN_STATE_CANDIDATE;
	LOG(INFO) << "switchToCandidate............... to new term=" << m_term;
	resetElectTimeout();
}

void Raft::switchToLeader() {
	RecursiveGuard l(m_mutex);
	m_leader = m_node_idx;
	m_state = EN_STATE_LEADER;
	recoverElectTime();
	LOG(INFO) << "switchToLeader............... current term=" << m_term;
}

void Raft::broadcastHeartBeat() {
	std::chrono::system_clock::time_point now_time = std::chrono::system_clock::now();
	if (now_time - m_last_hb_time >= std::chrono::milliseconds(m_heartbeat_interval)) {
		m_last_hb_time = now_time;

		RaftHeartBeat req;
		req.idx = m_node_idx;
		req.term = m_term;
		req.height = m_highest_block.number();
		req.block_hash = m_highest_block.hash(WithoutSeal);
		req.leader = m_node_idx;

		LOG(INFO) << "Ready to broadcastHeartBeat, term=" << req.term << ",leader=" << req.leader;

		RLPStream ts;
		req.streamRLPFields(ts);
		brocastMsg(RaftHeartBeatPacket, ts.out());

		clearFirstVoteCache();
	}
}

void Raft::broadcastVoteReq() {
	RaftVoteReq req;
	req.idx = m_node_idx;
	req.term = m_term;
	req.height = m_highest_block.number();
	req.block_hash = m_highest_block.hash(WithoutSeal);
	req.candidate = m_node_idx;
	req.last_leader_term = m_last_leader_term;

	LOG(INFO) << "Ready to broadcastVoteReq, term=" << req.term << ",last_leader_term=" << m_last_leader_term << ",vote=" << req.candidate;

	RLPStream ts;
	req.streamRLPFields(ts);
	brocastMsg(RaftVoteReqPacket, ts.out());
}

void Raft::brocastMsg(unsigned _id, bytes const & _data) {
	if (auto h = m_host.lock()) {
		h->foreachPeer([&](shared_ptr<RaftPeer> _p)
		{
			NodeID nodeid;
			auto session = _p->session();
			if (session && (nodeid = session->id()))
			{
				unsigned account_type = 0;
				if ( !NodeConnManagerSingleton::GetInstance().getAccountType(nodeid, account_type) || account_type != EN_ACCOUNT_TYPE_MINER ) {
					return true;
				}

				RLPStream ts;
				_p->prep(ts, _id, 1).append(_data);
				_p->sealAndSend(ts);
				LOG(INFO) << "Sent [" << _id << "] raftmsg to " << nodeid;
				
			}
			else
			{
				LOG(WARNING) << "brocastMsg: session id error!";
			}

			return true;
		});
	}
}

bool Raft::sendResponse(u256 const & _to, h512 const & _node, unsigned _id, RaftMsg const & _resp) {
	LOG(INFO) << "Ready to sendResponse, to=" << _to << ",term=" << _resp.term;
	RLPStream resp_ts;
	_resp.streamRLPFields(resp_ts);

	if (auto h = m_host.lock()) {
		h->foreachPeer([&](shared_ptr<RaftPeer> _p)
		{
			NodeID nodeid;
			auto session = _p->session();
			if (session && (nodeid = session->id()))
			{
				if ( nodeid != _node) {
					return true;
				}

				RLPStream ts;
				_p->prep(ts, _id, 1).append(resp_ts.out());
				_p->sealAndSend(ts);
				LOG(INFO) << "Sent [" << _id << "] raftmsg to " <<nodeid;
			}
			else
			{
				LOG(WARNING) << "sendResponse: session id error!";
			}

			return true;
		});
		return true;
	}
	return false;
}

bool Raft::handleVoteRequest(u256 const & _from,  h512 const & _node, RaftVoteReq const & _req) {
	LOG(INFO) << "handleVoteRequest: from=" << _from << ",node=" << _node.hex().substr(0, 5) << ",term=" << _req.term << ",candidate=" << _req.candidate;

	RaftVoteResp resp;
	resp.idx = m_node_idx;
	resp.term = m_term;
	resp.height = m_highest_block.number();
	resp.block_hash = m_highest_block.hash(WithoutSeal);

	resp.vote_flag = VOTE_RESP_REJECT;
	resp.last_leader_term = m_last_leader_term;

	if (_req.term <= m_term) {
		if (m_state == EN_STATE_LEADER) {
			// include _req.term < m_term and _req.term == m_term
			resp.vote_flag = VOTE_RESP_LEADER_REJECT;
			LOG(INFO) << "discart vreq for i'm the bigger leader, m_term=" << m_term << ",receive=" << _req.term;
		} else if (_req.term == m_term) {
			// _req.term == m_term for follower and candidate
			resp.vote_flag = VOTE_RESP_DISCARD;
			LOG(INFO) << "discart vreq for i'm already in this term, m_term=" << m_term << ",m_vote=" << m_vote;
		} else {
			// _req.term < m_term for follower and candidate
			resp.vote_flag = VOTE_RESP_REJECT;
			LOG(INFO) << "discart vreq for smaller term, m_term=" << m_term;
		}
		sendResponse(_from, _node, RaftVoteRespPacket, resp);
		return false;
	}

	// handle lastLeaderTerm error
	if (_req.last_leader_term < m_last_leader_term) {
		resp.vote_flag = VOTE_RESP_LASTTERM_ERROR;
		LOG(INFO) << "discart vreq for smaller last leader term, m_last_leader_term=" << m_last_leader_term << ", receive last_leader_term=" << _req.last_leader_term;
		sendResponse(_from, _node, RaftVoteRespPacket, resp);
		return false;
	}

	// first vote, not change term
	if (m_first_vote == Invalid256) {
		m_first_vote = _req.candidate;
		resp.vote_flag = VOTE_RESP_FIRST_VOTE;
		LOG(INFO) << "discart vreq for i'm the first time to vote, m_term=" << m_term << ", req_vote_term=" << _req.term << ",m_first_vote=" << m_first_vote;
		sendResponse(_from, _node, RaftVoteRespPacket, resp);
		return false;
	}

	m_term = _req.term;
	m_leader = Invalid256;
	m_vote = Invalid256;

	m_first_vote = _req.candidate;
	setVote(_req.candidate);

	resp.term = m_term;
	resp.vote_flag = VOTE_RESP_GRANTED;
	sendResponse(_from, _node, RaftVoteRespPacket, resp);

	resetElectTimeout();

	return true;
}

HandleVoteResult Raft::handleVoteResponse(u256 const & _from,  h512 const & _node, RaftVoteResp const & _req, VoteState& _vote_state) {
	unsigned vote_flag = (unsigned)_req.vote_flag;
	switch (vote_flag)
	{
	case VOTE_RESP_REJECT:
		_vote_state.unVote++;
		if (isMajorityVote(_vote_state.unVote)) {
			// increase elect time
			increaseElectTime();
			return TO_FOLLOWER;
		}
		break;
	case VOTE_RESP_LEADER_REJECT:
		// switch to leader directly
		m_term = _req.term;
		m_last_leader_term = _req.last_leader_term;
		// increase elect time
		increaseElectTime();
		return TO_FOLLOWER;
	case VOTE_RESP_LASTTERM_ERROR:
		_vote_state.lastTermErr++;
		if (isMajorityVote(_vote_state.lastTermErr)) {
			// increase elect time
			increaseElectTime();
			return TO_FOLLOWER;
		}
		break;
	case VOTE_RESP_FIRST_VOTE:
		_vote_state.firstVote++;
		if (isMajorityVote(_vote_state.firstVote)) {
			LOG(INFO) << "receive majority first vote, recover from" << m_term << "to m_term=" << m_term - 1 << "and switch to folloer";
			m_term--;
			return TO_FOLLOWER;
		}
		break;
	case VOTE_RESP_DISCARD:
		_vote_state.discardedVote++;
		// do nothing
		break;
	case VOTE_RESP_GRANTED:
		_vote_state.vote++;
		if (isMajorityVote(_vote_state.vote))
			return TO_LEADER;
		break;
	default:
		LOG(INFO) << "Error vote_flag=" << vote_flag << " from=" << _from << " node=" << _node.hex().substr(0, 5);
	}
	return NONE;
}

bool Raft::handleHeartBeat(u256 const & _from, h512 const & _node, RaftHeartBeat const & _hb) {
	LOG(INFO) << "handleHeartBeat: from=" << _from << ",node=" << _node.hex().substr(0, 5) << ",term=" << _hb.term << ",leader=" << _hb.leader;

	RaftHeartBeatResp resp;
	resp.idx = m_node_idx;
	resp.term = m_term;
	resp.height = m_highest_block.number();
	resp.block_hash = m_highest_block.hash(WithoutSeal);
	sendResponse(_from, _node, RaftHeartBeatRespPacket, resp);

	if (_hb.term < m_term && _hb.term <= m_last_leader_term) {
		LOG(INFO) << "discart hb for smaller term, m_term=" << m_term << ", receive term=" << _hb.term
		          << ", m_last_leader_term=" << m_last_leader_term << ", receive last leader term=" << _hb.term;
		return false;
	}

	bool step_down = false;

	// _hb.term >= m_term || _hb.last_leader_term > m_last_leader_term
	// receive larger last_leader_term, recover my term to hb term, set self to next step (follower)
	if (_hb.term > m_last_leader_term) {
		LOG(INFO) << "prepare to switch follower due to last leader term error: m_last_leader_term=" << m_last_leader_term << ", hb last leader=" << _hb.term;
		m_term = _hb.term;
		m_vote = Invalid256;
		step_down = true;
	}

	if (_hb.term > m_term) {
		LOG(INFO) << "prepare to switch follower due to receive higher term: m_term" << m_term << ", hb term=" << _hb.term;
		m_term = _hb.term;
		m_vote = Invalid256;
		step_down = true;
	}

	if (m_state == EN_STATE_CANDIDATE && _hb.term >= m_term) {
		LOG(INFO) << "prepare to switch follower due to receive higher or equal term in candidate: m_term" << m_term << ", hb term=" << _hb.term;
		m_term = _hb.term;
		m_vote = Invalid256;
		step_down = true;
	}

	clearFirstVoteCache();
	// see the leader last time
	m_last_leader_term = _hb.term;

	resetElectTimeout();

	return step_down;
}
