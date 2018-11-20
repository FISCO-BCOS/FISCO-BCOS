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
 * @file: SinglePointClient.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

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
	    std::shared_ptr<p2p::HostApi> _host,
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
