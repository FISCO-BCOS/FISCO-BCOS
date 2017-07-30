#include "DfsDeleteFileHandler.h"
#include <microhttpd.h>
#include <stdio.h>
#include <string.h>
#include "DfsCommon.h"
#include "DfsConst.h"
#include "DfsJsonUtils.h"
#include "DfsFileServer.h"
#include "DfsFileOperationManager.h"
#include "DfsBase.h"
#include "DfsFileRecorder.h"


using namespace dev::rpc::fs;

void DfsDeleteFileHandler::handle_request_completed (void *cls, struct MHD_Connection *connection,
                   void **con_cls, enum MHD_RequestTerminationCode toe)
{
	(void)cls;
	(void)connection;
	(void)con_cls;
	(void)toe;
  	printf("the delete request of delete completed\n");
}

DfsDeleteFileHandler::DfsDeleteFileHandler(const string& version, const string &method) 
: IUrlHandler(version, method)
{

}

DfsDeleteFileHandler::~DfsDeleteFileHandler()
{

}

//only modify filename
int DfsDeleteFileHandler::handle(DfsHttpInfo* http_info, DfsUrlInfo* url_info)
{
	//PUT, container_id, file_id
	if (0 != strcmp(http_info->method, JZ_HTTP_METHOD_DELETE) \
		|| url_info->container_id.empty() \
		|| url_info->file_id.empty())
	{
		printf("bad request url: %s, method: %s\n", http_info->url, http_info->method);
		string strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_BAD_REQUEST, -1, "bad request, check url");
		return IUrlHandler::send_page(http_info->connection, strjson.c_str(), MHD_HTTP_BAD_REQUEST);
	}

	dfs_debug << "delete file, url: " << http_info->url << ", method: " << http_info->method << "\n";
	if (*(http_info->ptr) == NULL)
    {
    	DfsConnectionInfo *con_info = new DfsConnectionInfo;
    	con_info->method = JZ_HTTP_METHOD_POST;
    	con_info->version = JZ_MODULE_VERSION_V1;
    	*(http_info->ptr) = con_info;
    }

    //consume data
    *(http_info->upload_data_size) = 0;

	//process delete file
	int http_code = MHD_HTTP_OK;
	string strRspJson = "";
	if (0 != deleteFile(url_info))
	{
		http_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
		strRspJson = DfsJsonUtils::createCommonRsp(http_code, -1, "delete file failed, maybe not exist or not allowed!");
		return IUrlHandler::send_page(http_info->connection, strRspJson.data(), http_code);
	}
	
	http_code = MHD_HTTP_OK;
	strRspJson = DfsJsonUtils::createCommonRsp(http_code, 0, "OK");
	return IUrlHandler::send_page(http_info->connection, strRspJson.data(), http_code);
}


//revise the file info
int DfsDeleteFileHandler::deleteFile(const DfsUrlInfo* url_info)
{
	DfsFileInfo fileinfo;
	fileinfo.id = url_info->file_id;
	fileinfo.container = url_info->container_id;
	string strFilePath;
	if (0 != DfsFileOperationManager::getInstance()->delFile(fileinfo, strFilePath))
	{
		dfs_warn << "### failed in del file: " << url_info->file_id.data() << "\n";
		return -1;
	}
	
	dfs_debug << "del file: " << url_info->file_id.data() << "\n";
	//write modify file record
	string strUpDir;
	createDirectoryByContainer(DfsFileServer::getInstance()->m_StoreRootPath, url_info->container_id, strUpDir);	

	DfsFileTask objTask;
	objTask.id = url_info->file_id;
	objTask.filename = strFilePath.substr(strFilePath.find_last_of("/")+1);
	objTask.operation = DfsFileTask::DELETE;
	objTask.directory = strUpDir;
	if (0 != DfsFileRecorder::writeRecord(objTask))
	{
	   dfs_warn << "## write file update record failed !!";
	}

	return 0;
}