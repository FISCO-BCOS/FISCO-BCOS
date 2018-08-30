/*
 * PBFTInitializer.h
 *
 *  Created on: 2018年5月16日
 *      Author: mo nan
 */
#pragma once

#include <libconsole/ConsoleServer.h>
#include <boost/property_tree/ptree.hpp>

namespace dev {

namespace eth {

class ConsoleInitializer: public std::enable_shared_from_this<ConsoleInitializer> {
public:
	typedef std::shared_ptr<ConsoleInitializer> Ptr;

	void initConfig(const boost::property_tree::ptree &pt);

	dev::console::ConsoleServer::Ptr consoleServer() { return _consoleServer; };

	void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext);

private:
	dev::console::ConsoleServer::Ptr _consoleServer;
	std::shared_ptr<boost::asio::ssl::context> _sslContext;
};

}

}
