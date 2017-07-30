#pragma once

#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <string>


using std::string;

namespace dev
{

namespace rpc
{

namespace fs
{

//eg. size_t write_data(void *ptr, size_t size, size_t nmemb, void *cls)
typedef size_t (*Callback_write_data)(void*, size_t, size_t, void *cls);

typedef struct JzBuffer{
    int compacity;
    int size;
    char* data;
} JzBuffer;

typedef struct JzRequestInfo {
    string  method;
	string  module;//must be "fs" now
	string 	version;
	string 	container_id;
	string 	file_id;
	string 	filename;
	string	filepath;
	string	filehash;
	string 	host;//host:port
	int 	port;
	void 	*user_data;
	CURL 	*curl;
	JzRequestInfo(){
		filepath = "";
		filehash = "";
		method = "";
		module = "fs";
		version = "v1";
		container_id = "files";
		file_id = "";
		filename = "";
		host = "";
		port = 0;	
		user_data = NULL;
		curl = NULL;
	}
} JzRequestInfo;


class DfsHttpClient
{
public:
	DfsHttpClient();
	virtual ~DfsHttpClient();


public:
	//init the curl components
	void init(const JzRequestInfo& req_info);

	//fill upload data and request to URL
	int request(const string& upload_data);

	int getRespCode() const {return m_resp_code;};
	int getErrorCode() const {return m_error_code;};

	const string& getMethod() const {return m_ReqInfo.method;}

	int parseRspJson(const string& json);

public:
	virtual int fillUserData(JzRequestInfo& req_info)=0;
	virtual int endRequest()=0;
	virtual size_t handle_recv_data(void *ptr, size_t size, size_t nmemb, void *cls)=0;

protected:
	bool					m_Inited;

	int 					m_resp_code;
	int                     m_error_code;
	string 					m_strError;


public:
	JzRequestInfo			m_ReqInfo;

};

}
}
}