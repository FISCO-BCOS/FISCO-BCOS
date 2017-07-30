
#pragma once

#include <atomic>
#include <string>
#include <vector>
#include <set>
#include <map>

#include <libethereum/ChainParams.h>
#include <libethereum/Client.h>
#include <libdevcore/concurrent_queue.h>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>


#include "RaftSealEngine.h"

using namespace std;
using namespace dev;
using namespace eth;

namespace dev
{
namespace eth
{

enum raftState
{
	raftInitial,
	raftPrePrepare,
	raftWaitingVote,
	raftFinished
};

class Raft:public RaftSealEngine
{
public:
	virtual bool interpret(RaftPeer*, unsigned _id, RLP const& _r);

	virtual bool shouldSeal(Interface*);

	virtual void initEnv(class Client *_c, p2p::Host *_host, BlockChain* _bc) override;
	virtual void generateSeal(BlockHeader const& _bi, bytes const& _block) override;
	void generateSealBegin(bytes const& _block);

	unsigned accountType() const { return m_account_type; }

	//void generateSeal(BlockHeader const& _bi, bytes const& _block_data) override;
	void onSealGenerated(std::function<void(bytes const& _block, bool _isOurs)> const& _f)  { m_onSealGenerated = _f;}

	void reportBlock(BlockHeader const& _b);
	h512s getMinerNodeList();
	uint64_t lastConsensusTime() const { /*Guard l(m_mutex);*/ return m_last_consensus_time;};

	u256 nodeIdx() const { /*Guard l(m_mutex);*/ return u256(static_cast<uint64_t>(m_node_idx)); }
protected:
	virtual void tick();
	void reportBlockSelf();
	
	void resetConfig();

	void onBlockVoteAck(RaftPeer* _p, RLP const& _r);
	void onBlockVote(RaftPeer* _p, RLP const& _r);
	bytes authBytes();
	void reSet();
	void voteBlockEnd();
	void voteBlockBegin();
	bool msgVerify(const NodeID &_nodeID, bytes const&  _msg, h520 _msgSign);
	void addNodes(const std::string &_nodes);
	uint64_t nodeCount() const;
	void signalHandler(const boost::system::error_code& err, int signal);
	bool checkBlockSign(BlockHeader const& _header, std::vector<std::pair<u256, Signature>> _sign_list);
private:
	unsigned m_consensusTimeInterval = 20000;
	unsigned m_consensusRandTimeInterval = 0;

	std::function<void(bytes const& _block, bool _isOurs)> m_onSealGenerated;

	std::atomic<uint64_t> m_blockReport = {0};
	uint64_t m_blockNumber = 0;
	uint64_t m_blockNumberRecv = 0;
	unsigned m_account_type;

	NodeID m_votedId;
	h512s m_miner_list;
	
	bytes m_blockBytes;
	uint64_t m_currentView;
	uint64_t m_consensusTimeOut = 0;
	set<NodeID> m_idUnVoted;
	map<NodeID, Signature> m_idVoted;
	//unsigned m_consensusState = raftInitial;
	std::atomic<unsigned> m_consensusState = { raftInitial };
	std::atomic<bool> m_recvedBlock = { false };
	std::atomic<uint64_t> m_node_idx = {0};
	std::atomic<uint64_t> m_last_consensus_time = {0};

	concurrent_queue<bytes> m_blocks;

	std::atomic<pid_t> m_hostTid = {0};
	boost::asio::signal_set *m_sigset;
};


}
}

