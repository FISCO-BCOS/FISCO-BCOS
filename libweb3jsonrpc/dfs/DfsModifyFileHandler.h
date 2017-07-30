#pragma once

#include "IUrlHandler.h"


namespace dev
{

namespace rpc
{

namespace fs
{


class DfsModifyFileHandler : public IUrlHandler
{
public:
	DfsModifyFileHandler(const string& version, const string &method);
	virtual ~DfsModifyFileHandler();

public:
	virtual int handle(DfsHttpInfo* http_info, DfsUrlInfo* url_info);

	virtual void handle_request_completed(void *cls, struct MHD_Connection *connection,
                   void **con_cls, enum MHD_RequestTerminationCode toe);

	//parse file info json:  eg.  file info json: {\"fileid\\":\"file_1111\",\"filename\":\"file_name_111\"}
	int parseFileInfo(const string& json, DfsFileInfo& fileInfo);
	
	//revise the file info
	int reviseFileInfo(const DfsFileInfo& fileInfo);

};

}
}
}
