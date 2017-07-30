/**
* @file IUrlHandler.h
* @time 2017-02-15
**@desc URL handler interface class
*/

#pragma once

#include "DfsBase.h"


using std::string;

namespace dev
{

namespace rpc
{

namespace fs
{

class IUrlHandler
{
public:
    IUrlHandler(const string& version, const string &method){m_Version = version; m_Method = method;};
    virtual ~IUrlHandler(){};

public:
	virtual int handle(DfsHttpInfo* http_info, DfsUrlInfo* url_info)=0;
    virtual void handle_request_completed(void *cls,struct MHD_Connection *connection,
                                    void **con_cls, enum MHD_RequestTerminationCode toe)=0;

	static int send_page (struct MHD_Connection *connection,
           const char *page,
           int status_code);

    static bool findShortestAsFile(const string& dir, const string& file_id, string& full_file);


protected:
	string 		m_Version;
	string 		m_Method;
};

}
}
}