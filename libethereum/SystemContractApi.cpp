#include "SystemContractApi.h"
#include "SystemContract.h"

using namespace dev::eth;


std::shared_ptr<SystemContractApi> SystemContractApiFactory::create(const Address _address, const Address _god, Client* _client)
{
	return shared_ptr<SystemContractApi>(new SystemContract(_address, _god, _client));
}