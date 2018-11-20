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
 * @file: SinglePointClient.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "SinglePointClient.h"
#include "SinglePoint.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace p2p;

/*
SinglePointClient& dev::eth::asEthashClient(Interface& _c)
{
	if (dynamic_cast<SinglePoint*>(_c.sealEngine()))
		return dynamic_cast<SinglePointClient&>(_c);
	throw InvalidSealEngine();
}

SinglePointClient* dev::eth::asEthashClient(Interface* _c)
{
	if (dynamic_cast<SinglePoint*>(_c->sealEngine()))
		return &dynamic_cast<SinglePointClient&>(*_c);
	throw InvalidSealEngine();
}
*/

DEV_SIMPLE_EXCEPTION(ChainParamsNotSinglePoint);

SinglePointClient::SinglePointClient(
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
	// will throw if we're not an Ethash seal engine.
	//asEthashClient(*this);
}


bool SinglePointClient::isMining() const
{
	return true;
}



u256 SinglePointClient::hashrate() const
{
	return 1;
}

std::tuple<h256, h256, h256> SinglePointClient::getEthashWork()
{

	return std::tuple<h256, h256, h256>(h256(1), h256(1), h256(1));
}




