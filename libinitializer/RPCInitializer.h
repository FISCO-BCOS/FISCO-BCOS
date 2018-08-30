/*
 * PBFTInitializer.h
 *
 *  Created on: 2018年5月16日
 *      Author: mo nan
 */
#pragma once

#include <libweb3jsonrpc/ChannelRPCServer.h>
#include <libweb3jsonrpc/SafeHttpServer.h>
#include <boost/property_tree/ptree.hpp>

namespace dev {

namespace eth {

class RPCInitializer: public std::enable_shared_from_this<RPCInitializer> {
public:
	typedef std::shared_ptr<RPCInitializer> Ptr;

	void initConfig(const boost::property_tree::ptree &pt);

	ChannelRPCServer::Ptr channelRPCServer();
	std::shared_ptr<dev::SafeHttpServer> safeHttpServer();

	void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext);

private:
	ChannelRPCServer::Ptr _channelRPCServer;
	std::shared_ptr<boost::asio::ssl::context> _sslContext;
	std::shared_ptr<dev::SafeHttpServer> _safeHttpServer;
};

}

}
