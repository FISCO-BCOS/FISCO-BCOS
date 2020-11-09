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
 * @file: PBFT.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include <set>
#include <libdevcore/concurrent_queue.h>
#include <libdevcore/db.h>
#include <libdevcore/Worker.h>
#include <libdevcrypto/Common.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/SealEngine.h>
#include <libethereum/CommonNet.h>
#include "Common.h"
#include "PBFTHost.h"

namespace dev
{

namespace eth
{

DEV_SIMPLE_EXCEPTION(PbftInitFailed);
DEV_SIMPLE_EXCEPTION(UnexpectError);

#define PBFT_LOG(level) LOG(level) << "[PBFT]"

class PBFT: public SealEngineFace, Worker
{
public:
	PBFT();
	virtual ~PBFT();

	static void init();

	std::string name() const override { return "PBFT"; }
	StringHashMap jsInfo(BlockHeader const& _bi) const override;

	strings sealers() const override { return {m_sealer}; }
	std::string sealer() const override { return m_sealer; }
	void setSealer(std::string const& _sealer) override { m_sealer = _sealer; }
	u256 nodeIdx() const { /*Guard l(m_mutex);*/ return m_node_idx; }
	uint64_t lastConsensusTime() const { /*Guard l(m_mutex);*/ return m_last_consensus_time;}
	unsigned accountType() const { return m_account_type; }
	u256 view() const { return m_view; }
	u256 to_view() const {return m_to_view; }
	u256 quorum() const { return m_node_num - m_f; }
	const BlockHeader& getHighestBlock() const { return m_highest_block; } 
	bool isLeader() {
		auto ret = getLeader();
		bool isLeader = false;
		if (ret.first) // if errer, isLeader is False 出错的话isLeader就使用false
			isLeader = ret.second == m_node_idx;
		return isLeader;
	}

	void startGeneration() { setName("PBFT"); m_last_consensus_time = utcTime(); resetConfig(); startWorking(); }
	void cancelGeneration() override { stopWorking(); }

	void generateSeal(BlockHeader const& , bytes const& ) override {}
	bool generateSeal(BlockHeader const& _bi, bytes const& _block_data, u256 &_view);
	bool generateCommit(BlockHeader const& _bi, bytes const& _block_data, u256 const& _view);
	void onSealGenerated(std::function<void(bytes const&)> const&) override {}
	void onSealGenerated(std::function<void(bytes const&, bool)> const& _f)  { m_onSealGenerated = _f;}
	void onViewChange(std::function<void()> const& _f) { m_onViewChange = _f; }
	bool shouldSeal(Interface* _i) override;

	// should be called before start
	void initEnv(std::weak_ptr<PBFTHost> _host, BlockChain* _bc, OverlayDB* _db, BlockQueue *bq, KeyPair const& _key_pair, unsigned _view_timeout);
	void setOmitEmptyBlock(bool _flag) {m_omit_empty_block = _flag;}

	// report newest block 上报最新块
	void reportBlock(BlockHeader const& _b, u256 const& td);

	void onPBFTMsg(unsigned _id, std::shared_ptr<p2p::Capability> _peer, RLP const& _r);

	h512s getMinerNodeList() const {  /*Guard l(m_mutex);*/ return m_miner_list; }

	void changeViewForEmptyBlockWithoutLock(u256 const& _from);
	void changeViewForEmptyBlockWithLock();

	uint64_t lastExecFinishTime() const { return m_last_exec_finish_time; }
private:
	void initBackupDB();
	void resetConfig();
	// 线程：处理各种消息的响应，超时主动发送viewchange消息
	// thread: handle msg response, broadcast viewchange when timeout
	void workLoop() override;

	//检测是否超时，超时切换视图
	// check timeout, if timeout, change view
	void checkTimeout();

	void collectGarbage();


	bool getMinerList(int _blk_no, h512s & _miner_list) const;

	std::pair<bool, u256> getLeader() const;

	Signature signHash(h256 const& _hash) const;
	bool checkSign(u256 const& _idx, h256 const& _hash, Signature const& _sign) const;
	bool checkSign(PBFTMsg const& _req) const;

	// 广播消息
	// broadcast msg
	bool broadcastPrepareReq(BlockHeader const& _bi, bytes const& _block_data);
	bool broadcastSignReq(PrepareReq const& _req);
	bool broadcastCommitReq(PrepareReq const & _req);
	bool broadcastViewChangeReq();
	bool broadcastMsg(std::string const& _key, unsigned _id, bytes const& _data, bool fromSelf = true, std::unordered_set<h512> const& _filter = std::unordered_set<h512>());
	bool broadcastFilter(std::string const& _key, unsigned _id, shared_ptr<PBFTPeer> _p);
	void broadcastMark(std::string const& _key, unsigned _id, shared_ptr<PBFTPeer> _p);
	void clearMask();

	// 处理响应消息
	// handle msg
	void handleMsg(unsigned _id, u256 const& _from, h512 const& _node, RLP const& _r, std::weak_ptr<SessionFace> session);
	void handlePrepareMsg(u256 const& _from, PrepareReq const& _req, bool _self = false);
	void handleSignMsg(u256 const& _from, SignReq const& _req);
	void handleCommitMsg(u256 const& _from, CommitReq const& _req);
	void handleViewChangeMsg(u256 const& _from, ViewChangeReq const& _req, std::weak_ptr<SessionFace> session);

	void reHandlePrepareReq(PrepareReq const& _req);

	// cache访问（未加锁，外层要加锁保护）
	// access cache (no thread safe )
	bool addRawPrepare(PrepareReq const& _req);
	bool addPrepareReq(PrepareReq const& _req);
	void addSignReq(SignReq const& _req);
	void addCommitReq(CommitReq const& _req);
	void delCache(h256 const& _hash);
	void delViewChange();

	bool isExistPrepare(PrepareReq const& _req);
	bool isExistSign(SignReq const& _req);
	bool isExistCommit(CommitReq const& _req);
	bool isExistViewChange(ViewChangeReq const& _req);

	void checkAndChangeView();
	void checkAndCommit();
	void checkAndSave();

	void handleFutureBlock();
	void recvFutureBlock(u256 const& _from, PrepareReq const& _req);

	bool checkBlockSign(BlockHeader const& _header, std::vector<std::pair<u256, Signature>> _sign_list);

	void backupMsg(std::string const& _key, PBFTMsg const& _msg);
	void reloadMsg(std::string const& _key, PBFTMsg * _msg);

private:
	mutable Mutex m_mutex;

	unsigned m_account_type;
	KeyPair m_key_pair;
	std::string m_sealer = "pbft";

	std::function<void(bytes const& _block, bool _isOurs)> m_onSealGenerated;
	std::function<void()> m_onViewChange;

	std::weak_ptr<PBFTHost> m_host;
	BlockChain* m_bc;
	OverlayDB* m_stateDB;
	BlockQueue* m_bq;

	u256 m_node_idx = 0;
	u256 m_view = 0;
	u256 m_node_num = 0;
	u256 m_f = 0;

	unsigned m_view_timeout;
	uint64_t m_last_consensus_time;
	unsigned m_change_cycle = 0;
	u256 m_to_view = 0;
	bool m_leader_failed;
	uint64_t m_last_sign_time;

	PrepareReq m_raw_prepare_cache;
	PrepareReq m_prepare_cache;
	std::pair<u256, PrepareReq> m_future_prepare_cache;
	std::unordered_map<h256, std::unordered_map<std::string, SignReq>> m_sign_cache;
	std::unordered_map<h256, std::unordered_map<std::string, CommitReq>> m_commit_cache;
	std::unordered_map<u256, std::unordered_map<u256, ViewChangeReq>> m_recv_view_change_req;

	ldb::DB *m_backup_db;  // backup msg
	ldb::WriteOptions m_writeOptions;
	ldb::ReadOptions m_readOptions;
	PrepareReq m_committed_prepare_cache;

	std::chrono::system_clock::time_point m_last_collect_time;

	BlockHeader m_highest_block;
	u256 m_consensus_block_number; // the block which is waiting consensus 在等待共识的块

	h512s m_miner_list;

	bool m_cfg_err = false;

	uint64_t m_last_exec_finish_time;

	bool m_empty_block_flag;
	bool m_omit_empty_block;

	std::condition_variable m_signalled;
	Mutex x_signalled;

	// msg queue 消息队列
	PBFTMsgQueue m_msg_queue;

	static const unsigned kCollectInterval; // second
	static const size_t kKnownPrepare = 1024;
	static const size_t kKnownSign = 1024;
	static const size_t kKnownCommit = 1024;
	static const size_t kKnownViewChange = 1024;

	static const unsigned kMaxChangeCycle = 20;
	// log whether the commit is called before, use to trigger commit phase under consensus control
	std::unordered_map<h256, bool> m_commitMap;
};

}
}
