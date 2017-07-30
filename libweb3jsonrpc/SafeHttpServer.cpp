/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file SafeHttpServer.cpp
 * @authors:
 *   Marek
 * @date 2015
 */

#include <microhttpd.h>
#include <sstream>
#include <jsonrpccpp/common/specificationparser.h>
#include <libdevcore/Log.h>
#include "SafeHttpServer.h"
#include "DfsFileServer.h"


using namespace std;
using namespace dev;
using namespace dev::rpc::fs;


/// structure copied from libjson-rpc-cpp httpserver version 0.6.0
struct mhd_coninfo
{
	struct MHD_PostProcessor *postprocessor;
	MHD_Connection* connection;
	stringstream request;
	TrustHttpServer* server;
	int code;
};

TrustHttpServer::TrustHttpServer(eth::Client* _eth, int _port, const std::string& _sslrootca, const std::string& _sslcert, const std::string& _sslkey, int _threads) :
    AbstractServerConnector(),
    m_port(_port),
    m_threads(_threads),
    m_running(false),
    m_path_sslcert(_sslcert),
    m_path_sslkey(_sslkey),
    m_path_sslrootca(_sslrootca),
    m_eth(_eth),
    daemon(NULL),
    m_DfsNodeGroupId(""),
    m_DfsNodeId(""),
    m_DfsStoragePath("")
{
}

jsonrpc::IClientConnectionHandler *TrustHttpServer::GetHandler(const std::string &_url)
{
    if (AbstractServerConnector::GetHandler() != NULL)
        return AbstractServerConnector::GetHandler();
    map<string, jsonrpc::IClientConnectionHandler*>::iterator it = this->m_urlhandler.find(_url);
    if (it != this->m_urlhandler.end())
        return it->second;
    return NULL;
}

bool TrustHttpServer::StartListening()
{
    if(!this->m_running)
    {
		//start the fileserver
      	if (0 != DfsFileServer::getInstance()->init(m_DfsStoragePath, m_DfsNodeGroupId, m_DfsNodeId, m_eth))
        {
          LOG(ERROR) << "init DfsFileServer failed !";
          //return false;
        }
		
        if (this->m_path_sslcert != "" && this->m_path_sslkey != "" && this->m_path_sslrootca != "")
        {
        	LOG(TRACE) << "*****#### HTTPS / SSL start, but not support ###******";
        }
        
        LOG(TRACE) << "*****#### HTTP start ###******";
        this->daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, this->m_port, NULL, NULL, \
				TrustHttpServer::callback, this, \
				MHD_OPTION_THREAD_POOL_SIZE, this->m_threads, \
				MHD_OPTION_NOTIFY_COMPLETED, DfsFileServer::request_completed, NULL, \
				MHD_OPTION_END);
        if (this->daemon != NULL)
            this->m_running = true;
    }

    return this->m_running;
}

bool TrustHttpServer::StopListening()
{
    if(this->m_running)
    {
        MHD_stop_daemon(this->daemon);
		DfsFileServer::getInstance()->destory();
        this->m_running = false;
    }
	
    return true;
}

bool TrustHttpServer::SendResponse(const string& _response, void* _addInfo)
{
    struct mhd_coninfo* client_connection = static_cast<struct mhd_coninfo*>(_addInfo);
    struct MHD_Response *result = MHD_create_response_from_buffer(_response.size(),(void *) _response.c_str(), MHD_RESPMEM_MUST_COPY);

    MHD_add_response_header(result, "Content-Type", "application/json");
    MHD_add_response_header(result, "Access-Control-Allow-Origin", "*");

    int ret = MHD_queue_response(client_connection->connection, client_connection->code, result);
    MHD_destroy_response(result);
    return ret == MHD_YES;
}

bool TrustHttpServer::SendOptionsResponse(void* _addInfo)
{
    struct mhd_coninfo* client_connection = static_cast<struct mhd_coninfo*>(_addInfo);
    struct MHD_Response *result = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_MUST_COPY);

    MHD_add_response_header(result, "Allow", "POST, OPTIONS");
    MHD_add_response_header(result, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(result, "Access-Control-Allow-Headers", "origin, content-type, accept");
    MHD_add_response_header(result, "DAV", "1");

    int ret = MHD_queue_response(client_connection->connection, client_connection->code, result);
    MHD_destroy_response(result);
    return ret == MHD_YES;
}

void TrustHttpServer::SetUrlHandler(const string &_url, jsonrpc::IClientConnectionHandler *_handler)
{
    this->m_urlhandler[_url] = _handler;
    this->SetHandler(NULL);
}

void TrustHttpServer::setGroup(const std::string& group)
{
	m_DfsNodeGroupId = group;
}

void TrustHttpServer::setNode(const std::string& node)
{
	m_DfsNodeId = node;
}

void TrustHttpServer::setStoragePath(const std::string& storage)
{
	m_DfsStoragePath = storage;
}

int TrustHttpServer::callback(void *_cls, MHD_Connection *_connection, const char *_url, const char *_method, const char *_version, const char *_upload_data, size_t *_upload_data_size, void **_con_cls)
{
    (void)_version;
	std::string account = "";
	string strUrl(_url);
	string strMethod(_method);
	
	bool isFileProcess = DfsFileServer::getInstance()->filter(strUrl, strMethod) == 0;
	//huanggaofeng: filter file process request
	if (isFileProcess)
    {
		DfsHttpInfo objHttpInfo;
		objHttpInfo.cls = _cls;
		objHttpInfo.connection = _connection;
		objHttpInfo.url = (char*)_url;
		objHttpInfo.method = (char*)_method;
		objHttpInfo.version = (char*)_version;
		objHttpInfo.upload_data = (char*)_upload_data;
		objHttpInfo.upload_data_size = _upload_data_size;
		objHttpInfo.ptr = _con_cls;
		int ret = DfsFileServer::getInstance()->handle(&objHttpInfo);
		*_upload_data_size = 0;
		return ret;
	}

	//not file process http request
    if (*_con_cls == NULL)//request first comes to here
    {
        struct mhd_coninfo* client_connection = new mhd_coninfo;
        client_connection->connection = _connection;
        client_connection->server = static_cast<TrustHttpServer*>(_cls);
        *_con_cls = client_connection;
		
        return MHD_YES;
    }
    struct mhd_coninfo* client_connection = static_cast<struct mhd_coninfo*>(*_con_cls);

    if (string("POST") == _method)
    {
        if (*_upload_data_size != 0)
        {
            client_connection->request.write(_upload_data, *_upload_data_size);
            *_upload_data_size = 0;
            return MHD_YES;
        }
        else
        {
            string response;
            jsonrpc::IClientConnectionHandler* handler = client_connection->server->GetHandler(string(_url));
            if (handler == NULL)
            {
	            //debug print
				LOG(TRACE) << "***** http get null handler *******";
                client_connection->code = MHD_HTTP_INTERNAL_SERVER_ERROR;
                client_connection->server->SendResponse("No client conneciton handler found", client_connection);
            }
            else
            {
            	//debug print
				//LOG(TRACE) << "***** http get handler and response *******";
                client_connection->code = MHD_HTTP_OK;
                handler->HandleRequest(client_connection->request.str(), response);
                client_connection->server->SendResponse(response, client_connection);
            }
        }
    }
	else if (string("OPTIONS") == _method) {
		//debug print
		//LOG(TRACE) << "***** http option process  ******";
        client_connection->code = MHD_HTTP_OK;
        client_connection->server->SendOptionsResponse(client_connection);
	}
    else
    {
		//debug print
		//LOG(TRACE) << "***** http else method: " << _method << "  *******";
		client_connection->code = MHD_HTTP_METHOD_NOT_ALLOWED;
        client_connection->server->SendResponse("Not allowed HTTP Method", client_connection);
    }
    delete client_connection;
    *_con_cls = NULL;

    return MHD_YES;
}


bool SafeHttpServer::SendResponse(string const& _response, void* _addInfo)
{
	struct mhd_coninfo* client_connection = static_cast<struct mhd_coninfo*>(_addInfo);
	struct MHD_Response *result = MHD_create_response_from_buffer(
		_response.size(),
		static_cast<void *>(const_cast<char *>(_response.c_str())),
		MHD_RESPMEM_MUST_COPY
	);

	MHD_add_response_header(result, "Content-Type", "application/json");
	MHD_add_response_header(result, "Access-Control-Allow-Origin", m_allowedOrigin.c_str());

	int ret = MHD_queue_response(client_connection->connection, client_connection->code, result);
	MHD_destroy_response(result);
	return ret == MHD_YES;
}

bool SafeHttpServer::SendOptionsResponse(void* _addInfo)
{
	struct mhd_coninfo* client_connection = static_cast<struct mhd_coninfo*>(_addInfo);
	struct MHD_Response *result = MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_MUST_COPY);

	MHD_add_response_header(result, "Allow", "POST, OPTIONS");
	MHD_add_response_header(result, "Access-Control-Allow-Origin", m_allowedOrigin.c_str());
	MHD_add_response_header(result, "Access-Control-Allow-Headers", "origin, content-type, accept");
	MHD_add_response_header(result, "DAV", "1");

	int ret = MHD_queue_response(client_connection->connection, client_connection->code, result);
	MHD_destroy_response(result);
	return ret == MHD_YES;
}
