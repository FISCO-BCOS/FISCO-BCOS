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
 * @file: ChannelServer.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "ChannelServer.h"

#include <libdevcore/easylog.h>

#include <iostream>
#include <boost/bind.hpp>
#include <boost/asio.hpp>

using namespace dev::channel;
using namespace std;

void dev::channel::ChannelServer::run() {
	//是否监听
	if (!_listenHost.empty() && _listenPort > 0) {
		_acceptor = std::make_shared<boost::asio::ip::tcp::acceptor>(*_ioService, boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(_listenHost), _listenPort));

		boost::asio::socket_base::reuse_address optionReuseAddress(true);
		_acceptor->set_option(optionReuseAddress);

		startAccept();
	}

	//让server维护线程
	for (int i = 0; i < 1; ++i) {
		auto serverThreads = std::make_shared<std::thread>([ = ]() {
			while (true) {
				try {
					_ioService->run();
				}
				catch (std::exception &e) {
					LOG(ERROR) << "IO线程错误:" << e.what();
				}

				LOG(ERROR) << "尝试重启 ";

				sleep(1);
			}
		});

		serverThreads->detach();

		_serverThreads.push_back(serverThreads);
	}

	//_ioService->run();
}


void dev::channel::ChannelServer::onAccept(const boost::system::error_code& error, ChannelSession::Ptr session) {
	if (!error) {
		auto remoteEndpoint = session->sslSocket()->lowest_layer().remote_endpoint();
		LOG(TRACE) << "收到新连接: " << remoteEndpoint.address().to_string() << ":" << remoteEndpoint.port();

		session->setHost(remoteEndpoint.address().to_string());
		session->setPort(remoteEndpoint.port());

		if (_enableSSL) {
			LOG(TRACE) << "开始SSL握手";
			session->sslSocket()->async_handshake(
			    boost::asio::ssl::stream_base::server,
			    boost::bind(&ChannelServer::onHandshake, shared_from_this(),
			                boost::asio::placeholders::error, session));
		}
		else {
			_connectionHandler(ChannelException(-1, error.message()), session);
		}
	}
	else {
		LOG(ERROR) << "accept失败: " << error.message();

		try {
			session->sslSocket()->lowest_layer().close();
		}
		catch (exception &e) {
			LOG(ERROR) << "close失败" << e.what();
		}
	}

	startAccept();
}

void dev::channel::ChannelServer::startAccept() {
	try {
		ChannelSession::Ptr session = std::make_shared<ChannelSession>();

		if (_enableSSL) {
			session->setSSLSocket(std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> >(*_ioService, *_sslContext));

			_acceptor->async_accept(session->sslSocket()->lowest_layer(), boost::bind(&ChannelServer::onAccept, shared_from_this(), boost::asio::placeholders::error, session));
		}
		else {
			//session->setSocket(std::make_shared())
		}
	}
	catch (exception &e) {
		LOG(ERROR) << "错误:" << e.what();
	}
}

#if 0
ChannelSession::Ptr dev::channel::ChannelServer::connect(std::string host, int port) throw (Exception) {
	return ChannelSession::Ptr();
}

void dev::channel::ChannelServer::asyncConnect(std::string host, int port, std::function<void(Exception, ChannelSession::Ptr)> callback) {
}
#endif

void dev::channel::ChannelServer::stop() {
	try {
		LOG(DEBUG) << "关闭acceptor";

		_acceptor->close();
	}
	catch (exception &e) {
		LOG(ERROR) << "错误:" << e.what();
	}

	try {
		LOG(DEBUG) << "关闭ioService";
		_ioService->stop();
	}
	catch (exception &e) {
		LOG(ERROR) << "错误:" << e.what();
	}
}

void dev::channel::ChannelServer::onHandshake(const boost::system::error_code& error, ChannelSession::Ptr session) {
	try {
		if (!error) {
			LOG(TRACE) << "SSL握手成功";
			if (_connectionHandler) {
				_connectionHandler(ChannelException(), session);
			}
			else {
				LOG(ERROR) << "connectionHandler为空";
			}
		}
		else {
			LOG(ERROR) << "SSL handshake错误: " << error.message();

			try {
				session->sslSocket()->lowest_layer().close();
			}
			catch (exception &e) {
				LOG(ERROR) << "shutdown错误:" << e.what();
			}
		}
	}
	catch (exception &e) {
		LOG(ERROR) << "错误:" << e.what();
	}
}
