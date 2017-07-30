#include "DfsDownloadHandler.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <microhttpd.h>
#include <unistd.h>

#include "DfsJsonUtils.h"
#include "DfsCommon.h"
#include "DfsConst.h"
#include "DfsFileServer.h"
#include "DfsFileOperationManager.h"
#include "DfsContractCaller.h"



using namespace dev::rpc::fs;



void DfsDownloadHandler::handle_request_completed (void *cls, struct MHD_Connection *connection,
                   void **con_cls, enum MHD_RequestTerminationCode toe)
{
    (void)cls;
    (void)connection;
    (void)con_cls;
    (void)toe;
  	dfs_debug << "download request of download completed \n";
}


DfsDownloadHandler::DfsDownloadHandler(const string &version, const string &method) :
    IUrlHandler(version, method)
{
}

DfsDownloadHandler::~DfsDownloadHandler()
{
}


static ssize_t
file_reader (void *cls,
             uint64_t pos,
             char *buf,
             size_t max);


static void free_callback (void *cls);


//handle request and setup download
int DfsDownloadHandler::handle(DfsHttpInfo *http_info, DfsUrlInfo *url_info)
{
    struct stat buf;

    dfs_debug << "the url: " << http_info->url << "\n";
    if ((0 != strcmp(http_info->method, MHD_HTTP_METHOD_GET)) 
    	|| url_info->container_id.empty() \
		|| url_info->file_id.empty())
    {
        dfs_warn << "bad request method: " << http_info->method << "\n";
    	string strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_BAD_REQUEST, -1, "bad request, check url");
		return IUrlHandler::send_page(http_info->connection, strjson.c_str(), MHD_HTTP_BAD_REQUEST);
    }   
    
    if (*(http_info->ptr) == NULL)
    {
    	DfsConnectionInfo *con_info = new DfsConnectionInfo;
    	con_info->method = JZ_HTTP_METHOD_GET;
    	con_info->version = JZ_MODULE_VERSION_V1;
		con_info->fp = NULL;
		con_info->fp_path = "";
		con_info->data = (char*)http_info->connection;
    	*(http_info->ptr) = con_info;
    }

	DfsConnectionInfo *down_con_info = (DfsConnectionInfo*)(*(http_info->ptr));
	int http_response_code = MHD_HTTP_OK;
	if (NULL == down_con_info->fp)
	{
		string strFileDir;
	    string strFirst;
	    string strSecond;
	    DfsFileServer::getInstance()->createDirectoryStringByContainer(url_info->container_id, strFileDir, strFirst, strSecond);

	    if (url_info->file_id.empty())
	    {
	    	dfs_warn << "bad request, url: " << http_info->url << " without file_id\n";
	    	string strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_BAD_REQUEST, -1, "bad request no fileid");
			return IUrlHandler::send_page(http_info->connection, strjson.c_str(), MHD_HTTP_BAD_REQUEST);
	    }
		
	    //get short fileid_xxx as the filepath
	    //find the file
		string strFullFileFound = "";
		if (!findShortestAsFile(strFileDir, url_info->file_id, strFullFileFound))
		{
			dfs_warn << "download, cannot find fileid: " << url_info->file_id.c_str() << " corresponding file \n";
			string strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_NOT_FOUND, -1, "file id not found");
			return IUrlHandler::send_page(http_info->connection, strjson.c_str(), MHD_HTTP_NOT_FOUND);
		}
	
    	down_con_info->fp = fopen (strFullFileFound.c_str(), "rb");
		if (down_con_info->fp == NULL)
		{
			http_response_code = MHD_HTTP_NOT_FOUND;
			
			dfs_warn << "**** open file failed! file: " << strFullFileFound.data();
			string strPage = DfsJsonUtils::createCommonRsp(MHD_HTTP_NOT_FOUND, -1, "file not found");
			return IUrlHandler::send_page(http_info->connection, strPage.data(), http_response_code);
		}
		else
		{
	        int fd = fileno (down_con_info->fp);
			int result = 0;
	        if (-1 == fd)
	        {
	        	result = -1;
	        }

			if ((result == 0) && ((0 != fstat (fd, &buf)) || (! S_ISREG (buf.st_mode))) )
	        {
	           result = -1;
	        }

			if (result != 0)
			{
				(void) fclose (down_con_info->fp);
				down_con_info->fp = NULL;
				http_response_code = MHD_HTTP_BAD_REQUEST;
			
				dfs_warn << "**** bad file cannot down, file: " << strFullFileFound.data();
				string strPage = DfsJsonUtils::createCommonRsp(MHD_HTTP_BAD_REQUEST, -1, "bad file");
				return IUrlHandler::send_page(http_info->connection, strPage.data(), http_response_code);
			}
		}
    }

	int ret = MHD_YES;
	struct MHD_Response *response;
	//do downloading...
	response = MHD_create_response_from_callback (buf.st_size, 4 * 1024,     /* 32k page size */
                   &file_reader,
                   down_con_info,
                   &free_callback);
    if (NULL == response)
    {
        fclose (down_con_info->fp);
		down_con_info->fp = NULL;
		http_response_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
			
		dfs_warn << "**** download file, MHD_create_response_from_callback, url: " << http_info->url;
		string strPage = DfsJsonUtils::createCommonRsp(http_response_code, -1, "create reader failed");
		return IUrlHandler::send_page(http_info->connection, strPage.data(), http_response_code);
    }
	
	dfs_debug << "download request commit ok! url: " << http_info->url << "\n";
	ret = MHD_queue_response(http_info->connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

static ssize_t
file_reader (void *cls,
             uint64_t pos,
             char *buf,
             size_t max)
{
	DfsConnectionInfo *down_con_info = (DfsConnectionInfo*)cls;

    FILE *file = (FILE*)down_con_info->fp;

    fseek (file, pos, SEEK_SET);
    int ret = fread (buf, 1, max, file);
    dfs_debug << "download, read " << ret << " bytes from file\n";
    return ret;
}


static void
free_callback (void *cls)
{
    DfsConnectionInfo *down_con_info = (DfsConnectionInfo*)cls;

    FILE *file = (FILE*)down_con_info->fp;
	if (file)
    {	
	    fclose (down_con_info->fp);
		down_con_info->fp = NULL;			
		dfs_debug << "**** download down and free, url: " << down_con_info->file_id.data();
		down_con_info->data = NULL;
		down_con_info->fp_path = "";
	}
	
    dfs_debug << "file read done, fileid: " << down_con_info->file_id.data() << "\n";
}

