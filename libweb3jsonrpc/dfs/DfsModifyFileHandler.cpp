#include "DfsModifyFileHandler.h"
#include <microhttpd.h>
#include <string.h>
#include <stdio.h>
#include "DfsCommon.h"
#include "DfsConst.h"
#include "DfsJsonUtils.h"
#include "DfsFileOperationManager.h"
#include "DfsFileServer.h"
#include "DfsFileRecorder.h"


using namespace dev::rpc::fs;


void DfsModifyFileHandler::handle_request_completed(void *cls, \
												struct MHD_Connection *connection,
                   								void **con_cls, enum MHD_RequestTerminationCode toe)
{
    DfsConnectionInfo *con_info = (DfsConnectionInfo*)*con_cls;  
    (void)cls;
	(void)connection;
	(void)toe;
    if (NULL == con_info)
      return;  

    dfs_debug << "the the request completed, data_len: "<< con_info->length;  

    if (con_info->data)
    {
	  	dfs_debug << "**** the all recv json data: " << con_info->data;
	  	delete [](con_info->data);
	  	con_info->data = NULL;
	  	con_info->length = 0;
  	}

  	dfs_debug << "the modify request completed\n";
}

DfsModifyFileHandler::DfsModifyFileHandler(const string& version, const string &method)
: IUrlHandler(version, method)
{

}

DfsModifyFileHandler::~DfsModifyFileHandler()
{

}

//only modify filename
int DfsModifyFileHandler::handle(DfsHttpInfo* http_info, DfsUrlInfo* url_info)
{
	//PUT, container_id, file_id
	if (0 != strcmp(http_info->method, JZ_HTTP_METHOD_PUT) \
		|| url_info->container_id.empty() \
		|| url_info->file_id.empty())
	{
		dfs_warn << "bad request url: "<< http_info->url <<", method: " << http_info->method;
		string strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_BAD_REQUEST, -1, "bad request, check url");
		return IUrlHandler::send_page(http_info->connection, strjson.c_str(), MHD_HTTP_BAD_REQUEST);
	}

	DfsConnectionInfo *con_info = NULL;
	dfs_debug << "request url: "<< http_info->url <<", method: " << http_info->method;
	//file info json: {\"fileid\\":\"file_1111\",\"filename\":\"file_name_111\"}
	if (*(http_info->ptr) == NULL)//
	{
		con_info = new DfsConnectionInfo;
		con_info->method = JZ_HTTP_METHOD_PUT;
    	con_info->version = JZ_MODULE_VERSION_V1;

		if (*(http_info->upload_data_size) > 0)
		{
			con_info->length = *(http_info->upload_data_size);
			con_info->data = new char[(*(http_info->upload_data_size))];
			memcpy(con_info->data, http_info->upload_data, *(http_info->upload_data_size));
		}
		else
		{
			con_info->length = 0;
			con_info->data = NULL;
		}

		*(http_info->ptr) = con_info;
		return MHD_YES;
	}

	con_info = (DfsConnectionInfo*)*(http_info->ptr);
	//recv data and append to buffer
	if (*(http_info->upload_data_size) != 0)
	{
		char *pData = new char[(*(http_info->upload_data_size) + con_info->length)];
		memcpy(pData, con_info->data, con_info->length);
		memcpy(pData + con_info->length, http_info->upload_data, *(http_info->upload_data_size));
		
		con_info->length += *(http_info->upload_data_size);
		delete []con_info->data;
		con_info->data = pData;

		//consume data
    	*(http_info->upload_data_size) = 0;

		return MHD_YES;
	}
	else//recv done
	{
		dfs_debug << "recv request url: " <<http_info->url <<" total: " << con_info->length <<"bytes \n";

		//parse the json
		string strjson(con_info->data, con_info->length);
		DfsFileInfo objFileInfo;
		objFileInfo.container = url_info->container_id;
		string strRspJson;
		int http_code = MHD_HTTP_OK;
		if (0 != parseFileInfo(strjson, objFileInfo))
		{
			http_code = MHD_HTTP_BAD_REQUEST;
			strRspJson = DfsJsonUtils::createCommonRsp(http_code, -1, "bad input json cannot verify !");
			return IUrlHandler::send_page(http_info->connection, strRspJson.data(), http_code);
		}
		
		if(!(objFileInfo.id == url_info->file_id))
		{
			http_code = MHD_HTTP_BAD_REQUEST;
			strRspJson = DfsJsonUtils::createCommonRsp(http_code, -1, "input fileid not consistent!");
			return IUrlHandler::send_page(http_info->connection, strRspJson.data(), http_code);
		}

		//process rename file
		if (0 != reviseFileInfo(objFileInfo))
		{
			http_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
			strRspJson = DfsJsonUtils::createCommonRsp(http_code, -1, "input parameter not valid!");
			return IUrlHandler::send_page(http_info->connection, strRspJson.data(), http_code);
		}

		//write modify file record
		string strUpDir;
		createDirectoryByContainer(DfsFileServer::getInstance()->m_StoreRootPath, con_info->container, strUpDir);

		
		DfsFileTask objTask;
		objTask.id = con_info->file_id;
		objTask.filename = con_info->filename;
		objTask.filename += " => ";
		objTask.filename += objFileInfo.filename;
		objTask.operation = DfsFileTask::MODIFY;
		objTask.directory = strUpDir;
		if (0 != DfsFileRecorder::writeRecord(objTask))
		{
		   dfs_warn << "## write file update record failed !!";
		}

		dfs_debug << "## modify file has completed ! ";
		http_code = MHD_HTTP_OK;
		strRspJson = DfsJsonUtils::createCommonRsp(http_code, 0, "OK");
		return IUrlHandler::send_page(http_info->connection, strRspJson.data(), http_code);
	}
}

int DfsModifyFileHandler::parseFileInfo(const string& json, DfsFileInfo& fileInfo)
{
	//file info json: {\"fileid\\":\"file_1111\",\"filename\":\"file_name_111\"}
	dfs_debug << "input json: " << json.c_str();
	Json::Reader reader;
	Json::Value objValue;
	if (json.size() <= 0 || !reader.parse(json, objValue))
	{
		dfs_warn << "bad parse json: " << json.c_str();
		return -1;
	}

	if (!objValue["fileid"].isNull() && !objValue["filename"].isNull())
	{
		fileInfo.id = objValue["fileid"].asString();
		fileInfo.filename = objValue["filename"].asString();
	}
	else
	{
		dfs_warn << "bad parse json: " << json.c_str() << "not complete\n";
		return -1;
	}

	return 0;
}

//revise the file info
int DfsModifyFileHandler::reviseFileInfo(const DfsFileInfo& fileInfo)
{
	return DfsFileOperationManager::getInstance()->modifyFile(fileInfo);
}

