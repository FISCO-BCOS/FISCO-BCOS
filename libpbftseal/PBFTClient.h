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
 * @file: PBFTClient.h
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

class PBFT;

DEV_SIMPLE_EXCEPTION(NotPBFTSealEngine);
DEV_SIMPLE_EXCEPTION(ChainParamsNotPBFT);
DEV_SIMPLE_EXCEPTION(InitFailed);

class PBFTClient: public Client
{
public:
	/// Trivial forwarding constructor.
	PBFTClient(
	    ChainParams const& _params,
	    int _networkID,
	    std::shared_ptr<p2p::HostApi> _host,
	    std::shared_ptr<GasPricer> _gpForAdoption,
	    std::string const& _dbPath = std::string(),
	    WithExisting _forceAction = WithExisting::Trust,
	    TransactionQueue::Limits const& _l = TransactionQueue::Limits {1024, 1024}
	);

	virtual ~PBFTClient();

	void startSealing() override;
	void stopSealing() override;
	//bool isMining() const override { return isWorking(); }

	PBFT* pbft() const;

protected:
	void init(ChainParams const& _params, std::shared_ptr<p2p::HostApi> _host);
	void doWork(bool _doWait) override;
	void rejigSealing() override;
	void syncBlockQueue() override;
	void syncTransactionQueue(u256 const& _max_block_txs);
	void executeTransaction();
	void onTransactionQueueReady() override;

	bool submitSealed(bytes const & _block, bool _isOurs);

private:
	bool  m_empty_block_flag;
	float m_exec_time_per_tx;
	uint64_t m_last_exec_finish_time;
	uint64_t m_left_time;

	ChainParams m_params;
};

PBFTClient& asPBFTClient(Interface& _c);
PBFTClient* asPBFTClient(Interface* _c);

}
}
