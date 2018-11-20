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
 * @file: RaftClient.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include <libethereum/Client.h>

namespace dev
{
namespace eth
{

class Raft;

DEV_SIMPLE_EXCEPTION(NotRaftSealEngine);
DEV_SIMPLE_EXCEPTION(ChainParamsNotRaft);
DEV_SIMPLE_EXCEPTION(InitRaftFailed);

class RaftClient: public Client
{
public:
	/// Trivial forwarding constructor.
	RaftClient(
	    ChainParams const& _params,
	    int _networkID,
	    std::shared_ptr<p2p::HostApi> _host,
	    std::shared_ptr<GasPricer> _gpForAdoption,
	    std::string const& _dbPath = std::string(),
	    WithExisting _forceAction = WithExisting::Trust,
	    TransactionQueue::Limits const& _l = TransactionQueue::Limits {1024, 1024}
	);

	virtual ~RaftClient();

	void startSealing() override;
	void stopSealing() override;
	//bool isMining() const override { return isWorking(); }

	Raft* raft() const;

	const BlockHeader & lastCommitedBlock() const { Guard l(x_last_block); return m_last_commited_block; }

protected:
	void init(ChainParams const& _params, std::shared_ptr<p2p::HostApi> _host);
	void rejigSealing() override;
	void syncBlockQueue() override;

	mutable Mutex x_last_block;
	u256  m_last_block_time;
	BlockHeader  m_last_commited_block;
};

RaftClient& asRaftClient(Interface& _c);
RaftClient* asRaftClient(Interface* _c);

}
}
