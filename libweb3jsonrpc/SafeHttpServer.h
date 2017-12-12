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
#include "libstatistics/RateLimitHttpServer.h"
#include <libethereum/Client.h>
#include <libdevcore/easylog.h>
#include "dfs/DfsFileServer.h"

namespace dev
{

class SafeHttpServer: public jsonrpc::HttpServer
{
public:
	/// "using HttpServer" won't work with msvc 2013, so we need to copy'n'paste constructor
	SafeHttpServer(int _port, std::string const& _sslcert = std::string(), std::string const& _sslkey = std::string(), int _threads = 50, std::string const& _config = std::string()):
		HttpServer(_port, _sslcert, _sslkey, _threads, _config) {}

	/// override HttpServer implementation
	bool virtual SendResponse(std::string const& _response, void* _addInfo = nullptr) override;
	bool virtual SendOptionsResponse(void* _addInfo) override;

	void setAllowedOrigin(std::string const& _origin) { m_allowedOrigin = _origin; }
	std::string const& allowedOrigin() const { return m_allowedOrigin; }

	void setGroup(const std::string& group) { m_DfsNodeGroupId = group; }
	void setNode(const std::string& node) {m_DfsNodeId = node;}
	void setStoragePath(const std::string& storage) {m_DfsStoragePath = storage;}
	void setEth(eth::Client* _eth) {m_eth = _eth;}

	virtual bool StartListening();
	virtual bool StopListening();

	static int callback(void *cls, struct MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls);

	virtual pCallBack getCallback() {
        return SafeHttpServer::callback;
    }
	virtual pCompletedCallbak getCompletedCallback() {
        return dev::rpc::fs::DfsFileServer::request_completed;
    }
private:
	std::string m_allowedOrigin;

	std::string m_path_sslrootca;
	std::string m_sslrootca;
	eth::Client* m_eth;
	std::string m_DfsNodeGroupId;
	std::string m_DfsNodeId;
	std::string m_DfsStoragePath;
};

}
