/*
 * P2PInitializer.h
 *
 *  Created on: 2018年5月10日
 *      Author: mo nan
 */
#pragma once

#include <libp2p/Host.h>
#include <boost/property_tree/ptree.hpp>

namespace dev {

namespace p2p {

class P2PInitializer: public std::enable_shared_from_this<P2PInitializer> {
public:
	typedef std::shared_ptr<P2PInitializer> Ptr;

	void initConfig(const boost::property_tree::ptree &pt);

	Host::Ptr host();
	void setKey(KeyPair key);
	void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext);

private:
	Host::Ptr _host;
	KeyPair _key;
	std::shared_ptr<boost::asio::ssl::context> _sslContext;
};

}

}
