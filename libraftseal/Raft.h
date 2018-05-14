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
 * @file: Raft.h
 * @author: fisco-dev
 * 
 * @date: 2017
 * A proof of work algorithm.
 */

#pragma once

#include <libdevcore/concurrent_queue.h>
#include <libdevcore/Worker.h>
#include <libdevcrypto/Common.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/SealEngine.h>
#include <libethereum/CommonNet.h>
#include "Common.h"
#include "RaftHost.h"

namespace dev
{

namespace eth
{

DEV_SIMPLE_EXCEPTION(RaftInitFailed);
DEV_SIMPLE_EXCEPTION(RaftUnexpectError);

class Raft: public SealEngineBase, Worker
{
public:
	Raft();
	virtual ~Raft() {stopWorking();}

	static void init();

	std::string name() const override { return "Raft"; }
	StringHashMap jsInfo(BlockHeader const& _bi) const override;

	strings sealers() const override { return {m_sealer}; }
	std::string sealer() const override { return m_sealer; }
	void setSealer(std::string const& _sealer) override { m_sealer = _sealer; }
	unsigned accountType() const { return m_account_type; }
	u256 nodeIdx() const { return m_node_idx; }

	void startGeneration() {setName("Raft"); m_last_elect_time = std::chrono::system_clock::now(); startWorking(); }
	void cancelGeneration() override { stopWorking(); }

	bool shouldSeal(Interface* _i) override;

	// should be called before start
	void initEnv(std::weak_ptr<RaftHost> _host, KeyPair const& _key_pair, unsigned _min_elect_time, unsigned _max_elect_time);

	// report newest block 上报最新块
	void reportBlock(BlockHeader const& _b);

	void onRaftMsg(unsigned _id, std::shared_ptr<p2p::Capability> _peer, RLP const& _r);

private:
	void workLoop() override;
	void resetConfig();

	void runAsLeader();
	void runAsFollower();
	void runAsCandidate();

	bool checkHeartBeatTimeout();
	bool checkElectTimeout();
	void resetElectTimeout();

	void switchToCandidate();
	void switchToFollower(u256 const& _leader);
	void switchToLeader();

	bool wonElection(u256 const& _votes) {return _votes >= m_node_num - m_f;}
	bool isMajorityVote(u256 const& _votes) {return _votes >= m_node_num - m_f;}

	// broadcast msg 广播消息
	void broadcastVoteReq();
	void broadcastHeartBeat();
	void brocastMsg(unsigned _id, bytes const& _data);

	// handle response 处理响应消息
	bool handleVoteRequest(u256 const& _from, h512 const& _node, RaftVoteReq const& _req);
	HandleVoteResult handleVoteResponse(u256 const & _from,  h512 const & _node, RaftVoteResp const & _req, VoteState &vote);
	bool handleHeartBeat(u256 const& _from, h512 const& _node, RaftHeartBeat const& _hb);
	bool sendResponse(u256 const& _to, h512 const& _node, unsigned _id, RaftMsg const& _resp);

	void setLeader(u256 const& _leader) {RecursiveGuard l(m_mutex); m_leader = _leader;}
	void setVote(u256 const& _candidate) {RecursiveGuard l(m_mutex); m_vote = _candidate;}
	unsigned state() {RecursiveGuard l(m_mutex); return m_state;}

private:
	void increaseElectTime() {
		if (m_max_elect_timeout + m_increase_time > m_max_elect_timeout_boundary) {
			m_max_elect_timeout = m_max_elect_timeout_boundary;
		}
		else {
			m_max_elect_timeout += m_increase_time;
			m_min_elect_timeout += m_increase_time;
		}
		LOG(INFO) << "increase elect time: m_min_elect_timeout=" << m_min_elect_timeout << ", m_max_elect_timeout" << m_max_elect_timeout;
	}

	void recoverElectTime() {
		m_max_elect_timeout = m_max_elect_timeout_init;
		m_min_elect_timeout = m_min_elect_timeout_init;
		LOG(INFO) << "reset elect time to init: m_min_elect_timeout=" << m_min_elect_timeout << ", m_max_elect_timeout" << m_max_elect_timeout;
	}

	void clearFirstVoteCache() {
		if (m_first_vote != Invalid256) {
			++m_heartbeat_count;
			if (m_heartbeat_count >= 2 * kHeartBeatIntervalRatio) {
				// clear m_first_vote
				m_heartbeat_count = 0;
				m_first_vote = Invalid256;
				LOG(INFO) << "broadcast or receive enough hb package and clear m_first_vote cache!";
			}
		}
	}

	mutable RecursiveMutex m_mutex;
	bool m_cfg_err = false;
	unsigned m_account_type;
	KeyPair m_key_pair;

	std::string m_sealer = "raft";
	BlockHeader m_highest_block;

	std::weak_ptr<RaftHost> m_host;

	u256 m_node_idx = Invalid256;
	u256 m_node_num = Invalid256;
	u256 m_f = 0;

	u256 m_term = u256(0);
	u256 m_last_leader_term = u256(0);
	RaftRole m_state = EN_STATE_FOLLOWER;
	u256 m_leader = Invalid256;
	u256 m_vote = Invalid256;
	u256 m_first_vote = Invalid256;

	std::chrono::system_clock::time_point m_last_hb_time;
	std::chrono::system_clock::time_point m_last_elect_time;
	unsigned m_elect_timeout;

	unsigned m_min_elect_timeout_init;
	unsigned m_max_elect_timeout_init;
	unsigned m_min_elect_timeout_boundary;
	unsigned m_max_elect_timeout_boundary;
	unsigned m_increase_time;

	unsigned m_min_elect_timeout;
	unsigned m_max_elect_timeout;

	struct BlockRef {
		u256 height;
		h256 block_hash;
		BlockRef(): height(0), block_hash(0) {}
		BlockRef(u256 _height, h256 _hash): height(_height), block_hash(_hash) {}
	};
	std::unordered_map<h512, BlockRef> m_member_block; // <node_id, BlockRef>

	// msg queue 消息队列
	RaftMsgQueue m_msg_queue;

	// not to const
	// static const unsigned kHeartBeatInterval = 1000;
	// static unsigned kHeartBeatInterval = 1000;
	unsigned m_heartbeat_interval = 1000;
	std::chrono::system_clock::time_point m_heartbeat_last_reset;
	unsigned m_heartbeat_timeout = 1000;
	unsigned m_heartbeat_count = 0;

	static const unsigned kHeartBeatIntervalRatio = 4;
};

}
}
