/*
 * RPCInitializer.cpp
 *
 *  Created on: 2018年5月16日
 *      Author: mo nan
 */

#include "RPCInitializer.h"
#include <libweb3jsonrpc/ChannelRPCServer.h>
#include <libchannelserver/ChannelServer.h>

using namespace dev;
using namespace dev::eth;

void RPCInitializer::initConfig(const boost::property_tree::ptree &pt) {
	std::string listenIP = pt.get<std::string>("rpc.listen_ip", "0.0.0.0");
	int listenPort = pt.get<int>("rpc.listen_port", 30301);
	int httpListenPort = pt.get<int>("rpc.http_listen_port", 0);

	if(listenPort > 0) {
		//不设置析构，ModularServer会析构
		_channelRPCServer.reset(new ChannelRPCServer(), [](ChannelRPCServer *p) {
			(void)p;
		});
		_channelRPCServer->setListenAddr(listenIP);
		_channelRPCServer->setListenPort(listenPort);
		_channelRPCServer->setSSLContext(_sslContext);

		auto ioService = std::make_shared<boost::asio::io_service>();

		auto server = std::make_shared<dev::channel::ChannelServer>();
		server->setIOService(ioService);
		server->setSSLContext(_sslContext);

		server->setEnableSSL(true);
		server->setBind(listenIP, listenPort);

		server->setMessageFactory(std::make_shared<dev::channel::ChannelMessageFactory>());

		_channelRPCServer->setChannelServer(server);
	}

	if(httpListenPort > 0) {
		//不设置析构，ModularServer会析构
		_safeHttpServer.reset(new SafeHttpServer(listenIP, httpListenPort), [](SafeHttpServer *p) {
			(void)p;
		});
	}
}

ChannelRPCServer::Ptr RPCInitializer::channelRPCServer() {
	return _channelRPCServer;
}

std::shared_ptr<dev::SafeHttpServer> RPCInitializer::safeHttpServer() {
	return _safeHttpServer;
}

void RPCInitializer::setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext) {
	_sslContext = sslContext;
}
