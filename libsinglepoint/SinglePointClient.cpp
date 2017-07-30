

#include "SinglePointClient.h"
#include "SinglePoint.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace p2p;

DEV_SIMPLE_EXCEPTION(ChainParamsNotSinglePoint);

SinglePointClient::SinglePointClient(
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




