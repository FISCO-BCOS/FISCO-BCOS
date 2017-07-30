#include "DfsCommonClient.h"
#include "DfsFileServer.h"
#include "DfsCommon.h"
#include "DfsConst.h"


using namespace dev::rpc::fs;



DfsCommonClient::DfsCommonClient() : DfsHttpClient()
{

}

DfsCommonClient::~DfsCommonClient()
{

}

int DfsCommonClient::fillUserData(JzRequestInfo& req_info)
{
	JzBuffer *pBuffer = new JzBuffer();
	pBuffer->data = (char*)new char[JZ_FIX_BUFFER_SIZE];
	pBuffer->compacity = JZ_FIX_BUFFER_SIZE;
	pBuffer->size = 0;
	req_info.user_data = pBuffer;

	return 0;
}

int DfsCommonClient::endRequest()
{
	if (m_ReqInfo.user_data)
	{
		//parse json
		JzBuffer* pBuffer = (JzBuffer*)m_ReqInfo.user_data;
		string strjson(pBuffer->data, pBuffer->size);
		parseRspJson(strjson);

		//free user data
		delete []pBuffer->data;
		delete pBuffer;
		m_ReqInfo.user_data = NULL;
	}

	return 0;
}


size_t DfsCommonClient::handle_recv_data(void *ptr, size_t size, size_t nmemb, void *cls)
{
	if (cls == NULL)
		return -1;

	DfsHttpClient *pClient = (DfsHttpClient*)cls;
	if (!pClient->m_ReqInfo.curl)
	{
		dfs_warn << "NULL curl found\n";
		return -1;
	}

	CURLcode res;
    long response_code;
    res = curl_easy_getinfo(pClient->m_ReqInfo.curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (res != CURLE_OK || (response_code != 0 && response_code != 200) )
    {
    	dfs_warn << "bad response: " << (char*)ptr << "\n";
    	string strjson((char*)ptr, size*nmemb);
    	parseRspJson(strjson);
    	return size*nmemb;
    }
    
    //content recv
    JzBuffer* pBuff = (JzBuffer*)pClient->m_ReqInfo.user_data;
    if (pBuff == NULL || !pBuff->data)
    {
    	dfs_warn << "null pointer for recv buffer\n";
        return -1;
    }

    if ((int)(pBuff->compacity - pBuff->size - nmemb*size) < 0)
    {
        int compacity = pBuff->compacity + nmemb*size + 1;
        char *pData = (char*)malloc(compacity);

        memcpy(pData, pBuff->data, pBuff->size);
        memset(pData + pBuff->size, 0, pBuff->compacity - pBuff->size);

        free(pBuff->data);
        pBuff->data = pData;
        pBuff->compacity = compacity;
    }
    
    memcpy(pBuff->data + pBuff->size, ptr, nmemb*size);
    pBuff->size = pBuff->size + nmemb * size;
    return size*nmemb;
}

