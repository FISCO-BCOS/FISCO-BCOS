/*
 * RPCInitializer.cpp
 *
 *  Created on: 2018年8月15日
 *      Author: mo nan
 */

#include "ConsoleInitializer.h"
#include <libchannelserver/ChannelServer.h>
#include <libconsole/ConsoleMessage.h>

using namespace dev;
using namespace dev::eth;

void ConsoleInitializer::initConfig(const boost::property_tree::ptree &pt) {
	std::string listenIP = pt.get<std::string>("rpc.listen_ip", "0.0.0.0");
	int listenPort = pt.get<int>("rpc.console_port", 5053);

	if(listenPort > 0) {
		auto ioService = std::make_shared<boost::asio::io_service>();

		auto channelServer = std::make_shared<dev::channel::ChannelServer>();
		channelServer->setBind(listenIP, listenPort);
		channelServer->setEnableSSL(false);
		channelServer->setIOService(ioService);
		channelServer->setSSLContext(_sslContext);
		channelServer->setMessageFactory(std::make_shared<dev::console::ConsoleMessageFactory>());

		_consoleServer = std::make_shared<dev::console::ConsoleServer>();
		_consoleServer->setChannelServer(channelServer);
	}
}

void ConsoleInitializer::setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext) {
	_sslContext = sslContext;
}
