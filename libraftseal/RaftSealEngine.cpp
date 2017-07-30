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
/** @file Ethash.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include <libethereum/Interface.h>
#include <libethcore/ChainOperationParams.h>
#include <libethcore/CommonJS.h>
#include <libp2p/Host.h>

#include <libethereum/Client.h>
#include <libethereum/EthereumHost.h>

#include <tr1/memory>
#include <boost/algorithm/string.hpp>

#include "RaftSealEngine.h"
#include "Common.h"
#include "Raft.h"

using namespace std;
using namespace dev;
using namespace eth;
//using namespace p2p;

RaftSealEngine::RaftSealEngine()
{
}

void RaftSealEngine::initEnv(class Client *_c, p2p::Host *_host, BlockChain* _bc)
{
	m_client = _c;
	m_host = _host;
	m_pair = _host->keyPair();
	m_bc = _bc;

	m_LeaderHost = new RaftHost(this);
	std::shared_ptr<RaftHost> ptr(m_LeaderHost);// = std::make_shared<LeaderHostCapability>();
	m_host->registerCapability(ptr);
}

void RaftSealEngine::init()
{
	static SealEngineFactory __eth_registerSealEngineFactoryRaft = SealEngineRegistrar::registerSealEngine<Raft>("RAFT");
}

void RaftSealEngine::populateFromParent(BlockHeader& _bi, BlockHeader const& _parent) const
{
	SealEngineFace::populateFromParent(_bi, _parent);
	_bi.setGasLimit(chainParams().u256Param("maxGasLimit"));
}

void RaftSealEngine::multicast(const set<NodeID> &_id, RLPStream& _msg)
{
	if(_id.empty())
		return;
	
	m_LeaderHost->foreachPeer([&](std::shared_ptr<RaftPeer> _p)
	{
		if(_id.count(_p->id())){
			RLPStream s;
			_p->prep(s, RaftMsgRaft, 1);
			s.appendList(_msg);
			_p->sealAndSend(s);
		}
		return true;
	});
}

void RaftSealEngine::multicast(const h512s &miner_list, RLPStream& _msg)
{
	set<NodeID> id;
	id.insert(miner_list.begin(), miner_list.end());

	multicast(id, _msg);
}

bool RaftSealEngine::verifyBlock(const bytes& _block, ImportRequirements::value _ir) const
{
	try{
		m_bc->verifyBlock(&_block, nullptr, _ir);
	}catch(...){
		return false;
	}

	return true;
}

void RaftSealEngine::tick() 
{
}

void RaftSealEngine::workLoop()
{/*
	while (isWorking()){
		this->tick();
	
		this_thread::sleep_for(std::chrono::milliseconds(100));
	}*/
}

bool RaftSealEngine::getMinerList(set<NodeID> &_nodes, int _blk_no) const 
{
	std::map<std::string, NodeConnParams> all_node;
	NodeConnManagerSingleton::GetInstance().getAllNodeConnInfo(_blk_no, all_node);

	for (auto iter = all_node.begin(); iter != all_node.end(); ++iter) {
		if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) {
			_nodes.insert(jsToPublic(toJS(iter->second._sNodeId)));
		}
	}

	LOG(TRACE) << "_nodes=" << _nodes;
	return true;
}

bool RaftSealEngine::getMinerList(h512s &_miner_list, int _blk_no) const 
{
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

bool RaftSealEngine::getIdx(map<NodeID, unsigned> &_idx, int _blk_no) const
{
	std::map<std::string, NodeConnParams> all_node;
	NodeConnManagerSingleton::GetInstance().getAllNodeConnInfo(_blk_no, all_node);

	unsigned miner_num = 0;
	for (auto iter = all_node.begin(); iter != all_node.end(); ++iter) {
		if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) {
			++miner_num;
		}
	}

	for (auto iter = all_node.begin(); iter != all_node.end(); ++iter) {
		if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) {
			auto idx = static_cast<unsigned>(iter->second._iIdx);
			if (idx >= miner_num) {
				LOG(ERROR) << "getMinerList return false cause for idx=" << idx << ",miner_num=" << miner_num;
				return false;
			}

			_idx[jsToPublic(toJS(iter->second._sNodeId))] = idx;
		}
	}

	return true;
}

bool RaftSealEngine::interpret(RaftPeer* _peer, unsigned _id, RLP const& _r) 
{
	(void)_peer;
	(void)_id;
	(void)_r;

	return true;
}

EVMSchedule const& RaftSealEngine::evmSchedule(EnvInfo const& _envInfo) const
{
	(void)_envInfo;
	return DefaultSchedule;
}

