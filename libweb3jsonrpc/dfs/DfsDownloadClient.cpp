#include "DfsDownloadClient.h"
#include "DfsFileServer.h"
#include "DfsCommon.h"
#include "DfsConst.h"
#include "DfsMd5.h"


using namespace dev::rpc::fs;


DfsDownloadClient::DfsDownloadClient() : DfsHttpClient()
{

}

DfsDownloadClient::~DfsDownloadClient()
{

}

int DfsDownloadClient::fillUserData(JzRequestInfo& req_info)
{
	(void)req_info;
	return 0;
}

int DfsDownloadClient::endRequest()
{
	//close file
	if (m_ReqInfo.user_data)
	{
		fclose((FILE*)m_ReqInfo.user_data);
		//get file full path to store
		string strFile;
        string strFileBak;
        string strFileDir;
        string strFirst;
        string strSecond;
        DfsFileServer::getInstance()->createDirectoryStringByContainer(m_ReqInfo.container_id, strFileDir, strFirst, strSecond);
        createDfsFileNames(strFileDir, m_ReqInfo.file_id, m_ReqInfo.filename, strFile, strFileBak);

        dfs_debug << "** end of the download request, save temp file " << m_ReqInfo.filepath.data() << "to " << strFile.data();
		FileCpy(m_ReqInfo.filepath, strFile);
		FileRm(m_ReqInfo.filepath);

		std::string strHash;
		if(0 != ms_md5(strFile, strHash))
		{
			dfs_warn << "** cannot calculate md5 hash of file: " << strFile;
		}

		if (m_ReqInfo.filehash == strHash)
		{
			dfs_debug << "the calculate hash check success ";
			m_error_code = 0;
		}
		else
		{
			dfs_warn << "the calculate hash not equal, srcHash: " << m_ReqInfo.filehash.data() << ", dstHash: " << strHash.data();
			m_error_code = -1;
		}

		m_ReqInfo.user_data = NULL;
	}

	return 0;
}

size_t DfsDownloadClient::handle_recv_data(void *ptr, size_t size, size_t nmemb, void *cls)
{
	dfs_debug << "### recv data: " << (int)size << " bytes ******";
	if (cls == NULL)
		return -1;

	DfsHttpClient *pClient = (DfsHttpClient*)cls;
	if (!pClient->m_ReqInfo.curl)
	{
		dfs_debug << "NULL curl found\n";
		return -1;
	}

	CURLcode res;
    long response_code;
    res = curl_easy_getinfo(pClient->m_ReqInfo.curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (res != CURLE_OK || (response_code != 0 && response_code != 200) )
    {
    	dfs_debug << "bad response: " << (char*)ptr << "\n";
    	string strjson((char*)ptr, size*nmemb);
    	parseRspJson(strjson);
    	return size*nmemb;
    }
    
    //file recv
    if (!pClient->m_ReqInfo.user_data)
    {
        m_ReqInfo.filepath = DfsFileServer::getInstance()->m_TempStorePath;
        m_ReqInfo.filepath += "/";
        m_ReqInfo.filepath += m_ReqInfo.file_id;
        m_ReqInfo.filepath += "_";
        m_ReqInfo.filepath += m_ReqInfo.filename;

		FILE* fp = fopen(m_ReqInfo.filepath.c_str(), "wb");
		if (fp == NULL)
		{
			dfs_warn << "cannot open file for download : " << m_ReqInfo.filepath.c_str();
			return -1;
		}	

		pClient->m_ReqInfo.user_data = fp;
    }

	size_t written = fwrite(ptr, size, nmemb, (FILE*)pClient->m_ReqInfo.user_data);
	dfs_debug << "#####**** download, recv " << size << ", and write " << (int)written << " bytes to fileid: " << pClient->m_ReqInfo.file_id.data() << ", filename: " << pClient->m_ReqInfo.filename.data() << "\n";
	return written;
}

