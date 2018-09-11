/*
 * PBFTInitializer.h
 *
 *  Created on: 2018年5月10日
 *      Author: mo nan
 */
#pragma once

#include <libdevcrypto/Common.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl.hpp>

namespace dev {

namespace eth {

DEV_SIMPLE_EXCEPTION(PrivateKeyError);
DEV_SIMPLE_EXCEPTION(PrivateKeyNotExists);
DEV_SIMPLE_EXCEPTION(CertificateError);
DEV_SIMPLE_EXCEPTION(CertificateNotExists);

class SecureInitializer: public std::enable_shared_from_this<SecureInitializer> {
public:
	typedef std::shared_ptr<SecureInitializer> Ptr;

	void initConfig(const boost::property_tree::ptree &pt);

	KeyPair& key();
	std::shared_ptr<boost::asio::ssl::context> sslContext();

	void setDataPath(std::string dataPath);

private:
	void completePath(std::string &path);
	void loadGmFile();
	KeyPair _key;
	std::shared_ptr<boost::asio::ssl::context> _sslContext;

	std::string _dataPath;
};

}

}
