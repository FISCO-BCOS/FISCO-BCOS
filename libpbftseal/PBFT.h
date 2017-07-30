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


#pragma once

#include <set>
#include <libdevcore/concurrent_queue.h>
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

class PBFT: public SealEngineFace, Worker
{
public:
	PBFT();

	static void init();

	std::string name() const override { return "PBFT"; }
	StringHashMap jsInfo(BlockHeader const& _bi) const override;

	strings sealers() const override { return {m_sealer}; }
	std::string sealer() const override { return m_sealer; }
	void setSealer(std::string const& _sealer) override { m_sealer = _sealer; }
	u256 nodeIdx() const { /*Guard l(m_mutex);*/ return m_node_idx; }
	uint64_t lastConsensusTime() const { /*Guard l(m_mutex);*/ return m_last_consensus_time;}
	unsigned accountType() const { return m_account_type; }

	void startGeneration() { setName("PBFT"); m_last_consensus_time = utcTime(); resetConfig(); startWorking(); }
	void cancelGeneration() override { stopWorking(); }

	void generateSeal(BlockHeader const& _bi, bytes const& _block_data) override;
	void onSealGenerated(std::function<void(bytes const&)> const&) override {}
	void onSealGenerated(std::function<void(bytes const&, bool)> const& _f)  { m_onSealGenerated = _f;}
	void onViewChange(std::function<void()> const& _f) { m_onViewChange = _f; }
	bool shouldSeal(Interface* _i) override;

	// should be called before start
	void initEnv(std::weak_ptr<PBFTHost> _host, BlockChain* _bc, OverlayDB* _db, BlockQueue *bq, KeyPair const& _key_pair, unsigned _view_timeout);

	// 上报最新块
	void reportBlock(BlockHeader const& _b, u256 const& td);

	void onPBFTMsg(unsigned _id, std::shared_ptr<p2p::Capability> _peer, RLP const& _r);

	h512s getMinerNodeList() const {  /*Guard l(m_mutex);*/ return m_miner_list; }
	uint64_t lastExecFinishTime() const { return m_last_exec_finish_time; }
private:
	void resetConfig();
	// 线程：处理各种消息的响应，超时主动发送viewchange消息
	void workLoop() override;

	//检测是否超时，超时切换视图
	void checkTimeout();

	void collectGarbage();


	bool getMinerList(int _blk_no, h512s & _miner_list) const;

	std::pair<bool, u256> getLeader() const;

	Signature signBlock(h256 const& _hash);
	bool checkSign(u256 const& _idx, h256 const& _hash, Signature const& _sign);

	// 广播消息
	bool broadcastPrepareReq(BlockHeader const& _bi, bytes const& _block_data);
	bool broadcastSignReq(PrepareReq const& _req);
	bool broadcastViewChangeReq();
	bool broadcastMsg(std::string const& _key, unsigned _id, bytes const& _data, std::unordered_set<h512> const& _filter = std::unordered_set<h512>());
	bool broadcastFilter(std::string const& _key, unsigned _id, shared_ptr<PBFTPeer> _p);
	void broadcastMark(std::string const& _key, unsigned _id, shared_ptr<PBFTPeer> _p);

	// 处理响应消息
	void handleMsg(unsigned _id, u256 const& _from, h512 const& _node, RLP const& _r);
	void handlePrepareMsg(u256 const& _from, PrepareReq const& _req);
	void handleSignMsg(u256 const& _from, SignReq const& _req);
	void handleViewChangeMsg(u256 const& _from, ViewChangeReq const& _req);

	// cache访问（未加锁，外层要加锁保护）
	bool addPrepareReq(PrepareReq const& _req);
	void addSignReq(SignReq const& _req);
	void delCache(h256 const& _block_hash);

	bool isExistPrepare(PrepareReq const& _req);
	bool isExistSign(SignReq const& _req);
	bool isExistViewChange(ViewChangeReq const& _req);

	void checkAndChangeView();
	void checkAndCommit();

	void handleFutureBlock();
	void recvFutureBlock(u256 const& _from, PrepareReq const& _req);

	bool checkBlockSign(BlockHeader const& _header, std::vector<std::pair<u256, Signature>> _sign_list);

private:
	mutable Mutex m_mutex;

	unsigned m_account_type;
	KeyPair m_key_pair;
	std::string m_sealer = "pbft";

	std::function<void(bytes const& _block, bool _isOurs)> m_onSealGenerated;
	std::function<void()> m_onViewChange;

	std::weak_ptr<PBFTHost> m_host;
	std::shared_ptr<BlockChain> m_bc;
	std::shared_ptr<OverlayDB> m_stateDB;
	std::shared_ptr<BlockQueue> m_bq;

	u256 m_node_idx = 0;
	u256 m_view = 0;
	u256 m_node_num = 0;
	u256 m_f = 0;

	unsigned m_view_timeout;
	uint64_t m_last_consensus_time;
	unsigned m_change_cycle;
	u256 m_to_view;
	bool m_leader_failed;
	uint64_t m_last_sign_time;

	//std::unordered_map<h256, PrepareReq> m_raw_prepare_cache;
	PrepareReq m_raw_prepare_cache;
	//std::unordered_map<h256, PrepareReq> m_prepare_cache;
	PrepareReq m_prepare_cache;
	std::pair<u256, PrepareReq> m_future_prepare_cache;
	std::unordered_map<h256, std::unordered_map<std::string, SignReq>> m_sign_cache;
	std::unordered_map<u256, std::unordered_map<u256, ViewChangeReq>> m_recv_view_change_req;

	std::chrono::system_clock::time_point m_last_collect_time;

	BlockHeader m_highest_block;
	u256 m_consensus_block_number; // 在等待共识的块
	//BlockHeader m_commited_block; // 已经共识提交的块

	h512s m_miner_list;

	bool m_cfg_err = false;

	uint64_t m_last_exec_finish_time;
	std::condition_variable m_signalled;
	Mutex x_signalled;

	static const size_t kKnownPrepare = 1024;
	static const size_t kKnownSign = 1024;
	static const size_t kKnownViewChange = 1024;
};

}
}
