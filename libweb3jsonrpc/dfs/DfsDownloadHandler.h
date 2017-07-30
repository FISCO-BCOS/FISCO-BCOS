#pragma once

#include "IUrlHandler.h"

namespace dev
{

namespace rpc
{

namespace fs
{

class DfsDownloadHandler : public IUrlHandler
{
public:
	DfsDownloadHandler(const string& version, const string &method);
	virtual ~DfsDownloadHandler();

public:
	virtual int handle(DfsHttpInfo* http_info, DfsUrlInfo* url_info);
	virtual void handle_request_completed (void *cls, struct MHD_Connection *connection,
                   void **con_cls, enum MHD_RequestTerminationCode toe);
};

}
}
}
