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
	jsonrpc::HttpServer* server;
	int code;
};

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

int SafeHttpServer::callback(void *cls, struct MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls) {
	(void)version;
	std::string account = "";
	string strUrl(url);
	string strMethod(method);

	bool isFileProcess = DfsFileServer::getInstance()->filter(strUrl, strMethod) == 0;
	//huanggaofeng: filter file process request
	if (isFileProcess)
	{
		DfsHttpInfo objHttpInfo;
		objHttpInfo.cls = cls;
		objHttpInfo.connection = connection;
		objHttpInfo.url = (char*)url;
		objHttpInfo.method = (char*)method;
		objHttpInfo.version = (char*)version;
		objHttpInfo.upload_data = (char*)upload_data;
		objHttpInfo.upload_data_size = upload_data_size;
		objHttpInfo.ptr = con_cls;
		int ret = DfsFileServer::getInstance()->handle(&objHttpInfo);
		*upload_data_size = 0;
		return ret;
	}
	return HttpServer::callback(cls, connection, url, method, version, upload_data, upload_data_size, con_cls);
}

bool SafeHttpServer::StartListening() {
	if (!isRunning()) {
		if (0 != DfsFileServer::getInstance()->init(m_DfsStoragePath, m_DfsNodeGroupId, m_DfsNodeId, m_eth))
		{
			LOG(INFO) << "init DfsFileServer failed !";
			//return false;
		}
	}
	return HttpServer::StartListening();
}
bool SafeHttpServer::StopListening() {
	if (isRunning()) {
		DfsFileServer::getInstance()->destory();
	}
	return HttpServer::StopListening();
}