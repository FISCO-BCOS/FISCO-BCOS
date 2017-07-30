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
/** @file Ethash.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 *
 * A proof of work algorithm.
 */

#pragma once

#include <sys/types.h>

#include <libethcore/SealEngine.h>
#include <libethereum/GenericFarm.h>
#include <libp2p/Capability.h>
#include <libp2p/Common.h>
#include <libethereum/ChainParams.h>
#include <libethereum/BlockChain.h>

#include "RaftHost.h"
#include "RaftPeer.h"

#include <atomic>
#include <string>
#include <vector>
#include <set>
#include <map>

using namespace std;
using namespace dev;
using namespace eth;

namespace dev
{

using namespace p2p;

namespace eth
{

class RaftHost;
class RaftPeer;

class RaftSealEngine: public eth::SealEngineBase//, Worker
{
public:
	RaftSealEngine();
	std::string name() const override { return "RAFT"; }
	static void init();
	
	virtual void initEnv(class Client *_c, p2p::Host *_host, BlockChain* _bc);

	void startGeneration() {/* startWorking(); */}
	//void cancelGeneration() override { stopWorking(); }
	
	virtual bool shouldSeal(Interface*){return true;};
	void populateFromParent(BlockHeader& _bi, BlockHeader const& _parent) const;
	virtual bool interpret(RaftPeer*, unsigned _id, RLP const& _r);

	EVMSchedule const& evmSchedule(EnvInfo const&) const override;

	
protected:
	virtual void tick();
	bool getMinerList(set<NodeID> &_nodes, int _blk_no = -1) const;
	bool getMinerList(h512s &_miner_list, int _blk_no = -1) const;
	bool getIdx(map<NodeID, unsigned> &_idx, int _blk_no = -1) const;

		
	Signature sign(h256 const& _hash){return dev::sign(m_pair.secret(), _hash);};
	bool verify(Signature const& _s, h256 const& _hash){return dev::verify(m_pair.pub(), _s, _hash);};

	void multicast(const set<NodeID> &_id, RLPStream& _msg);
	void multicast(const h512s &_miner_list, RLPStream& _msg);

	bool verifyBlock(const bytes& _block, ImportRequirements::value _ir = ImportRequirements::OutOfOrderChecks) const;

	NodeID id() const{ return m_pair.pub();};
	
private:
	virtual void workLoop();	
private:
	class Client *m_client = 0;
	RaftHost* m_LeaderHost = 0;
	p2p::Host* m_host = 0;
	KeyPair m_pair;
	BlockChain *m_bc;
};

}
}
