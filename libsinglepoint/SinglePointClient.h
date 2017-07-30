

#pragma once

#include <tuple>
#include <libethereum/Client.h>
#include "SinglePoint.h"

namespace dev
{
namespace eth
{

class SinglePoint;

DEV_SIMPLE_EXCEPTION(InvalidSealEngine);

class SinglePointClient: public Client
{
public:
	/// Trivial forwarding constructor.
	SinglePointClient(
	    ChainParams const& _params,
	    int _networkID,
	    p2p::Host* _host,
	    std::shared_ptr<GasPricer> _gpForAdoption,
	    std::string const& _dbPath = std::string(),
	    WithExisting _forceAction = WithExisting::Trust,
	    TransactionQueue::Limits const& _l = TransactionQueue::Limits {2048, 2048}
	);





	/// Are we mining now?
	bool isMining() const;

	/// The hashrate...
	u256 hashrate() const;

	std::tuple<h256, h256, h256> getEthashWork();




};
/*
SinglePointClient& asEthashClient(Interface& _c);
SinglePointClient* asEthashClient(Interface* _c);*/

}
}
