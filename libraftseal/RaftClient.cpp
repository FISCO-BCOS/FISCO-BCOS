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
 * @file: RaftClient.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <libethereum/EthereumHost.h>
#include <libethereum/NodeConnParamsManagerApi.h>
#include <libp2p/Host.h>
#include "RaftClient.h"
#include "Raft.h"
#include "RaftHost.h"
#include <libdevcore/easylog.h>
using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace p2p;

RaftClient& dev::eth::asRaftClient(Interface& _c)
{
	if (dynamic_cast<Raft*>(_c.sealEngine()))
		return dynamic_cast<RaftClient&>(_c);
	throw NotRaftSealEngine();
}

RaftClient* dev::eth::asRaftClient(Interface* _c)
{
	if (dynamic_cast<Raft*>(_c->sealEngine()))
		return &dynamic_cast<RaftClient&>(*_c);
	throw NotRaftSealEngine();
}

RaftClient::RaftClient(
    ChainParams const& _params,
    int _networkID,
    std::shared_ptr<p2p::HostApi> _host,
    std::shared_ptr<GasPricer> _gpForAdoption,
    std::string const& _dbPath,
    WithExisting _forceAction,
    TransactionQueue::Limits const& _limits
):
	Client(_params, _networkID, _host, _gpForAdoption, _dbPath, _forceAction, _limits)
{
	// will throw if we're not an Raft seal engine.
	asRaftClient(*this);

	init(_params, _host);
}

RaftClient::~RaftClient() {
	raft()->cancelGeneration();
	stopWorking();
}

void RaftClient::init(ChainParams const&, std::shared_ptr<p2p::HostApi> _host) {
	auto raft_host = _host->registerCapability(make_shared<RaftHost>([this](unsigned _id, std::shared_ptr<Capability> _peer, RLP const & _r) {
		raft()->onRaftMsg(_id, _peer, _r);
	}));

	raft()->initEnv(raft_host,  _host->keyPair(), static_cast<unsigned>(raft()->getIntervalBlockTime()), static_cast<unsigned>(raft()->getIntervalBlockTime()) + 1000);

	raft()->reportBlock(bc().info());

	LOG(INFO) << "Init RaftClient success";
}

Raft* RaftClient::raft() const
{
	return dynamic_cast<Raft*>(Client::sealEngine());
}

void RaftClient::startSealing() {
	setName("Client");
	raft()->reportBlock(bc().info());
	raft()->startGeneration();
	Client::startSealing();
}

void RaftClient::stopSealing() {
	Client::stopSealing();
	raft()->cancelGeneration();
}

void RaftClient::syncBlockQueue() {
	Client::syncBlockQueue();
	raft()->reportBlock(bc().info());

	u256 time = bc().info().timestamp();

	Guard l(x_last_block);
	if (time > m_last_block_time) {
		m_last_block_time = time;
	}
}

void RaftClient::rejigSealing()
{
	bool would_seal = m_wouldSeal && (raft()->accountType() == EN_ACCOUNT_TYPE_MINER);
	bool is_major_syncing = isMajorSyncing();
	if (would_seal && !is_major_syncing)
	{
		if (sealEngine()->shouldSeal(this))
		{
			m_wouldButShouldnot = false;

			VLOG(10) << "Rejigging seal engine...";
			DEV_WRITE_GUARDED(x_working)
			{
				if (m_working.isSealed())
				{
					VLOG(10) << "Tried to seal sealed block...";
					return;
				}

				u256 now_time = u256(utcTime());
				u256 parent_time = m_last_block_time; //bc().info().timestamp();
				size_t tx_num = m_working.pending().size();
				if (tx_num < m_maxBlockTranscations &&  now_time - parent_time < sealEngine()->getIntervalBlockTime()) {
					VLOG(10) << "Wait for next interval, tx:" << tx_num << ", parent_time:" << parent_time << ",now:" << now_time << ", interval=" << now_time - parent_time << "ms";
					return;
				}

				m_working.setIndex(raft()->nodeIdx());
				m_working.commitToSeal(bc(), m_extraData);

				bc().addBlockCache(m_working, m_working.info().difficulty());
			}

			DEV_READ_GUARDED(x_working)
			{
				DEV_WRITE_GUARDED(x_postSeal)
				m_postSeal = m_working;
				m_sealingInfo = m_working.info();
			}

			if (wouldSeal())
			{
				sealEngine()->onSealGenerated([ = ](bytes const & header) {
					if (!this->submitSealed(header)) {
						LOG(ERROR) << "Submitting block failed...";
					} else {
						Guard l(x_last_block);
						m_last_commited_block = BlockHeader(header, HeaderData);
						u256 time = m_last_commited_block.timestamp();
						if (time > m_last_block_time) {
							m_last_block_time = time;
						}
					}
				});
				LOG(INFO) << "-------------------------Generating seal on" << m_sealingInfo.hash(WithoutSeal) << "#" << m_sealingInfo.number();
				sealEngine()->generateSeal(m_sealingInfo);
			}
		}
		else
			m_wouldButShouldnot = true;
	}
}

