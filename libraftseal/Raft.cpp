
#include <boost/algorithm/string.hpp>


#include "Common.h"
#include "Raft.h"

#include <sys/syscall.h>  

#define gettidv1() syscall(__NR_gettid)  
#define gettidv2() syscall(SYS_gettid) 

#define RAFT_SIGNAL SIGUSR1
#define RAFT_SIGNAL_REPORT SIGUSR2

using namespace dev;
using namespace dev::eth;

using namespace boost::asio;
using namespace ba::ip;

void Raft::tick()
{
	if(m_account_type != EN_ACCOUNT_TYPE_MINER)
		return;

	uint64_t now = utcTime();
	if(raftInitial == m_consensusState){
		bool have;
		bytes blockBytes;
		tie(have, blockBytes) = m_blocks.tryPop(0);

		if(have && !blockBytes.empty())
			generateSealBegin(blockBytes);
	}else if(raftPrePrepare == m_consensusState){
		if(now > m_consensusTimeOut){
			LOG(TRACE) << "raftPrePrepare: m_account_type=" << m_account_type << ",now=" << now;
			voteBlockBegin();
		}
	}else if(raftWaitingVote == m_consensusState){
		LOG(TRACE) << "raftWaitingVote: m_account_type=" << m_account_type << ",now=" << now << ",m_consensusTimeOut=" << m_consensusTimeOut;
		voteBlockEnd();
	}else if(raftFinished == m_consensusState){
		if(now > m_consensusTimeOut){
			LOG(TRACE) << "raftInitial: m_account_type=" << m_account_type << ",now=" << now;
			m_consensusState = raftInitial;
		}
	}
}

bool Raft::interpret(RaftPeer* _p, unsigned _id, RLP const& _r)
{
	if(m_account_type != EN_ACCOUNT_TYPE_MINER){
		LOG(TRACE) << "_id= " << _id << ",m_account_type=" << m_account_type << ",_p->id()=" << _p->id();
		return true;
	}

	unsigned msgType;
	try{
		msgType = _r[0][0].toInt();
	}catch(...){
		LOG(TRACE) << "msgType err _id= " << _id << ",m_account_type=" << m_account_type << ",_p->id()=" << _p->id();
		return true;
	}

	bool contain = (find(m_miner_list.begin(), m_miner_list.end(), _p->id()) != m_miner_list.end());
	if(!contain){
		LOG(TRACE) << "interpret _p->id()=" << _p->id() << ",_id=" << _id << ",msgType=" << msgType;
		return true;
	}

	LOG(TRACE) << "interpret _p->id()=" << _p->id() << ",_id=" << _id << ",msgType=" << msgType;
	try{
		switch(msgType){
			case raftBlockVote:
				onBlockVote(_p, _r);
				break;
			case raftBlockVoteEcho:
				onBlockVoteAck(_p, _r);
				break;
			default:
				break;
		}
	}catch(...){
		LOG(ERROR) << "catchErrinterpret _p->id()=" << _p->id() << ",_id=" << _id << ",msgType=" << msgType;
	}

	return true;
}

void Raft::reportBlock(BlockHeader const& _b)
{
	unsigned long long blockNumber = _b.number().convert_to<unsigned long long>();
	m_blockReport = blockNumber;
	kill(m_hostTid, RAFT_SIGNAL_REPORT);

	LOG(TRACE) << "blockNumber=" << blockNumber << ",m_blockNumber=" << m_blockNumber;
}

void Raft::reportBlockSelf()
{
	unsigned long long blockNumber = m_blockReport;

	LOG(TRACE) << "blockNumber=" << blockNumber << ",m_blockNumber=" << m_blockNumber;

	if(m_blockNumber >= blockNumber)
		return;
	
	m_blockNumber = blockNumber;
	m_blockNumberRecv = blockNumber;

	resetConfig();
	reSet();
	m_currentView = 0;
}

void Raft::resetConfig()
{
	if(!getMinerList(m_miner_list)){
		LOG(ERROR) << "resetConfig: getMinerList return false";
	}

	auto ourIt = find(m_miner_list.begin(), m_miner_list.end(), id());
	bool contain = (ourIt != m_miner_list.end());
	m_account_type = contain ? EN_ACCOUNT_TYPE_MINER : EN_ACCOUNT_TYPE_NORMAL;
	if(contain){
		m_node_idx = ourIt - m_miner_list.begin();
	}else{
		m_node_idx = 0;
	}

	LOG(TRACE) << "m_account_type=" << m_account_type << ",id()=" << id() << ",m_node_idx=" << (unsigned)m_node_idx;
}

void Raft::signalHandler(const boost::system::error_code& err, int signal)
{
	LOG(TRACE) << "_host.onTick err = " << err << ",signal = " << signal << ",m_hostTid=" << m_hostTid;

	if(RAFT_SIGNAL == signal){
		tick();
	}else if(RAFT_SIGNAL_REPORT == signal){
		reportBlockSelf();
	}
	m_sigset->async_wait(boost::bind(&Raft::signalHandler, this, _1, _2));
}

void Raft::initEnv(class Client *_c, p2p::Host *_host, BlockChain* _bc)
{
	srand(utcTime());
	RaftSealEngine::initEnv(_c, _host, _bc);	

	resetConfig();

	_host->onTick(1000, [=]() {
		this->tick();
	});

	_host->onInit([=]() {
		m_hostTid = gettidv1();
		LOG(TRACE) << "_host.onTick**************************************************************************************** m_hostTid =" << m_hostTid;
	});

	m_sigset = new signal_set(_host->ioService(), RAFT_SIGNAL, RAFT_SIGNAL_REPORT);
	m_sigset->async_wait(boost::bind(&Raft::signalHandler, this, _1, _2));
}

uint64_t Raft::nodeCount() const
{
	return m_miner_list.size();
}

bool Raft::msgVerify(const NodeID &_nodeID, bytes const&  _msg, h520 _msgSign)
{
	BlockHeader header(_msg);
	//h256 hash =  sha3(_msg);
	h256 hash =  header.hash(WithoutSeal);

	LOG(TRACE) << "_nodeID=" << _nodeID << ",hash=" << hash << ",_msgSign=" << _msgSign;
	return dev::verify(_nodeID, _msgSign, hash);
}

bytes Raft::authBytes()
{
	bytes ret;
	RLPStream authListStream;
	authListStream.appendList(m_idVoted.size()*2);

	for(auto it : m_idVoted){
		authListStream << it.first; 
		authListStream << it.second; 
	}

	authListStream.swapOut(ret);
	return ret;
}

void Raft::reSet()
{
	m_blockBytes = bytes();
	//m_currentView = 0;
	m_consensusTimeOut = 0;
	m_idVoted.clear();
	m_idUnVoted.clear();
	m_votedId = NodeID();
	m_recvedBlock = false;
	m_last_consensus_time = utcTime();

	bool have;
	bytes blockBytes;

	while(1){
		tie(have, blockBytes) = m_blocks.tryPop(0);
		if(!have)
			break;

		LOG(TRACE) << "reSet too much block:m_consensusState=" << m_consensusState << ",nodeCount()=" << nodeCount();
	}
	
	m_consensusState = raftInitial;

	LOG(TRACE) << "m_consensusState=" << m_consensusState << ",nodeCount()=" << nodeCount();
}

void Raft::voteBlockEnd()
{
	uint64_t now = utcTime(); 
	if(m_idUnVoted.size() >= (nodeCount()+1)/2 || now > m_consensusTimeOut){
		LOG(TRACE) << "m_idUnVoted.size()=" << m_idUnVoted.size() << ",m_idVoted.size()=" << m_idVoted.size() << ",nodeCount()=" << nodeCount();
		
		reSet();
		return;
	}
	
	if(raftFinished != m_consensusState && m_idVoted.size() > nodeCount()/2){
		LOG(TRACE) << "Vote succed, m_blockBytes.size() = " << m_blockBytes.size();

		try{
			if(m_onSealGenerated){
				std::vector<std::pair<u256, Signature>> sig_list;

				for(size_t i = 0; i < m_miner_list.size(); i++){
					if(m_idVoted.count(m_miner_list[i])){
						sig_list.push_back(std::make_pair(u256(i), m_idVoted[m_miner_list[i]]));
					}
				}

				BlockHeader header(m_blockBytes);
				RLP r(m_blockBytes);
				RLPStream rs;
				rs.appendList(5);
				rs.appendRaw(r[0].data()); // header
				rs.appendRaw(r[1].data()); // tx
				rs.appendRaw(r[2].data()); // uncles
				rs.append(header.hash()); // hash
				rs.appendVector(sig_list); // sign_list

				bytes blockBytes;
				rs.swapOut(blockBytes);
				
				LOG(TRACE) << "Vote_succed: idx.count blockBytes.size() = " << blockBytes.size() << ",header.number()=" << header.number() << ",header.hash()=" << header.hash(WithoutSeal) << ",generl_id = " << (m_votedId == NodeID() ? id() : m_votedId) << ",m_node_idx=" << static_cast<uint64_t>(m_node_idx);
				
				m_onSealGenerated(blockBytes, m_votedId == NodeID());
			}
		}catch(...){
			LOG(ERROR) << "m_consensusFinishedFunc run err";
		}

		m_consensusState = raftFinished;
		m_consensusTimeOut = utcTime() + m_consensusTimeInterval;
		return;
	}	
}

void Raft::onBlockVoteAck(RaftPeer* _p, RLP const& _r)
{	
	bool vote = _r[0][1].toInt();	
	uint64_t currentView = _r[0][2].toInt();
	Signature mySign = h520(_r[0][3].toBytes());
	NodeID idrecv(_r[0][4].toBytes());

	bool v = msgVerify(_p->id(), m_blockBytes, mySign);
	bool voteMatch = true;
	if(m_votedId != NodeID()){
		voteMatch = (idrecv == m_votedId);
	}else{
		voteMatch = (id() == idrecv);
	}
	
	LOG(TRACE) << ",v=" << v << ",voteMatch="  << voteMatch << ",currentView=" << currentView << ",nodeCount()=" << nodeCount() << ",vote=" << vote << ",m_currentView=" << m_currentView << ",m_blockNumber=" << m_blockNumber << ",idrecv=" << idrecv << ",m_consensusState=" << static_cast<unsigned>(m_consensusState);
	if(!v || currentView != m_currentView || !voteMatch || raftFinished == m_consensusState)
		return;
	
	if(vote){
		m_idVoted[_p->id()] = mySign;
	}else{
		m_idUnVoted.insert(_p->id());
	}

	LOG(TRACE) << ",m_idUnVoted.size()=" << m_idUnVoted.size() << ",m_votedId=" << m_votedId << ",m_idVoted.size()=" << m_idVoted.size() << ",nodeCount()=" << nodeCount();
	voteBlockEnd();
}

void Raft::onBlockVote(RaftPeer* _p, RLP const& _r)
{
	uint64_t currentView = _r[0][1].toInt();
	Signature mySign = h520(_r[0][2].toBytes());
	bytes blockBytes = _r[0][3].toBytes();
	BlockHeader header(blockBytes);
	uint64_t blockNumber = header.number().convert_to<uint64_t>();
	bool vote = false;
	uint64_t now = utcTime();

	bool verify = msgVerify(_p->id(), blockBytes, mySign);
	bool verifyblock = true;
	verifyblock = verifyBlock(blockBytes);

	LOG(TRACE) << "verify =" << verify << ",verifyblock=" << verifyblock << ",nodeCount()=" << nodeCount() << ",currentView=" << currentView << ",m_currentView=" << m_currentView << ",blockNumber=" << blockNumber << ",m_blockNumber=" << m_blockNumber;
	if(verify && verifyblock && m_blockNumber + 1 == blockNumber && currentView > m_currentView){
		vote = true;
		m_idVoted.clear();
		m_idUnVoted.clear();
		m_currentView = currentView;
		m_consensusState = raftWaitingVote;
		m_consensusTimeOut = utcTime() + m_consensusTimeInterval;
		m_votedId = _p->id();
		m_blockBytes = blockBytes;
		m_idVoted[_p->id()] = mySign;
		LOG(TRACE) << "m_currentView =" << m_currentView << ",m_votedId=" << m_votedId << ",m_blockBytes.size()=" << m_blockBytes.size();
	}
	
	h256 hash =  sha3(blockBytes);
	mySign = sign(header.hash(WithoutSeal));

	if(vote)
		m_idVoted[id()] = mySign;
	
	RLPStream data;
	data << raftBlockVoteEcho;
	data << (uint8_t)vote; 
	data << m_currentView; 
	data << mySign; 
	data << _p->id(); 

	multicast(m_miner_list, data);
	LOG(TRACE) << "hash=" << hash << ",mySign=" << mySign << ",blockBytes.size()=" << blockBytes.size() << ",vote=" << vote << ",_p->id()=" << _p->id();

	voteBlockEnd();
}

void Raft::voteBlockBegin()
{
	BlockHeader header(m_blockBytes);
	Signature mySign = sign(header.hash(WithoutSeal));
	RLPStream msg;
	msg << raftBlockVote;
	msg << ++m_currentView;
	msg << mySign;
	msg << m_blockBytes; 

	m_idVoted[id()] = mySign;
	
	m_consensusTimeOut = utcTime() + m_consensusTimeInterval;
	m_consensusState = raftWaitingVote;

	multicast(m_miner_list, msg);
	LOG(TRACE) << ",header.hash(WithoutSeal)=" << header.hash(WithoutSeal) << ",mySign=" << mySign << ",m_blockBytes.size()=" << m_blockBytes.size();
}

void Raft::generateSealBegin(bytes const& _block)
{
	if(m_account_type != EN_ACCOUNT_TYPE_MINER || m_consensusState != raftInitial)
		return;

	m_blockBytes = _block;

	LOG(TRACE) << "m_miner_list.size()=" << m_miner_list.size() << ",m_consensusState=" << m_consensusState << ",m_consensusRandTimeInterval=" << m_consensusRandTimeInterval << ",m_blockBytes.size()=" << m_blockBytes.size();
	assert(m_miner_list.size() >= 1);

	if(1 == m_miner_list.size() || 0 == m_consensusRandTimeInterval){
		voteBlockBegin();
	}else{
		m_consensusState = raftPrePrepare;
		m_consensusTimeOut = utcTime() + rand()%m_consensusRandTimeInterval;
	}
}

void Raft::generateSeal(BlockHeader const& _bi, bytes const& _block)
{
	(void)_bi;
	
	if(m_account_type != EN_ACCOUNT_TYPE_MINER)
		return;

	m_blocks.push(_block);
	kill(m_hostTid, RAFT_SIGNAL);
	m_recvedBlock = true;
}

bool Raft::shouldSeal(Interface * _client)
{
	unsigned long long number = m_blockNumberRecv;
	uint64_t blockNumber = _client->number();
	bool recvedBlock = m_recvedBlock;
	
	LOG(TRACE) << "blockNumber = " << blockNumber << ",m_blockNumberRecv=" << number << ",m_consensusState=" << m_consensusState << ",recvedBlock=" << recvedBlock;
	return m_consensusState == raftInitial && !recvedBlock;
}

h512s Raft::getMinerNodeList()
{	
	h512s ret;
	for(auto i : m_miner_list)
		ret.push_back(i);

	return ret;
}

bool Raft::checkBlockSign(BlockHeader const& _header, std::vector<std::pair<u256, Signature>> _sign_list) 
{
	Timer t;
	LOG(TRACE) << "PBFT::checkBlockSign " << _header.number();

	h512s miner_list;
	if (!getMinerList(miner_list, static_cast<int>(_header.number() - 1))) {
		LOG(ERROR) << "checkBlockSign failed for getMinerList return false, blk=" <<  _header.number() - 1;
		return false;
	}

	LOG(DEBUG) << "checkBlockSign call getAllNodeConnInfo: blk=" << _header.number() - 1 << ", miner_list.size()=" << miner_list.size();

	unsigned singCount = 0;
	for (auto item : _sign_list) {
		unsigned idx = item.first.convert_to<unsigned>();
		if (idx >=  miner_list.size() || !dev::verify(miner_list[idx], item.second, _header.hash(WithoutSeal))) {
			LOG(ERROR) << "checkBlockSign failed, verify false, blk=" << _header.number() << ",hash=" << _header.hash(WithoutSeal);
			continue;
		}

		singCount++;
		if(singCount >= (miner_list.size()+1)/2)
			return true;
	}

	LOG(INFO) << "checkBlockSign success, blk=" << _header.number() << ",hash=" << _header.hash(WithoutSeal) << ",timecost=" << t.elapsed() / 1000 << "ms";
	return false;
}


