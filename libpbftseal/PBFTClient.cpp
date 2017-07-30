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
#include "PBFTClient.h"
#include "PBFT.h"
#include "PBFTHost.h"
#include <libdevcore/easylog.h>
using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace p2p;

PBFTClient& dev::eth::asPBFTClient(Interface& _c)
{
	if (dynamic_cast<PBFT*>(_c.sealEngine()))
		return dynamic_cast<PBFTClient&>(_c);
	throw NotPBFTSealEngine();
}

PBFTClient* dev::eth::asPBFTClient(Interface* _c)
{
	if (dynamic_cast<PBFT*>(_c->sealEngine()))
		return &dynamic_cast<PBFTClient&>(*_c);
	throw NotPBFTSealEngine();
}

PBFTClient::PBFTClient(
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
	// will throw if we're not an PBFT seal engine.
	asPBFTClient(*this);

	init(_params, _host);
}

PBFTClient::~PBFTClient() {
	pbft()->cancelGeneration();
	stopWorking();
}

void PBFTClient::init(ChainParams const& _params, p2p::Host *_host) {
	m_params = _params;
	m_working.setEvmCoverLog(m_params.evmCoverLog);
	m_working.setEvmEventLog(m_params.evmEventLog);


	// register PBFTHost
	auto pbft_host = _host->registerCapability(make_shared<PBFTHost>([this](unsigned _id, std::shared_ptr<Capability> _peer, RLP const & _r) {
		pbft()->onPBFTMsg(_id, _peer, _r);
	}));

	pbft()->initEnv(pbft_host, &m_bc, &m_stateDB, &m_bq, _host->keyPair(), static_cast<unsigned>(sealEngine()->getIntervalBlockTime()) * 3);

	pbft()->reportBlock(bc().info(), bc().details().totalDifficulty);

	pbft()->onSealGenerated([ this ](bytes const & _block, bool _isOurs) {
		if (!submitSealed(_block, _isOurs))
			LOG(ERROR) << "Submitting block failed...";
	});

	pbft()->onViewChange([this]() {
		DEV_WRITE_GUARDED(x_working)
		{
			if (m_working.isSealed()) {
				m_working.resetCurrent();
			}
		}
	});

	LOG(INFO) << "Init PBFTClient success";
}

PBFT* PBFTClient::pbft() const
{
	return dynamic_cast<PBFT*>(Client::sealEngine());
}

void PBFTClient::startSealing() {
	setName("Client");
	pbft()->reportBlock(bc().info(), bc().details().totalDifficulty);
	pbft()->startGeneration();
	Client::startSealing();
}

void PBFTClient::stopSealing() {
	Client::stopSealing();
	pbft()->cancelGeneration();
}

void PBFTClient::syncBlockQueue() {
	Client::syncBlockQueue();

	pbft()->reportBlock(bc().info(), bc().details().totalDifficulty);

	DEV_WRITE_GUARDED(x_working)
	{
		if (m_working.isSealed() && m_working.info().number() <= bc().info().number()) {
			m_working.resetCurrent();
		}
	}
}

void PBFTClient::onTransactionQueueReady() {
	m_syncTransactionQueue = true;
	m_signalled.notify_all();
	// 通知EthereumHost去广播交易
	if (auto h = m_host.lock()) {
		h->noteNewTransactions();
	}
}

void PBFTClient::syncTransactionQueue()
{
	Timer timer;

	h256Hash changeds;
	TransactionReceipts newPendingReceipts;

	DEV_WRITE_GUARDED(x_working)
	{
		if (m_working.isSealed())
		{
			LOG(TRACE) << "Skipping txq sync for a sealed block.";
			return;
		}
		if ( m_working.pending().size() >= m_maxBlockTranscations )
		{
			LOG(TRACE) << "Skipping txq sync for Full block .";
			return;
		}

		tie(newPendingReceipts, m_syncTransactionQueue) = m_working.sync(bc(), m_tq, *m_gp);
	}

	if (newPendingReceipts.empty())
	{
		auto s = m_tq.status();
		LOG(TRACE) << "No transactions to process. " << m_working.pending().size() << " pending, " << s.current << " queued, " << s.future << " future, " << s.unverified << " unverified";
		return;
	}

	DEV_READ_GUARDED(x_working)
	DEV_WRITE_GUARDED(x_postSeal)
	m_postSeal = m_working;

	DEV_READ_GUARDED(x_postSeal)
	for (size_t i = 0; i < newPendingReceipts.size(); i++)
		appendFromNewPending(newPendingReceipts[i], changeds, m_postSeal.pending()[i].sha3());

	// Tell farm about new transaction (i.e. restart mining).
	onPostStateChanged();

	// Tell watches about the new transactions.
	noteChanged(changeds);

	LOG(TRACE) << "Processed " << newPendingReceipts.size() << " transactions in" << (timer.elapsed() * 1000) << "(" << (bool)m_syncTransactionQueue << ")";

	cblockstat << m_working.info().number() << ",trans=" << newPendingReceipts.size() << " 交易处理速度=" << ((1000 * newPendingReceipts.size()) / ((timer.elapsed() * 1000000) / 1000)) << "/S";

}

void PBFTClient::doWork(bool _doWait)
{
	bool t = true;
	if (m_syncBlockQueue.compare_exchange_strong(t, false))
		syncBlockQueue();

	if (m_needStateReset)
	{
		resetState();
		m_needStateReset = false;
	}

	tick();

	rejigSealing();

	callQueuedFunctions();

	if (!m_syncBlockQueue && _doWait)
	{
		std::unique_lock<std::mutex> l(x_signalled);
		m_signalled.wait_for(l, chrono::milliseconds(10));
	}
}

void PBFTClient::rejigSealing() {
	bool would_seal = m_wouldSeal && (pbft()->accountType() == EN_ACCOUNT_TYPE_MINER);
	bool is_major_syncing = isMajorSyncing();
	if (would_seal && !is_major_syncing)
	{
		if (pbft()->shouldSeal(this)) // 自己是不是leader？
		{
			DEV_READ_GUARDED(x_working)
			{
				if (m_working.isSealed()) {
					LOG(TRACE) << "Tried to seal sealed block...";
					return;
				}
			}

			// 被选为leader之后才开始打包
			bool t = true;
			if (!isSyncing() && !m_remoteWorking && m_syncTransactionQueue.compare_exchange_strong(t, false)) {
				syncTransactionQueue();
			}

			DEV_READ_GUARDED(x_working)
			{
				// 判断是否已够
				size_t tx_num = m_working.pending().size();
				//auto last_consensus_time = pbft()->lastConsensusTime();
				auto last_exec_finish_time = pbft()->lastExecFinishTime();
				auto now_time = utcTime();
				//auto proc_time = last_consensus_time - static_cast<uint64_t>(bc().info().timestamp());
				auto max_interval = static_cast<uint64_t>(sealEngine()->getIntervalBlockTime());
				/*if (proc_time > max_interval) {
					max_interval = 0;
				} else {
					max_interval -= proc_time;
				}*/

				if (tx_num < m_maxBlockTranscations && now_time - last_exec_finish_time < max_interval) {
					//LOG(TRACE) << "Wait for next interval, tx:" << tx_num;
					return;
				}
			}

			DEV_WRITE_GUARDED(x_working)
			{
				// 出块
				DEV_BLOCK_STAT_LOG("", m_working.info().number(), utcTime(), "commitToSeal");
				//m_working.resetCurrentTime();
				m_working.setIndex(pbft()->nodeIdx());
				m_working.setNodeList(pbft()->getMinerNodeList());
				m_working.commitToSeal(bc(), m_extraData);

				m_sealingInfo = m_working.info();
				RLPStream ts;
				m_sealingInfo.streamRLP(ts, WithoutSeal);


				m_working.setEvmCoverLog(m_params.evmCoverLog);
				m_working.setEvmEventLog(m_params.evmEventLog);

				//增加cache
				bc().addBlockCache(m_working, m_working.info().difficulty());

				if (!m_working.sealBlock(m_sealingInfo, &ts.out())) {
					LOG(INFO) << "Error: sealBlock failed";
					return;
				}
			}

			DEV_READ_GUARDED(x_working)
			{
				DEV_WRITE_GUARDED(x_postSeal)
				m_postSeal = m_working;

				if (would_seal)
				{
					LOG(INFO) << "+++++++++++++++++++++++++++ Generating seal on " << m_sealingInfo.hash(WithoutSeal).abridged() << "#" << m_sealingInfo.number() << "tx:" << m_working.pending().size();
					pbft()->generateSeal(m_sealingInfo, m_working.blockData());
				}
			}
		}
	}
}

bool PBFTClient::submitSealed(bytes const & _block, bool _isOurs) {
	auto ret = m_bq.import(&_block, _isOurs);
	LOG(TRACE) << "PBFTClient::submitSealed m_bq.import return " << (unsigned)ret;
	return ret == ImportResult::Success;
}
