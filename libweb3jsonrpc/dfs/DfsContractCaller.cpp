#include "DfsContractCaller.h"
#include "libcontract/FileInfoManagerContract.h"
#include "libcontract/FileServerManagerContract.h"
#include "libdevcore/Log.h"

using namespace contract;

namespace dev
{

namespace rpc
{

namespace fs
{

static dev::eth::Client* psClient = NULL;
static FileInfoManagerContract* pFileInfoContract = NULL;
static FileServerManagerContract* pFileServerContract = NULL;

static void updateFileContracts()
{
	if (pFileInfoContract && pFileServerContract)
		return;
	
	//get the file info contract address, the fileserver contract address
    Address addrFileInfo = psClient->findContract("FileInfoManager");
    Address addrFileServer = psClient->findContract("FileServerManager");
	
	if (addrFileInfo == h160() || addrFileServer == h160())
	{
		LOG(ERROR) << "Client not ready or cannot find the system contract !";
		return;
	}

	pFileInfoContract = new FileInfoManagerContract(psClient, addrFileInfo);
	pFileServerContract = new FileServerManagerContract(psClient, addrFileServer);
	LOG(TRACE) << "update the file contracts and DFS is prepared ! OK";
	return;
}

int init_caller_client(dev::eth::Client* client)
{
	psClient = client;
	{
		updateFileContracts();
	}

	return 0;
}

int uninit_caller_client()
{
	psClient = NULL;
	if (pFileInfoContract)
	{
		delete pFileInfoContract;
		pFileInfoContract = NULL;
	}

	if (pFileServerContract)
	{
		delete pFileServerContract;
		pFileServerContract = NULL;
	}

	return 0;
}


int listFileInfo(string& result)
{
	updateFileContracts();
	if (!pFileInfoContract || !psClient)
	{
		return -1;
	}

	result.clear();
	if (0 != pFileInfoContract->listFileinfo(result))
	{
		return -1;
	}

	return 0;
}

int listGroupFileInfo(const string& group, string& result)
{
	updateFileContracts();
	if (!pFileInfoContract || !psClient)
	{
		return -1;
	}

	result.clear();
	if (0 != pFileInfoContract->listFileinfoByGroup(group, result))
	{
		return -1;
	}

	if (result.size() <= 0)
	{
		return -2;
	}
	
	return 0;
}

int findFileInfo(const string& fileid, string& result)
{
	updateFileContracts();
	if (!pFileInfoContract || !psClient)
	{
		return -1;
	}

	if (0 != pFileInfoContract->findFileInfo(fileid, result))
	{
		return -1;
	}

	return 0;
}

int listServerInfo(string& result)
{
	updateFileContracts();
	if (!psClient || !pFileServerContract)
	{
		return -1;
	}

	if (0 != pFileServerContract->listFileServer(result))
	{
		return -1;
	}

	return 0;
}

int findServerInfo(const string& serverid, string& result)
{
	updateFileContracts();
	if (!psClient || !pFileServerContract)
	{
		return -1;
	}

	if (0 != pFileServerContract->findServerInfo(serverid, result))
	{
		return -1;
	}

	return 0;
}

int pageByGroup(string _group, int _pageNo, string& result)
{
	updateFileContracts();
	if (!pFileInfoContract || !psClient)
	{
		return -1;
	}

	if (0 != pFileInfoContract->pageByGroup(_group, _pageNo, result))
	{
		return -1;
	}
	
	return 0;
}

int getGroupPageCount(const std::string& group, int& result)
{
	updateFileContracts();
	if (!pFileInfoContract || !psClient)
	{
		return -1;
	}

	if (0 != pFileInfoContract->getGroupPageCount(group, result))
	{
		return -1;
	}

	return 0;
}

int getPageCount(int& result)
{
	updateFileContracts();
	if (!pFileInfoContract || !psClient)
	{
		return -1;
	}

	if (0 != pFileInfoContract->getPageCount(result))
	{
		return -1;
	}

	return 0;
}

int checkServiceNode(const std::string& nodeid, bool& enable)
{
	updateFileContracts();
	if (!psClient || !pFileServerContract)
	{
		return -1;
	}

	if (0 != pFileServerContract->isServerEnable(nodeid, enable))
	{
		return -1;
	}

	return 0;
}


}
}
}
