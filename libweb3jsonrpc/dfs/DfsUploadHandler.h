#pragma once

#include "IUrlHandler.h"


namespace dev
{

namespace rpc
{

namespace fs
{


class DfsUploadHandler : public IUrlHandler
{
public:
	DfsUploadHandler(const string& version, const string &method);
	virtual ~DfsUploadHandler();

public:
	virtual int handle(DfsHttpInfo* http_info, DfsUrlInfo* url_info);
	
	virtual void handle_request_completed (void *cls, struct MHD_Connection *connection,
                   void **con_cls, enum MHD_RequestTerminationCode toe);
};

}
}
}

