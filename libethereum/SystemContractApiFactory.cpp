#include "SystemContractApi.h"
#include "SystemContractApiFactory.h"
#include "SystemContract.h"
#include "SystemContractSSL.h"
#include <libdevcore/FileSystem.h>

using namespace dev::eth;


std::shared_ptr<SystemContractApi> SystemContractApiFactory::create(const Address _address, const Address _god, Client* _client)
{
    if (dev::getSSL() == SSL_SOCKET_V2)
	{
		return shared_ptr<SystemContractApi>(new SystemContractSSL(_address, _god, _client));
	}
	else
	{
		return shared_ptr<SystemContractApi>(new SystemContract(_address, _god, _client));
	}
}