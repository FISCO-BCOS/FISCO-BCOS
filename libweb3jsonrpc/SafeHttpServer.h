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

#include <map>
#include <string>
#include <microhttpd.h>
#include <libethereum/Client.h>
#include <jsonrpccpp/server/abstractserverconnector.h>

namespace dev
{
/**
 * This class provides an embedded HTTP Server, based on libmicrohttpd, to handle incoming Requests and send HTTP 1.1
 * valid responses.
 * Note that this class will always send HTTP-Status 200, even though an JSON-RPC Error might have occurred. Please
 * always check for the JSON-RPC Error Header.
 */
class TrustHttpServer: public jsonrpc::AbstractServerConnector
{
public:

	/**
	 * @brief TrustHttpServer, constructor for the included TrustHttpServer
	 * @param port on which the server is listening
	 * @param enableSpecification - defines if the specification is returned in case of a GET request
	 * @param sslcert - defines the path to a SSL certificate, if this path is != "", then SSL/HTTPS is used with the given certificate.
	 */
	TrustHttpServer(eth::Client* _eth, int _port, const std::string& _sslrootca = "", const std::string& _sslcert = "", const std::string& _sslkey = "", int _threads = 50);

	virtual bool StartListening();
	virtual bool StopListening();

	bool virtual SendResponse(std::string const& _response, void* _addInfo = NULL) ;
	bool virtual SendOptionsResponse(void* _addInfo);

	void SetUrlHandler(const std::string &_url, jsonrpc::IClientConnectionHandler *_handler);

	void setGroup(const std::string& group);
	void setNode(const std::string& node);
	void setStoragePath(const std::string& storage);

private:
	int m_port;
	int m_threads;
	bool m_running;
	std::string m_path_sslcert;
	std::string m_path_sslkey;
	std::string m_path_sslrootca;
	std::string m_sslcert;
	std::string m_sslkey;
	std::string m_sslrootca;
	eth::Client* m_eth;

	struct MHD_Daemon *daemon;

	std::map<std::string, jsonrpc::IClientConnectionHandler*> m_urlhandler;

	static int callback(void *_cls, struct MHD_Connection *_connection, const char *_url, const char *_method, const char *_version, const char *_upload_data, size_t *_upload_data_size, void **_con_cls);

	jsonrpc::IClientConnectionHandler* GetHandler(const std::string &_url);

protected:
	std::string m_DfsNodeGroupId;
	std::string m_DfsNodeId;
	std::string m_DfsStoragePath;

};

class SafeHttpServer: public TrustHttpServer
{
public:
	/// "using HttpServer" won't work with msvc 2013, so we need to copy'n'paste constructor
	SafeHttpServer(eth::Client* _eth, int _port, std::string const& _sslrootca = std::string(), std::string const& _sslcert = std::string(), std::string const& _sslkey = std::string(), int _threads = 50):
		TrustHttpServer(_eth, _port, _sslrootca, _sslcert, _sslkey, _threads) {}

	/// override HttpServer implementation
	bool virtual SendResponse(std::string const& _response, void* _addInfo = nullptr) override;
	bool virtual SendOptionsResponse(void* _addInfo) override;

	void setAllowedOrigin(std::string const& _origin) { m_allowedOrigin = _origin; }
	std::string const& allowedOrigin() const { return m_allowedOrigin; }

private:
	std::string m_allowedOrigin;
};

}
