/** 
**@file 文件处理服务模块
*/

#pragma once

#include <string>
#include <map>
#include <vector>
#include <pthread.h>
#include "microhttpd.h"
#include "IUrlHandler.h"
#include "DfsContractCaller.h"
#include "DfsFileInfoScanner.h"



using std::string;
using std::map;
using std::vector;


namespace dev
{

namespace rpc
{

namespace fs
{

class DfsFileServer
{
private:
	DfsFileServer();
	~DfsFileServer();

public:
	static DfsFileServer* getInstance();

	
public:
	//initiate file storage repository tree
	int init(const string& store_root, const string& group, const string& node, dev::eth::Client* ethClient);

	//destory
	void destory();

	//filter ilegal file process request
	int filter(const string &url, const string &method);

	//handle upload, download, revise file, delete file
	int handle(DfsHttpInfo* http_info);//http_info有内部handler释放

	//create directory string by container id
	void createDirectoryStringByContainer(const string &container, string &directory, string& first, string& second);


	static void request_completed (void *cls,
                   struct MHD_Connection *connection,
                   void **con_cls,
                   enum MHD_RequestTerminationCode toe);

	bool isInited() const{return m_Inited;};

	//check if service node avaiable
	bool checkServiceAvailable();

private:
	//register request handler, module now is : "fs"
	int registerHandler(const string& version, const string& method, IUrlHandler* handler);

	//find the specfic handler
	IUrlHandler* getHandler(const string& version, const string& method);

	//parse url
	int parserURL(const string& url, DfsUrlInfo& url_info);

	//iniatate the storage
	int init_storage(const string& store_root);


	
public:
	string 										m_StoreRootPath;
	string 										m_TempStorePath;
	string 										m_NodeGroup;
	string 										m_NodeId;
	DfsFileInfoScanner						m_DfsFileInfoScanner;
	vector<string>								m_Containers;
	
private:
	map<string, map<string, IUrlHandler*> >		m_HandlerMap;//<version, <method, handler> >	
	bool 										m_Inited;
	
};

}
}
}