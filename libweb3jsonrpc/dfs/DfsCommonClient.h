#pragma once


#include "DfsBase.h"
#include "DfsHttpClient.h"

namespace dev
{

namespace rpc
{

namespace fs
{

class DfsCommonClient : public DfsHttpClient
{
public:
	DfsCommonClient();
	virtual ~DfsCommonClient();


	virtual int fillUserData(JzRequestInfo& req_info);
	virtual int endRequest();
	virtual size_t handle_recv_data(void *ptr, size_t size, size_t nmemb, void *cls);
	
};

}
}
}