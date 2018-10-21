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
/** @file SafeHttpServer.h
 * @authors:
 *   Marek
 * @date 2015
 */
#pragma once

#include <string>
#include <map>
#include <microhttpd.h>
#include "jsonrpccpp/server/abstractserverconnector.h"

namespace dev
{

class SafeHttpServer: public jsonrpc::AbstractServerConnector
{
	typedef int (*pCallBack)(void *, struct MHD_Connection *, const char *, const char *, const char *, const char *, size_t *, void **);

public:
	/// "using HttpServer" won't work with msvc 2013, so we need to copy'n'paste constructor
	SafeHttpServer(std::string const& _address, int _port, std::string const& _sslcert = std::string(), std::string const& _sslkey = std::string(), int _threads = 50);
	virtual ~SafeHttpServer() {
	}

	virtual bool StartListening();
	virtual bool StopListening();
	virtual bool SendResponse(std::string const& _response, void* _addInfo = nullptr);
	virtual bool SendOptionsResponse(void* _addInfo);

	void SetUrlHandler(const std::string &url, jsonrpc::IClientConnectionHandler *handler);
	void setAllowedOrigin(std::string const& _origin) { m_allowedOrigin = _origin; }
	std::string const& allowedOrigin() const { return m_allowedOrigin; }
	virtual pCallBack getCallback() {
        return SafeHttpServer::callback;
    }

private:
    int port;
    int threads;
    bool running;
    std::string path_sslcert;
    std::string path_sslkey;
    std::string sslcert;
    std::string sslkey;

    struct MHD_Daemon* daemon;

    std::map<std::string, jsonrpc::IClientConnectionHandler*> urlhandler;

    static int callback(void* cls, struct MHD_Connection* connection, const char* url,
        const char* method, const char* version, const char* upload_data, size_t* upload_data_size,
        void** con_cls);

    jsonrpc::IClientConnectionHandler* GetHandler(const std::string& url);

    std::string m_allowedOrigin;
    std::string m_address;
};

}
