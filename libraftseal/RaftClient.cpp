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

#include <libethereum/EthereumHost.h>
#include <libethereum/NodeConnParamsManagerApi.h>
#include <libp2p/Host.h>
#include "RaftClient.h"
#include "Common.h"
#include "Raft.h"
#include "RaftHost.h"
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
    p2p::Host* _host,
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
	m_last_import_time = utcTime();
}

RaftClient::~RaftClient() {
	raft()->cancelGeneration();
	stopWorking();
}

void RaftClient::init(ChainParams const&, p2p::Host *_host) {
	raft()->initEnv(this, _host, &m_bc);

	raft()->onSealGenerated([ = ](bytes const & _block, bool _isOurs) {
		if (!this->submitSealed(_block, _isOurs)) {
			clog(ClientNote) << "Submitting block failed...";
		} else {
			//Guard l(x_last_block);
			//m_last_commited_block = BlockHeader(header, HeaderData);
			//u256 time = m_last_commited_block.timestamp();

			SetLastTime();
		}
	});

	clog(ClientNote) << "Init RaftClient success";
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

void RaftClient::SetLastTime()
{
	uint64_t time = bc().info().timestamp().convert_to<uint64_t>();

	if (time > m_last_block_time) {
		m_last_block_time = time;
	}

}

void RaftClient::syncBlockQueue() {
	Client::syncBlockQueue();
	raft()->reportBlock(bc().info());

	SetLastTime();
}

bool RaftClient::submitSealed(bytes const& _block, bool _isOurs)
{
	uint64_t now = utcTime();
	uint64_t interval = now - m_last_import_time;
	m_last_import_time = now;
	LOG(TRACE) << "Client_submitSealed: _block.size() = " << _block.size() << ",_isOurs=" << _isOurs << ",interval=" << interval;

	// OPTIMISE: very inefficient to not utilise the existing OverlayDB in m_postSeal that contains all trie changes.
	return m_bq.import(&_block, _isOurs) == ImportResult::Success;
}

void RaftClient::rejigSealing()
{
	bytes blockBytes;
	bool would_seal = m_wouldSeal && wouldSeal() && (raft()->accountType() == EN_ACCOUNT_TYPE_MINER);
	bool is_major_syncing = isMajorSyncing();
	if (would_seal && !is_major_syncing)
	{
		if (sealEngine()->shouldSeal(this))
		{

			m_wouldButShouldnot = false;

			LOG(TRACE) << "Rejigging seal engine... m_maxBlockTranscations = " << m_maxBlockTranscations;
			DEV_WRITE_GUARDED(x_working)
			{
				if (m_working.isSealed())
				{
					clog(ClientTrace) << "Tried to seal sealed block...";
					return;
				}

				u256 now_time = u256(utcTime());
				auto last_exec_finish_time = raft()->lastConsensusTime();
				size_t tx_num = m_working.pending().size();
				if (tx_num < m_maxBlockTranscations &&  now_time - last_exec_finish_time < sealEngine()->getIntervalBlockTime()) {
					clog(ClientTrace) << "Wait for next interval, tx:" << tx_num << ", last_exec_finish_time:" << last_exec_finish_time << ",now:" << now_time << ", interval=" << now_time - last_exec_finish_time << "ms" << ",sealEngine()->getIntervalBlockTime()=" << sealEngine()->getIntervalBlockTime();
					return;
				}

				if (m_working.empty())
					m_working.resetCurrentTime();
				m_working.setIndex(raft()->nodeIdx());
				//m_working.setNodeList(raft()->getMinerNodeList());
				m_working.commitToSeal(bc(), m_extraData);
			}
			DEV_READ_GUARDED(x_working)
			{
				DEV_WRITE_GUARDED(x_postSeal)
				m_postSeal = m_working;
				m_sealingInfo = m_working.info();
				blockBytes = m_working.blockBytes();
			}

			if (wouldSeal())
			{
				clog(ClientNote) << "-------------------------Generating seal on" << m_sealingInfo.hash(WithoutSeal) << "#" << m_sealingInfo.number() << ",m_working.empty()=" << m_working.empty();
				raft()->generateSeal(BlockHeader(), blockBytes);
			}
		}
		else {
			m_wouldButShouldnot = true;
			LOG(TRACE) << "m_wouldButShouldnot=" << m_wouldButShouldnot;
			//this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
	}/* else {
		clog(ClientTrace) << "would_seal=" << would_seal << ",is_major_syncing=" << is_major_syncing;
		if (is_major_syncing) {
			if (auto h = m_host.lock()) {
				clog(ClientTrace) << "state=" << EthereumHost::stateName(h->status().state);
			}
		}
	}*/
}

