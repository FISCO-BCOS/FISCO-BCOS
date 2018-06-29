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
/**
 * @file: ChannelServer.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include <boost/asio.hpp>
#include <string>
#include <thread>
#include <queue>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <boost/asio/ssl.hpp>
#include <libdevcore/FixedHash.h>

#include "ChannelException.h"
#include "ChannelSession.h"
#include "ThreadPool.h"

namespace dev
{

namespace channel {

class ChannelServer: public std::enable_shared_from_this<ChannelServer> {
public:
	void run();

	void onAccept(const boost::system::error_code& error, ChannelSession::Ptr session);

	void startAccept();

	//Session::Ptr connect(std::string host, int port) throw(Exception);
	//void asyncConnect(std::string host, int port, std::function<void(Exception, Session::Ptr)> callback);

	void setBind(const std::string &host, int port) { _listenHost = host; _listenPort = port; };

	void setEnableSSL(bool enableSSL) { _enableSSL = enableSSL; };

	void setConnectionHandler(std::function<void(dev::channel::ChannelException, ChannelSession::Ptr)> handler) { _connectionHandler = handler; };

	void setIOService(std::shared_ptr<boost::asio::io_service> ioService) { _ioService = ioService; };
	void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext) { _sslContext = sslContext; };

	void stop();

private:
	void onHandshake(const boost::system::error_code& error, ChannelSession::Ptr session);

	std::shared_ptr<boost::asio::io_service> _ioService;
	std::shared_ptr<boost::asio::ssl::context> _sslContext;

	std::shared_ptr<std::thread> _serverThread;

	std::shared_ptr<boost::asio::ip::tcp::acceptor> _acceptor;
	ThreadPool::Ptr _threadPool;

	std::function<void(dev::channel::ChannelException, ChannelSession::Ptr)> _connectionHandler;

	std::string _listenHost = "";
	int _listenPort = 0;
	bool _enableSSL = false;
};

}

}
