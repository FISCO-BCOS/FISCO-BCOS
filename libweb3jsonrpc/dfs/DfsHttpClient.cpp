#include "DfsHttpClient.h"
#include <stdio.h>
#include <json/json.h>
#include "DfsBase.h"


using namespace dev::rpc::fs;


DfsHttpClient::DfsHttpClient()
{
	m_Inited = false;
	m_resp_code = 404;
	m_error_code = -1;
	m_strError = "";
}

DfsHttpClient::~DfsHttpClient()
{

}

static size_t recv_response(void *ptr, size_t size, size_t nmemb, void *cls)
{
	if (cls == NULL)
		return -1;

	DfsHttpClient *pClient = (DfsHttpClient*)cls;
	return pClient->handle_recv_data(ptr, size, nmemb, cls);
}

//init the curl components
void DfsHttpClient::init(const JzRequestInfo& req_info)
{
	m_Inited = true;
	m_ReqInfo = req_info;
}

//fill upload data and request to URL
int DfsHttpClient::request(const string& upload_data)
{
	if (m_ReqInfo.host.empty() || !m_Inited)
		return -1;

	string strUrl = m_ReqInfo.host;
	char szUrl[256];
	memset(szUrl, 0, sizeof(szUrl));
	snprintf(szUrl, sizeof(szUrl), ":%d/fs/%s/files/%s", m_ReqInfo.port, m_ReqInfo.version.c_str(), m_ReqInfo.file_id.c_str());
	strUrl += szUrl;
	
	CURL *curl;
    CURLcode res;
    
	curl = curl_easy_init();
    if (!curl)
    {
    	dfs_debug << "curl_easy_init() failed\n";
    	return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, strUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, m_ReqInfo.method.c_str());

	dfs_debug << "###1 do http client request: " << strUrl.data() << ", method: " << m_ReqInfo.method.c_str() << " start ###";
    //for reading data from response
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, recv_response);
    m_ReqInfo.curl = curl;
    fillUserData(m_ReqInfo);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);

    //for sending data to server
    if (!upload_data.empty())
    {
    	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, upload_data.data());
    	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, upload_data.size());
    }
    
    //do request
    res = curl_easy_perform(curl);
    if(res != CURLE_OK)
    {
    	dfs_debug << "URL: " << strUrl.data() << ", curl_easy_perform() failed: " << curl_easy_strerror(res);
    	curl_easy_cleanup(curl);
    	return -1;
    }
    
    curl_easy_cleanup(curl);
	dfs_debug << "###2 do http client request: " << strUrl.data() << ", method: " << m_ReqInfo.method.c_str() << " end ###";
    return 0;
}


int DfsHttpClient::parseRspJson(const string& json)
{
	//file info json: {\"fileid\\":\"file_1111\",\"filename\":\"file_name_111\"}
	printf("input json: %s\n", json.c_str());
	Json::Reader reader;
	Json::Value objValue;
	if ( (json.size() <= 0) || !(reader.parse(json, objValue)) )
	{
		dfs_warn << "parse json: " << json.c_str() <<"failed\n";
		return -1;
	}

	if ( (!objValue["ret"].isNull()) && (!objValue["code"].isNull()) && (!objValue["info"].isNull()) )
	{
		m_strError = objValue["info"].asString();
		m_resp_code = objValue["ret"].asInt();
		m_error_code = objValue["code"].asInt();
	}
	else
	{
		dfs_warn << "bad json: " << json.c_str() <<"not complete\n";
		m_resp_code = -1;
		m_error_code = -1;
		m_strError = "";
		return -1;
	}

	return 0;
}
